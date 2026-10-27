#include "gtest/gtest.h"
#include <cstdint>

#include "stride/stridegen/codeanalysis.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include "llvm/IR/Function.h"

namespace {

TEST(JITControlFlowTest, ReactionBasic) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto Out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "TestDomain")
                 .value_or(0.0);
  EXPECT_EQ(Out, 3);
}

TEST(JITControlFlowTest, ReactionSingleStream) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "reaction_single_stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto Out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "TestDomain")
                 .value_or(0.0);
  EXPECT_EQ(Out, 3);
}

TEST(JITControlFlowTest, ReactionCondition) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reactioncondition.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto Out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "TestDomain")
                 .value_or(0.0);
  EXPECT_EQ(Out, 0);

  strenv.invoke("TestDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "TestDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 3);
}

TEST(JITControlFlowTest, ReactionTerminateWhen) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "reaction_terminate_when.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto Out = strenv
                 .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                       "TestDomain")
                 .value_or(0);
  EXPECT_EQ(Out, 2);

  auto Done =
      strenv
          .getStateVar<bool>(statePtr.get(), "Done", std::nullopt, "TestDomain")
          .value_or(false);
  EXPECT_TRUE(Done);
}

TEST(JITControlFlowTest, ReactionInModule) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction_in_module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 3.0, std::nullopt, "RootDomain");
  strenv.setStateVar(statePtr.get(), "Out", 1.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto Out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "RootDomain")
                 .value_or(0.0);
  EXPECT_EQ(Out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 4.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 5.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 6.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 6.0);

  strenv.setStateVar(statePtr.get(), "In", 10.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 10.0);
}

TEST(JITControlFlowTest, LoopBasic) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  int32_t List[20] = {1000, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      1,    1, 1, 1, 1, 1, 1, 1, 1, 100};
  for (int i = 0; i < 20; ++i) {
    strenv.setStateVar(statePtr.get(), "List", List[i], i, "RootDomain");
  }

  strenv.invoke("RootDomain_process", args);
  auto Out = strenv
                 .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                       "RootDomain")
                 .value_or(0);
  EXPECT_EQ(Out, 1118);
}

TEST(JITControlFlowTest, StandaloneReactionSimple) {
  strd::ASTNode tree;
  tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "reaction_simple.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("ReactionSimple", scope, tree);
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
      strenv.getFunction("ReactionSimple");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  double out = 1.0;

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  Entry(&out);
  EXPECT_FLOAT_EQ(out, 4.0);
}

TEST(JITControlFlowTest, StandaloneReactionTerminateWhen) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "reaction_terminate_when.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl = strd::ASTQuery::findDeclarationByName("ReactionTerminateWhen",
                                                        scope, tree);
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
      strenv.getFunction("ReactionTerminateWhen");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  int32_t out = 0;
  bool done = false;

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, bool *)>();
  Entry(&out, &done);
  EXPECT_EQ(out, 2);
  EXPECT_TRUE(done);
}

TEST(JITControlFlowTest, StandaloneReaction) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "reaction.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("Reaction", scope, tree);
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
      strenv.getFunction("Reaction");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  double out = 1.0;

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  Entry(&out);
  EXPECT_FLOAT_EQ(out, 4.0);
}

TEST(JITControlFlowTest, StandaloneLoopSimple) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "loop_simple.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  scope.push_back({nullptr, {}});
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == strd::AST::Declaration ||
        node->getNodeType() == strd::AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<strd::DeclarationNode>(node);
      if (decl->getEntityType() == "platformModule") {
        strd::StrideGenerator::generatePlatformFunctionSignature(
            decl, scope.back().second, strenv.mStrideEnv);
      }
    }
  }

  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("LoopSimple", scope, tree);
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
      strenv.getFunction("LoopSimple");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  EXPECT_NE(Entry, nullptr);

  int32_t out = 0;
  Entry(&out);
  EXPECT_EQ(out, 11);
}

TEST(JITControlFlowTest, StandaloneLoopTerminateWhen) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "loop_terminate_when.stride");
  ASSERT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  scope.push_back({nullptr, {}});
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == strd::AST::Declaration ||
        node->getNodeType() == strd::AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<strd::DeclarationNode>(node);
      if (decl->getEntityType() == "platformModule") {
        strd::StrideGenerator::generatePlatformFunctionSignature(
            decl, scope.back().second, strenv.mStrideEnv);
      }
    }
  }

  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("LoopTerminateWhen", scope, tree);
  ASSERT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  ASSERT_NE(func, nullptr);
  ASSERT_NE(func->codegen(strenv.mStrideEnv), nullptr);
  ASSERT_TRUE(strenv.compileInMemory());

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("LoopTerminateWhen");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  int32_t out = 0;
  EntrySym->toPtr<void (*)(int32_t *)>()(&out);
  EXPECT_EQ(out, 10);
}

TEST(JITControlFlowTest, StandaloneLoop) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "loop.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  scope.push_back({nullptr, {}});
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == strd::AST::Declaration ||
        node->getNodeType() == strd::AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<strd::DeclarationNode>(node);
      if (decl->getEntityType() == "platformModule") {
        strd::StrideGenerator::generatePlatformFunctionSignature(
            decl, scope.back().second, strenv.mStrideEnv);
      }
    }
  }

  auto funcDecl = strd::ASTQuery::findDeclarationByName("Add", scope, tree);
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

  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym = strenv.getFunction("Add");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  EXPECT_NE(Entry, nullptr);

  int32_t out = 0;
  int32_t in[20];
  for (int i = 0; i < 20; ++i) {
    in[i] = 2; // sum should be 40
  }
  int32_t size = 20;

  Entry(&out, in, size);
  EXPECT_EQ(out, 40);
}

} // namespace
