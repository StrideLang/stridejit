#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <string>

#include "stride/parser/ast.h"
#include "stride/stridejit/strideenvironment.hpp"

TEST(StridecCompilerTest, SensorDSPHeaderAndObjectGeneration) {
  strd::StrideEnvironment env;
  bool ok = env.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "sensor_dsp.stride");
  ASSERT_TRUE(ok);

  std::string headerStr = env.generateCHeaderString("SensorDSP");
  EXPECT_FALSE(headerStr.empty());
  EXPECT_NE(headerStr.find("#ifndef SENSOR_DSP_H"), std::string::npos);
  EXPECT_NE(headerStr.find("SensorDSP_init"), std::string::npos);
  EXPECT_NE(headerStr.find("SensorDSP_process"), std::string::npos);
  EXPECT_NE(headerStr.find("RawSensorValue"), std::string::npos);
  EXPECT_NE(headerStr.find("AlertThreshold"), std::string::npos);
  EXPECT_NE(headerStr.find("FilteredValue"), std::string::npos);
  EXPECT_NE(headerStr.find("AlertTriggered"), std::string::npos);

  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stride_sensor_dsp_test";
  std::filesystem::create_directories(tempDir);

  // 1. Emit C header
  std::string headerPath = (tempDir / "sensor_dsp.h").string();
  EXPECT_TRUE(env.emitCHeader(headerPath, "SensorDSP"));
  EXPECT_TRUE(std::filesystem::exists(headerPath));

  // 2. Emit host native object file
  std::string hostObjPath = (tempDir / "sensor_dsp_host.o").string();
  EXPECT_TRUE(env.emitObjectFile(hostObjPath));
  EXPECT_TRUE(std::filesystem::exists(hostObjPath));
  EXPECT_GT(std::filesystem::file_size(hostObjPath), 0);

  // 3. Emit Raspberry Pi Pico (RP2040 ARM Cortex-M0+) object file
  std::string picoObjPath = (tempDir / "sensor_dsp_pico.o").string();
  EXPECT_TRUE(env.emitObjectFile(picoObjPath, "thumbv6m-none-eabi", "cortex-m0plus"));
  EXPECT_TRUE(std::filesystem::exists(picoObjPath));
  EXPECT_GT(std::filesystem::file_size(picoObjPath), 0);

  // 4. Emit STM32 Cortex-M4 object file
  std::string stm32ObjPath = (tempDir / "sensor_dsp_stm32.o").string();
  EXPECT_TRUE(env.emitObjectFile(stm32ObjPath, "thumbv7em-none-eabi", "cortex-m4"));
  EXPECT_TRUE(std::filesystem::exists(stm32ObjPath));
  EXPECT_GT(std::filesystem::file_size(stm32ObjPath), 0);

  std::filesystem::remove_all(tempDir);
}

TEST(StridecCompilerTest, SensorDSPExecution) {
  strd::StrideEnvironment env;
  bool ok = env.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "sensor_dsp.stride");
  ASSERT_TRUE(ok);

  ok = env.compileInMemory();
  ASSERT_TRUE(ok);

  auto statePtr = env.allocateSharedState("SensorDSP");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  // Initialize domain state
  env.invoke("SensorDSP_init", args);

  // Set inputs
  env.setStateVar(statePtr.get(), "RawSensorValue", 10.0, std::nullopt, "SensorDSP");
  env.setStateVar(statePtr.get(), "AlertThreshold", 5.0, std::nullopt, "SensorDSP");

  // Step 1: Filter starts rising towards 10.0: (0.15 * 10) + (0.85 * 0) = 1.5
  env.invoke("SensorDSP_process", args);
  auto filtered1 = env.getStateVar<double>(statePtr.get(), "FilteredValue", std::nullopt, "SensorDSP");
  auto alert1 = env.getStateVar<bool>(statePtr.get(), "AlertTriggered", std::nullopt, "SensorDSP");
  ASSERT_TRUE(filtered1.has_value());
  EXPECT_NEAR(filtered1.value(), 1.5, 1e-4);
  EXPECT_FALSE(alert1.value_or(true));

  // Step 2: (0.15 * 10) + (0.85 * 1.5) = 1.5 + 1.275 = 2.775
  env.invoke("SensorDSP_process", args);
  auto filtered2 = env.getStateVar<double>(statePtr.get(), "FilteredValue", std::nullopt, "SensorDSP");
  auto alert2 = env.getStateVar<bool>(statePtr.get(), "AlertTriggered", std::nullopt, "SensorDSP");
  ASSERT_TRUE(filtered2.has_value());
  EXPECT_NEAR(filtered2.value(), 2.775, 1e-4);
  EXPECT_FALSE(alert2.value_or(true));

  // Simulate multiple steps until filter exceeds threshold 5.0
  for (int i = 0; i < 15; ++i) {
    env.invoke("SensorDSP_process", args);
  }
  auto filteredFinal = env.getStateVar<double>(statePtr.get(), "FilteredValue", std::nullopt, "SensorDSP");
  auto alertFinal = env.getStateVar<bool>(statePtr.get(), "AlertTriggered", std::nullopt, "SensorDSP");
  ASSERT_TRUE(filteredFinal.has_value());
  EXPECT_GT(filteredFinal.value(), 5.0);
  EXPECT_TRUE(alertFinal.value_or(false));
}

TEST(StridecCompilerTest, MultiFileUnifiedCompilation) {
  strd::StrideEnvironment env;
  std::vector<std::string> files = {
      STRIDEJIT_TESTS_SOURCE_DIR "sensor_dsp.stride",
      STRIDEJIT_TESTS_SOURCE_DIR "math_utils.stride"
  };

  bool ok = env.generateIr(files, /*emitAllFunctions=*/true);
  ASSERT_TRUE(ok);

  std::string headerStr = env.generateCHeaderString();
  EXPECT_NE(headerStr.find("SensorDSP_process"), std::string::npos);
  EXPECT_NE(headerStr.find("Gain"), std::string::npos);
  EXPECT_NE(headerStr.find("Offset"), std::string::npos);

  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stride_multi_file_jit_test";
  std::filesystem::create_directories(tempDir);

  std::string objPath = (tempDir / "multi.o").string();
  EXPECT_TRUE(env.emitObjectFile(objPath));
  EXPECT_TRUE(std::filesystem::exists(objPath));
  EXPECT_GT(std::filesystem::file_size(objPath), 0);

  std::filesystem::remove_all(tempDir);
}

TEST(StridecCompilerTest, StandaloneFunctionLibraryWithFlag) {
  strd::StrideEnvironment env;
  bool ok = env.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "math_utils.stride", /*emitAllFunctions=*/true);
  ASSERT_TRUE(ok);

  std::string headerStr = env.generateCHeaderString();
  EXPECT_NE(headerStr.find("Gain"), std::string::npos);
  EXPECT_NE(headerStr.find("Offset"), std::string::npos);

  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stride_standalone_fn_jit_test";
  std::filesystem::create_directories(tempDir);

  std::string objPath = (tempDir / "math.o").string();
  EXPECT_TRUE(env.emitObjectFile(objPath));
  EXPECT_TRUE(std::filesystem::exists(objPath));
  EXPECT_GT(std::filesystem::file_size(objPath), 0);

  std::filesystem::remove_all(tempDir);
}
