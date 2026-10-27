#include "gtest/gtest.h"

#include "stride/stridegen/codeanalysis.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include "llvm/IR/Function.h"

namespace {

TEST(JITFunctionTest, ModuleDeclaration) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);
  auto out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "RootDomain")
                 .value_or(0.0);
  EXPECT_EQ(out, 5);
}

TEST(JITFunctionTest, ModuleInternalSignals) {
  strd::StrideEnvironment strenv;
  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module_internal.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "TestDomain")
                 .value_or(0.0);
  EXPECT_EQ(out, 5);
}

TEST(JITFunctionTest, ModuleProperties) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module_properties.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 2.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);

  auto out0 = strenv.getStateVar<double>(statePtr.get(), "Out", 0, "RootDomain")
                  .value_or(0.0);
  auto out1 = strenv.getStateVar<double>(statePtr.get(), "Out", 1, "RootDomain")
                  .value_or(0.0);

  EXPECT_EQ(out0, 6);
  EXPECT_EQ(out1, 8);
}

TEST(JITFunctionTest, LiteralModuleInput) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "literal_input.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto out0 =
      strenv.getStateVar<int32_t>(statePtr.get(), "Out", 0, "TestDomain")
          .value_or(0);
  auto out1 =
      strenv.getStateVar<int32_t>(statePtr.get(), "Out", 1, "TestDomain")
          .value_or(0);
  EXPECT_EQ(out0, 5);
  EXPECT_EQ(out1, 8);
}

TEST(JITFunctionTest, StandaloneModuleSimple) {
  strd::ASTNode tree;
  tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module_simple.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("ModuleSimple", scope, tree);
  EXPECT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  EXPECT_NE(func, nullptr);

  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  auto *llvmFunc = llvm::dyn_cast<llvm::Function>(v);
  EXPECT_NE(llvmFunc, nullptr);
  EXPECT_FALSE(llvmFunc->isDeclaration());

  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("ModuleSimple");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  EXPECT_NE(Entry, nullptr);

  double in = 3.0;
  double out = 0.0;

  Entry(&out, &in);
  EXPECT_FLOAT_EQ(out, 5.0);
}

TEST(JITFunctionTest, StandaloneModule) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl = strd::ASTQuery::findDeclarationByName("AddTwo", scope, tree);
  EXPECT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  EXPECT_NE(func, nullptr);

  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  // Ensure it generated an actual function with a body
  auto *llvmFunc = llvm::dyn_cast<llvm::Function>(v);
  EXPECT_NE(llvmFunc, nullptr);
  EXPECT_FALSE(llvmFunc->isDeclaration());

  // Compile and execute the generated standalone function
  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("AddTwo");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  EXPECT_NE(Entry, nullptr);

  double in = 3.0;
  double out = 0.0;

  Entry(&out, &in);
  EXPECT_FLOAT_EQ(out, 5.0);
}

TEST(JITFunctionTest, ExternalFunctions) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "external_function.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  {
    strenv.setStateVar(statePtr.get(), "Out", true, std::nullopt, "RootDomain");
    strenv.invoke("RootDomain_init", args);
    // out should get initialized to false
    auto out = strenv
                   .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                         "RootDomain")
                   .value_or(true);
    EXPECT_EQ(out, false);
  }

  strenv.setStateVar(statePtr.get(), "Out", false, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto out = strenv
                 .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                       "RootDomain")
                 .value_or(false);
  EXPECT_EQ(out, true);
}

TEST(JITFunctionTest, PlatformFunction) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "platform_module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "InReal", 3.0, std::nullopt, "TestDomain");
  strenv.setStateVar(statePtr.get(), "InInt", 2, std::nullopt, "TestDomain");

  strenv.invoke("TestDomain_process", args);

  auto OutInt = strenv
                    .getStateVar<double>(statePtr.get(), "OutInt", std::nullopt,
                                         "TestDomain")
                    .value_or(0.0);
  auto OutReal = strenv
                     .getStateVar<double>(statePtr.get(), "OutReal",
                                          std::nullopt, "TestDomain")
                     .value_or(0.0);
  auto OutWrapped = strenv
                        .getStateVar<double>(statePtr.get(), "OutWrapped",
                                             std::nullopt, "TestDomain")
                        .value_or(0.0);

  EXPECT_EQ(OutInt, 2);
  EXPECT_DOUBLE_EQ(OutReal, 5.5);
  EXPECT_DOUBLE_EQ(OutWrapped, 5.5);
}

TEST(JITFunctionTest, Polymorphism) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "polymorphism_external.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "InInt", 5, std::nullopt, "TestDomain");
  strenv.setStateVar(statePtr.get(), "InReal", 5.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);

  auto compInt = strenv
                     .getStateVar<double>(statePtr.get(), "CompInt",
                                          std::nullopt, "TestDomain")
                     .value_or(0.0);
  auto compReal = strenv
                      .getStateVar<double>(statePtr.get(), "CompReal",
                                           std::nullopt, "TestDomain")
                      .value_or(0.0);

  EXPECT_FALSE(compInt != 0.0);
  EXPECT_FALSE(compReal != 0.0);

  strenv.setStateVar(statePtr.get(), "InInt", 0, std::nullopt, "TestDomain");
  strenv.setStateVar(statePtr.get(), "InReal", 0.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);

  compInt = strenv
                .getStateVar<double>(statePtr.get(), "CompInt", std::nullopt,
                                     "TestDomain")
                .value_or(0.0);
  compReal = strenv
                 .getStateVar<double>(statePtr.get(), "CompReal", std::nullopt,
                                      "TestDomain")
                 .value_or(0.0);
  EXPECT_TRUE(compInt != 0.0);
  EXPECT_TRUE(compReal != 0.0);
}

TEST(JITFunctionTest, MathFunction) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "functions.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);
  auto out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "RootDomain")
                 .value_or(0.0);
  EXPECT_EQ(out, 1.0);
}

TEST(JITFunctionTest, FunctionOverload) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "function_overload.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("TestDomain_process");
  EXPECT_TRUE((bool)EntrySym);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "InInt", 10, std::nullopt, "TestDomain");
  strenv.setStateVar(statePtr.get(), "InReal", 10.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);

  auto outInt = strenv.getStateVar<int>(statePtr.get(), "OutInt", std::nullopt, "TestDomain").value_or(0);
  auto outReal = strenv.getStateVar<double>(statePtr.get(), "OutReal", std::nullopt, "TestDomain").value_or(0.0);

  EXPECT_EQ(outInt, 12);
  EXPECT_DOUBLE_EQ(outReal, 12.5);
}

} // namespace
