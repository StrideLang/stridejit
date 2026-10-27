#include "gtest/gtest.h"
#include <cstdint>

#include "stride/stridejit/stridecompiler.hpp"
#include "stride/stridejit/strideenvironment.hpp"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"

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

} // namespace
