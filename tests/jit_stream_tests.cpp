#include "gtest/gtest.h"
#include <cmath>

#include "stride/stridejit/strideenvironment.hpp"

namespace {

TEST(JITStreamTest, BasicStream) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
}

TEST(JITStreamTest, TwoParallelStreams) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "twostreams.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 0.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "RootDomain")
                 .value_or(0.0);
  EXPECT_EQ(out, cos(cos(0.0)));

  strenv.setStateVar(statePtr.get(), "In", 3.14159, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_FLOAT_EQ(out, cos(cos(3.14159)));

  strenv.setStateVar(statePtr.get(), "In", 1.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_FLOAT_EQ(out, cos(cos(1.0)));
}

TEST(JITStreamTest, SwitchOut) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "switchout.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 0.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto out = strenv
                 .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                       "RootDomain")
                 .value_or(false);
  EXPECT_FALSE(out);

  strenv.setStateVar(statePtr.get(), "In", 10.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv
            .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                  "RootDomain")
            .value_or(false);
  EXPECT_TRUE(out);

  strenv.setStateVar(statePtr.get(), "In", 1.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv
            .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                  "RootDomain")
            .value_or(false);
  EXPECT_FALSE(out);
}

TEST(JITStreamTest, SwitchDefaults) {
  strd::StrideEnvironment strenv;
  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "switch_defaults.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);
  strenv.invoke("RootDomain_process", args);

  auto getSw = [&](const std::string &name) -> bool {
    return strenv
        .getStateVar<bool>(statePtr.get(), name, std::nullopt, "RootDomain")
        .value_or(false);
  };

  // Scalar defaults
  EXPECT_TRUE(getSw("OutScalarOn"));
  EXPECT_FALSE(getSw("OutScalarOff"));

  // Uniform array defaults (SwArr[3] { default: on })
  EXPECT_TRUE(getSw("OutArrayUniform0"));
  EXPECT_TRUE(getSw("OutArrayUniform1"));
  EXPECT_TRUE(getSw("OutArrayUniform2"));

  // List array defaults (SwList[3] { default: [on, off, on] })
  EXPECT_TRUE(getSw("OutArrayList0"));
  EXPECT_FALSE(getSw("OutArrayList1"));
  EXPECT_TRUE(getSw("OutArrayList2"));
}

TEST(JITStreamTest, ChainedCodeGenerators) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "chained_code_generators.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  auto getResult = [&]() {
    return strenv
        .getStateVar(statePtr.get(), "Result", std::nullopt, "RootDomain")
        .value_or(0);
  };

  // Initially Trigger is off, Result is 0
  EXPECT_EQ(getResult(), 0);

  // Tick with Trigger off -> Entire stream (StepOne and StepTwo) must be
  // skipped -> Result remains 0
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 0);

  // Set Trigger to on (true) -> StepOne(factor: 5) outputs 10 ->
  // StepTwo(offset: 10) outputs 20 -> Result = 20
  strenv.setStateVar(statePtr.get(), "Trigger", true, std::nullopt,
                     "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 20);

  // Turn Trigger back to off (false), reset Result to 999 -> Process tick must
  // NOT execute StepOne or StepTwo
  strenv.setStateVar(statePtr.get(), "Trigger", false, std::nullopt,
                     "RootDomain");
  strenv.setStateVar(statePtr.get(), "Result", 999, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 999);
}

TEST(JITStreamTest, ChainedFunctionsInPipeline) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "chained_functions_in_stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  auto getOutput = [&]() {
    return strenv
        .getStateVar(statePtr.get(), "OutputValue", std::nullopt, "RootDomain")
        .value_or(0);
  };

  // Default InputValue = 10:
  // (10 * 3) + 7 - 2 = 30 + 7 - 2 = 35
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getOutput(), 35);

  // Update InputValue = 4:
  // (4 * 3) + 7 - 2 = 12 + 7 - 2 = 17
  strenv.setStateVar(statePtr.get(), "InputValue", 4, std::nullopt,
                     "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getOutput(), 17);

  // Update InputValue = 0:
  // (0 * 3) + 7 - 2 = 0 + 7 - 2 = 5
  strenv.setStateVar(statePtr.get(), "InputValue", 0, std::nullopt,
                     "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getOutput(), 5);
}

TEST(JITStreamTest, TriggeredReactionStreamPipeline) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "triggered_reaction_stream_pipeline.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  auto getResult = [&]() {
    return strenv
        .getStateVar(statePtr.get(), "Result", std::nullopt, "RootDomain")
        .value_or(0);
  };
  auto getLiteralOnResult = [&]() {
    return strenv
        .getStateVar(statePtr.get(), "LiteralOnResult", std::nullopt,
                     "RootDomain")
        .value_or(0);
  };

  // Before process: both outputs 0
  EXPECT_EQ(getResult(), 0);
  EXPECT_EQ(getLiteralOnResult(), 0);

  // 1st tick with Trigger off:
  // - 3-stage Trigger pipeline is skipped entirely -> Result remains 0
  // - 2-stage 'on' pipeline executes: (4 * 3) + 8 = 20 -> LiteralOnResult
  // becomes 20
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 0);
  EXPECT_EQ(getLiteralOnResult(), 20);

  // 2nd tick with Trigger on:
  // - 3-stage Trigger pipeline executes: ((5 * 3) + 5) * 2 = 40 -> Result
  // becomes 40
  // - 2-stage 'on' pipeline executes again -> LiteralOnResult remains 20
  strenv.setStateVar(statePtr.get(), "Trigger", true, std::nullopt,
                     "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 40);
  EXPECT_EQ(getLiteralOnResult(), 20);

  // 3rd tick with Trigger off:
  // - Mutate Result manually to 123
  // - Process tick: Trigger is off -> 3-stage pipeline does not run -> Result
  // stays 123
  strenv.setStateVar(statePtr.get(), "Trigger", false, std::nullopt,
                     "RootDomain");
  strenv.setStateVar(statePtr.get(), "Result", 123, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 123);
  EXPECT_EQ(getLiteralOnResult(), 20);
}

TEST(JITStreamTest, LogicalComparisonOperations) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "logical_comparisons.stride");
  ASSERT_TRUE(ret);
  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  // Test case 1: InA = true, InB = false
  {
    strenv.setStateVar(statePtr.get(), "InA", 1, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 0, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAnd", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOr", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNot", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutXor", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNand", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNor", std::nullopt, "RootDomain").value_or(1), 0);
  }

  // Test case 2: InA = true, InB = true
  {
    strenv.setStateVar(statePtr.get(), "InA", 1, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 1, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAnd", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOr", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNot", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutXor", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNand", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNor", std::nullopt, "RootDomain").value_or(1), 0);
  }

  // Test case 3: InA = false, InB = false
  {
    strenv.setStateVar(statePtr.get(), "InA", 0, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 0, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAnd", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOr", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNot", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutXor", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNand", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNor", std::nullopt, "RootDomain").value_or(0), 1);
  }

  // Test case 4: InA = false, InB = true
  {
    strenv.setStateVar(statePtr.get(), "InA", 0, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 1, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAnd", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOr", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNot", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutXor", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNand", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNor", std::nullopt, "RootDomain").value_or(1), 0);
  }
}

} // namespace
