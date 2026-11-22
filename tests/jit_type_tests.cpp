#include "gtest/gtest.h"
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "stride/stridejit/stridecompiler.hpp"
#include "stride/stridejit/strideenvironment.hpp"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"
#include "stride/utils/astquery.h"
#include <iostream>

namespace {

TEST(JITTypeTest, IntegerType) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "integer_type.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);
  auto S =
      strenv
          .getStateVar<double>(statePtr.get(), "S", std::nullopt, "RootDomain")
          .value_or(0.0);
  EXPECT_EQ(S, 0);

  strenv.invoke("RootDomain_process", args);
  S = strenv
          .getStateVar<double>(statePtr.get(), "S", std::nullopt, "RootDomain")
          .value_or(0.0);
  EXPECT_EQ(S, 3);
}

TEST(JITTypeTest, SizedTypes) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "sized_types.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_process", args);

  auto get = [&](const std::string &name) {
    return strenv.getStateVar<int64_t>(statePtr.get(), name, std::nullopt,
                                       "RootDomain");
  };
  EXPECT_EQ(static_cast<uint8_t>(get("Wrapped").value_or(-1)), 0);
  EXPECT_EQ(static_cast<int8_t>(get("Narrow").value_or(0)), -3);
  EXPECT_EQ(get("Wide").value_or(0), 3000000000LL);
  EXPECT_EQ(static_cast<uint8_t>(get("Halved").value_or(0)), 127);
  EXPECT_EQ(static_cast<int16_t>(get("Truncated").value_or(0)), 4464);
  EXPECT_EQ(static_cast<int32_t>(get("Platform").value_or(0)), 7);
  EXPECT_EQ(get("Difference").value_or(0), -13);

  auto getReal = [&](const std::string &name) {
    return strenv
        .getStateVar<double>(statePtr.get(), name, std::nullopt, "RootDomain")
        .value_or(-100.0);
  };
  EXPECT_DOUBLE_EQ(getReal("FromUnsigned"), 255.0);
  EXPECT_DOUBLE_EQ(getReal("FromSigned"), -3.0);
  EXPECT_DOUBLE_EQ(getReal("Single"), 0.25);
  EXPECT_DOUBLE_EQ(getReal("Double"), 0.75);

  EXPECT_TRUE(strenv
                  .getStateVar<bool>(statePtr.get(), "UnsignedGreater",
                                     std::nullopt, "RootDomain")
                  .value_or(false));
}

TEST(JITTypeTest, LiteralTypes) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "literal_types.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_process", args);

  auto get = [&](const std::string &name) {
    return strenv.getStateVar<int64_t>(statePtr.get(), name, std::nullopt,
                                       "RootDomain");
  };
  EXPECT_EQ(static_cast<uint8_t>(get("ByteSum").value_or(0)), 4);
  EXPECT_EQ(static_cast<int16_t>(get("SignedByte").value_or(0)), -1);
  EXPECT_EQ(static_cast<uint64_t>(get("AllBits").value_or(0)), UINT64_MAX);
  EXPECT_EQ(get("PlatformUnsigned").value_or(0), 4000000000LL);
  EXPECT_EQ(static_cast<uint8_t>(get("ByteWrap").value_or(-1)), 0);
  EXPECT_EQ(static_cast<int32_t>(get("Widened").value_or(0)), 256);
  EXPECT_EQ(static_cast<int32_t>(get("Doubled").value_or(0)), 510);
  EXPECT_EQ(static_cast<int32_t>(get("NotWrapped").value_or(0)), 555);
  EXPECT_EQ(static_cast<int32_t>(get("Separated").value_or(0)), 1002);
  EXPECT_EQ(static_cast<int32_t>(get("BelowZero").value_or(0)), -10);

  auto getReal = [&](const std::string &name) {
    return strenv
        .getStateVar<double>(statePtr.get(), name, std::nullopt, "RootDomain")
        .value_or(-100.0);
  };
  EXPECT_DOUBLE_EQ(getReal("SingleTenth"), static_cast<double>(0.1f));
  EXPECT_DOUBLE_EQ(getReal("DoubleTenth"), 0.1);
  EXPECT_DOUBLE_EQ(getReal("AllBitsReal"), 18446744073709551615.0);
  EXPECT_DOUBLE_EQ(getReal("SingleProduct"),
                   static_cast<double>(3.0f * 0.1f));
  EXPECT_DOUBLE_EQ(getReal("DoubleProduct"), 3.0 * 0.1);
  EXPECT_DOUBLE_EQ(getReal("DoubledInReal"), 510.0);
  EXPECT_DOUBLE_EQ(getReal("QuarterInReal"), 2.0);
}

TEST(JITTypeTest, DataTypeConversions) {
  strd::StrideCompiler compiler;
  // Aliases share the LLVM type of their target
  EXPECT_EQ(compiler.typesMap["Int"], compiler.typesMap["Int32"]);
  EXPECT_EQ(compiler.typesMap["Real"], compiler.typesMap["Float64"]);
  EXPECT_TRUE(compiler.typesMap["UInt8"]->isIntegerTy(8));
  EXPECT_TRUE(compiler.typesMap["Float32"]->isFloatTy());
  EXPECT_TRUE(compiler.typesMap["Switch"]->isIntegerTy(1));
  EXPECT_FALSE(compiler.getDataTypeInfo("UInt16").isSigned);
  EXPECT_FALSE(compiler.getDataTypeInfo("_IntLiteral").valid());

  auto *fnType = llvm::FunctionType::get(
      llvm::Type::getVoidTy(*compiler.TheContext), false);
  auto *fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage,
                                    "conversions", compiler.TheModule.get());
  compiler.Builder->SetInsertPoint(
      llvm::BasicBlock::Create(*compiler.TheContext, "entry", fn));
  auto *byte = llvm::ConstantInt::get(compiler.typesMap["UInt8"], 200);
  // Constant folding shows which conversion was emitted
  auto *asReal = llvm::dyn_cast<llvm::ConstantFP>(
      compiler.convertToDataType(byte, "UInt8", "Real"));
  ASSERT_NE(asReal, nullptr);
  EXPECT_DOUBLE_EQ(asReal->getValueAPF().convertToDouble(), 200.0);
  auto *asSignedReal = llvm::dyn_cast<llvm::ConstantFP>(
      compiler.convertToDataType(byte, "Int8", "Real"));
  ASSERT_NE(asSignedReal, nullptr);
  EXPECT_DOUBLE_EQ(asSignedReal->getValueAPF().convertToDouble(), -56.0);
  auto *widened = llvm::dyn_cast<llvm::ConstantInt>(
      compiler.convertToDataType(byte, "UInt8", "Int32"));
  ASSERT_NE(widened, nullptr);
  EXPECT_EQ(widened->getSExtValue(), 200);
  auto *single = llvm::ConstantFP::get(compiler.typesMap["Float32"], 1.5);
  auto *asDouble = compiler.convertToDataType(single, "Float32", "Float64");
  EXPECT_TRUE(asDouble->getType()->isDoubleTy());
  auto *asSwitch = llvm::dyn_cast<llvm::ConstantInt>(
      compiler.convertToDataType(byte, "UInt8", "Switch"));
  ASSERT_NE(asSwitch, nullptr);
  EXPECT_TRUE(asSwitch->isOne());
}

TEST(JITTypeTest, TypecastStream) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "typecast_stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);

  auto Int0 = strenv.getStateVar<double>(statePtr.get(), "Ints", 0, "RootDomain")
                  .value_or(0.0);
  auto Int1 = strenv.getStateVar<double>(statePtr.get(), "Ints", 1, "RootDomain")
                  .value_or(0.0);
  EXPECT_EQ(Int0, 1);
  EXPECT_EQ(Int1, 4);

  auto Real0 =
      strenv.getStateVar<double>(statePtr.get(), "Reals", 0, "RootDomain")
          .value_or(0.0);
  auto Real1 =
      strenv.getStateVar<double>(statePtr.get(), "Reals", 1, "RootDomain")
          .value_or(0.0);
  EXPECT_DOUBLE_EQ(Real0, 3.0);
  EXPECT_DOUBLE_EQ(Real1, 5.0);
}

TEST(JITTypeTest, TypecastList) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "typecast_list.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "InReal", 5.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);

  auto compReal = strenv
                      .getStateVar<bool>(statePtr.get(), "CompReal",
                                         std::nullopt, "TestDomain")
                      .value_or(false);
  EXPECT_FALSE(compReal != 0.0);

  strenv.setStateVar(statePtr.get(), "InReal", 0.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);
  compReal = strenv
                 .getStateVar<bool>(statePtr.get(), "CompReal", std::nullopt,
                                    "TestDomain")
                 .value_or(false);
  EXPECT_TRUE(compReal != 0.0);
}

// A program for the AVR framework compiles with that framework's types and
// platform modules: Real is float, Int is i16, and pin and timer modules
// call the AVR runtime. Only IR is generated, so the host needs no AVR
// backend.
TEST(JITTypeTest, AvrFrameworkTypes) {
  strd::StrideEnvironment strenv;
  ASSERT_TRUE(strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "avr_blink.stride"));

  ASSERT_NE(strenv.mStrideEnv.TheModule, nullptr);
  std::string ir;
  llvm::raw_string_ostream os(ir);
  strenv.mStrideEnv.TheModule->print(os, nullptr);
  os.flush();

  EXPECT_NE(strenv.mStrideEnv.TheModule->getFunction("AvrTickDomain_process"),
            nullptr);
  EXPECT_NE(ir.find("@__stride_avr_pin_write(i16"), std::string::npos) << ir;
  EXPECT_NE(ir.find("@__stride_avr_pin_mode(i16"), std::string::npos);
  EXPECT_NE(ir.find("@__stride_avr_tick_sleep(i32"), std::string::npos) << ir;

  // avr-gcc's libraries have no 64-bit float routines: nothing may use double.
  EXPECT_EQ(ir.find("double"), std::string::npos) << ir;
}

} // namespace

TEST(JITTypeTest, AvrObjectAndStateBuffer) {
  strd::StrideEnvironment strenv;
  strenv.setTarget("avr", "atmega328p");
  ASSERT_TRUE(strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "avr_blink.stride"));
  strenv.emitDomainStateBuffers();

  ASSERT_NE(strenv.mStrideEnv.TheModule, nullptr);
  EXPECT_NE(strenv.mStrideEnv.TheModule->getGlobalVariable("LedOn"), nullptr);

  std::string ir;
  llvm::raw_string_ostream os(ir);
  strenv.mStrideEnv.TheModule->print(os, nullptr);
  os.flush();
  EXPECT_EQ(ir.find("cmpxchg"), std::string::npos) << ir;

  std::error_code ec;
  auto objPath =
      std::filesystem::temp_directory_path(ec) / "stride_avr_blink_test.o";
  std::filesystem::remove(objPath, ec);
  EXPECT_TRUE(strenv.emitObjectFile(objPath.string(), "avr", "atmega328p", "",
                                    "static", ""));
  EXPECT_TRUE(std::filesystem::exists(objPath));
  EXPECT_GT(std::filesystem::file_size(objPath), 0u);
  std::filesystem::remove(objPath, ec);
}

TEST(JITTypeTest, SleepSubdomainTicks) {
  const std::string code = R"(
_domainDefinition MainDomain {
    framework: _JitFramework
    rate: 100
    domainFunctionCode: ''
}

_domainDefinition WorkerDomain {
    framework: _JitFramework
    parentDomain: MainDomain
    rate: 100
    domainFunctionCode: ''
}

signal WorkCount { type: Int default: 0 domain: WorkerDomain }

WorkCount + 1 >> WorkCount;
0.03 >> Sleep(domain: WorkerDomain);
)";

  std::error_code ec;
  auto stridePath = std::filesystem::temp_directory_path(ec) /
                    "stride_sleep_subdomain_test.stride";
  {
    std::ofstream out(stridePath);
    ASSERT_TRUE(out.is_open());
    out << code;
  }

  strd::StrideEnvironment strenv;
  bool ok = strenv.generateIr(stridePath.string());
  std::filesystem::remove(stridePath, ec);
  ASSERT_TRUE(ok);
  ASSERT_TRUE(strenv.compileInMemory());
  auto mainState = strenv.allocateSharedState("MainDomain");
  void *mainArgs[1] = {mainState.get()};

  auto count = [&]() {
    int32_t *ptr = strenv.getGlobal<int32_t>("WorkCount");
    return ptr ? *ptr : -1;
  };

  // Tick 1: WorkerDomain runs (WorkCount = 1) and sleeps 0.03s * 100Hz = 3 ticks.
  EXPECT_EQ(strenv.invoke("MainDomain_process", mainArgs), 0);
  EXPECT_EQ(count(), 1);

  // Ticks 2, 3, 4: WorkerDomain is sleeping, so WorkCount stays 1.
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(strenv.invoke("MainDomain_process", mainArgs), 0);
    EXPECT_EQ(count(), 1);
  }

  // Tick 5: WorkerDomain wakes up and runs again (WorkCount = 2).
  EXPECT_EQ(strenv.invoke("MainDomain_process", mainArgs), 0);
  EXPECT_EQ(count(), 2);
}

// Memory-mapped register access through native llvm:: instructions, as the
// portable GPIO modules use it. The "register" here is a host buffer whose
// address the program reads from a signal, so the test runs on any host.
TEST(JITTypeTest, VolatileRegisterLoadStore) {
  const std::string code = R"(
_domainDefinition MainDomain {
    framework: _JitFramework
    rate: 100
    domainFunctionCode: ''
}

platformModule _TestRegWrite8 {
    inputs: [UInt64, UInt8]
    outputs: []
    processing: "llvm::store volatile i8 %%intokens:1%%, %%intokens:0%%"
    inherits: [_Signal]
}

platformModule _TestRegRead32 {
    inputs: [UInt64]
    outputs: [UInt32]
    processing: "%%outtokens:0%% = llvm::load volatile i32, %%intokens:0%%"
    inherits: [_Signal]
}

signal Address { type: UInt64 default: 0 domain: MainDomain }
signal WordAddress { type: UInt64 default: 0 domain: MainDomain }
signal Value { type: UInt8 default: 0 domain: MainDomain }
signal Word { type: UInt32 default: 0 domain: MainDomain }

[Address, Value] >> _TestRegWrite8();
WordAddress >> _TestRegRead32() >> Word;
)";

  std::error_code ec;
  auto stridePath = std::filesystem::temp_directory_path(ec) /
                    "stride_volatile_register_test.stride";
  {
    std::ofstream out(stridePath);
    ASSERT_TRUE(out.is_open());
    out << code;
  }

  strd::StrideEnvironment strenv;
  bool ok = strenv.generateIr(stridePath.string());
  std::filesystem::remove(stridePath, ec);
  ASSERT_TRUE(ok);

  std::string ir;
  {
    llvm::raw_string_ostream os(ir);
    strenv.mStrideEnv.TheModule->print(os, nullptr);
  }
  EXPECT_NE(ir.find("store volatile i8"), std::string::npos) << ir;
  EXPECT_NE(ir.find("load volatile i32"), std::string::npos) << ir;
  EXPECT_NE(ir.find("inttoptr"), std::string::npos) << ir;

  ASSERT_TRUE(strenv.compileInMemory());
  auto mainState = strenv.allocateSharedState("MainDomain");
  void *mainArgs[1] = {mainState.get()};

  volatile uint8_t byteRegister = 0;
  volatile uint32_t wordRegister = 0xA5C3F00Fu;
  void *state = mainState.get();
  ASSERT_TRUE(strenv.setStateVar<uint64_t>(
      state, "Address", reinterpret_cast<uint64_t>(&byteRegister),
      std::nullopt, "MainDomain"));
  ASSERT_TRUE(strenv.setStateVar<uint64_t>(
      state, "WordAddress", reinterpret_cast<uint64_t>(&wordRegister),
      std::nullopt, "MainDomain"));
  ASSERT_TRUE(strenv.setStateVar<uint8_t>(state, "Value", 0x5A, std::nullopt,
                                          "MainDomain"));

  EXPECT_EQ(strenv.invoke("MainDomain_process", mainArgs), 0);
  EXPECT_EQ(byteRegister, 0x5A);
  EXPECT_EQ(strenv.getStateVar<uint32_t>(state, "Word", std::nullopt,
                                         "MainDomain")
                .value_or(0),
            0xA5C3F00Fu);
}

// The standard platform domain aliases resolve to AVR's domains, and Sleep
// calls each domain's declared sleepModule: the loop sleeps without blocking
// the tick domain.
TEST(JITTypeTest, AvrStandardDomainsAndSleep) {
  const std::string code = R"(
use AVR version 1.0 on ArduinoUno

switch Blink { default: off domain: PlatformLoopDomain }
signal Count { type: Int default: 0 domain: PlatformTimerDomain }

Blink >> Not() >> Blink;
0.5 >> Sleep(domain: PlatformLoopDomain);

Count + 1 >> Count;
0.25 >> Sleep(domain: PlatformTimerDomain);
)";

  std::error_code ec;
  auto stridePath = std::filesystem::temp_directory_path(ec) /
                    "stride_avr_standard_domains_test.stride";
  {
    std::ofstream out(stridePath);
    ASSERT_TRUE(out.is_open());
    out << code;
  }

  strd::StrideEnvironment strenv;
  bool ok = strenv.generateIr(stridePath.string());
  std::filesystem::remove(stridePath, ec);
  ASSERT_TRUE(ok);

  auto *module = strenv.mStrideEnv.TheModule.get();
  ASSERT_NE(module, nullptr);
  EXPECT_NE(module->getFunction("AvrLoopDomain_process"), nullptr);
  EXPECT_NE(module->getFunction("AvrTickDomain_process"), nullptr);

  std::string ir;
  {
    llvm::raw_string_ostream os(ir);
    module->print(os, nullptr);
  }
  // Loop sleep is cooperative, never the blocking delay.
  EXPECT_NE(ir.find("@__stride_avr_loop_sleep(i32"), std::string::npos) << ir;
  EXPECT_EQ(ir.find("@__stride_avr_sleep_ms"), std::string::npos) << ir;
  EXPECT_NE(ir.find("@__stride_avr_tick_sleep(i32"), std::string::npos) << ir;
}

TEST(JITTypeTest, MixedTypeComparisonUsesRealVariant) {
  // GreaterOrEqual and Less declare their Int variant first; [Real, Int]
  // must still compare as reals.
  strd::StrideEnvironment strenv;
  ASSERT_TRUE(strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                                "mixed_type_comparison.stride"));
  ASSERT_TRUE(strenv.compileInMemory());

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  for (double in : {2.5, 3.0, 3.5}) {
    strenv.setStateVar(statePtr.get(), "In", in, std::nullopt, "RootDomain");
    strenv.invoke("RootDomain_process", args);
    auto atLeast = strenv.getStateVar<bool>(statePtr.get(), "AtLeast",
                                            std::nullopt, "RootDomain");
    auto below = strenv.getStateVar<bool>(statePtr.get(), "Below",
                                          std::nullopt, "RootDomain");
    ASSERT_TRUE(atLeast.has_value()) << in;
    ASSERT_TRUE(below.has_value()) << in;
    EXPECT_EQ(*atLeast, in >= 3.0) << in;
    EXPECT_EQ(*below, in < 3.0) << in;
  }
}
