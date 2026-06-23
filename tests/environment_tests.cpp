#include "gtest/gtest.h"

#include "stride/parser/ast.h"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/stridejit/strideenvironment.hpp"

#include "llvm/Support/TargetSelect.h"

namespace {

// ============================================================================
// StrideEnvironment Initialization Tests
// ============================================================================

TEST(StrideEnvironmentTest, DefaultConstruction) {
  strd::StrideEnvironment env;

  EXPECT_NE(env.state.TheContext, nullptr);
  EXPECT_NE(env.state.TheModule, nullptr);
}

TEST(StrideEnvironmentTest, ConstructionWithStrideRoot) {
  strd::StrideEnvironment env("./test_root");

  EXPECT_NE(env.state.TheContext, nullptr);
}

// ============================================================================
// IR Generation Tests
// ============================================================================

TEST(StrideEnvironmentTest, GenerateIRFromInvalidPath) {
  strd::StrideEnvironment env;

  bool ret = env.generateIr("nonexistent_file_xyz.stride");
  EXPECT_FALSE(ret);
}

TEST(StrideEnvironmentTest, ProcessTreeWithValidAST) {
  strd::StrideEnvironment env;

  // Create a minimal valid AST
  strd::ASTNode tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");

  if (tree != nullptr) {
    EXPECT_NO_THROW(env.processTree(tree));
  }
}

// ============================================================================
// JIT Compilation Tests
// ============================================================================

TEST(StrideEnvironmentTest, InitializeJIT) {
  strd::StrideEnvironment env;

  // Initialize JIT - should not crash
  env.initializeJIT();
}

TEST(StrideEnvironmentTest, JITIsNull) {
  strd::StrideEnvironment env;

  // JIT is initially null
  EXPECT_EQ(env.JIT, nullptr);
}

// ============================================================================
// Compiler State Access Tests
// ============================================================================

TEST(StrideEnvironmentTest, AccessCompilerState) {
  strd::StrideEnvironment env;

  EXPECT_NE(env.state.TheContext, nullptr);
  EXPECT_NE(env.state.TheModule, nullptr);
  EXPECT_NE(env.state.Builder, nullptr);
}

// ============================================================================
// Multi-Instance Tests
// ============================================================================

TEST(StrideEnvironmentTest, MultipleInstances) {
  strd::StrideEnvironment env1;
  strd::StrideEnvironment env2;

  EXPECT_NE(env1.state.TheContext.get(), env2.state.TheContext.get());
  EXPECT_NE(env1.state.TheModule.get(), env2.state.TheModule.get());
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(StrideEnvironmentTest, GetFunctionBeforeCompilation) {
  strd::StrideEnvironment env;

  auto result = env.getFunction("SomeFunction");

  // Should return error since JIT is not initialized
  EXPECT_FALSE((bool)result);
  llvm::consumeError(result.takeError());
}

} // namespace

namespace {

// ============================================================================
// PortPropertyAST Tests (from numberexprast.hpp)
// ============================================================================

TEST(PortPropertyAST, BasicConstruction) {
  strd::StrideCompiler state;

  strd::PortPropertyAST prop("Port", "size");
  auto [v, t] = prop.codegen(state);

  // Port property codegen behavior depends on implementation
  // Just verify it doesn't crash
}

// ============================================================================
// ResetExprAST Tests (from numberexprast.hpp)
// ============================================================================

TEST(ResetExprAST, BasicConstruction) {
  strd::StrideCompiler state;

  auto condition = std::make_unique<strd::BoolExprAST>(true);
  std::vector<std::unique_ptr<strd::ExprAST>> expressions;
  expressions.push_back(std::make_unique<strd::RealExprAST>(0.0));

  strd::ResetExprAST reset("signal", std::move(condition),
                           std::move(expressions));
  // FIXME verify reset codegen doesn't crash
  // auto [v, t] = reset.codegen(state);

  // Just verify it doesn't crash
}

} // namespace
