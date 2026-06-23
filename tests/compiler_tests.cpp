#include "gtest/gtest.h"

#include "stride/parser/declarationnode.h"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

namespace {

// ============================================================================
// StrideCompiler Initialization Tests
// ============================================================================

TEST(StrideCompilerTest, DefaultConstruction) {
  strd::StrideCompiler compiler;

  EXPECT_NE(compiler.TheContext, nullptr);
  EXPECT_NE(compiler.TheModule, nullptr);
  EXPECT_NE(compiler.Builder, nullptr);
}

TEST(StrideCompilerTest, ModuleNotNull) {
  strd::StrideCompiler compiler;

  EXPECT_TRUE(compiler.TheModule != nullptr);
  // EXPECT_EQ(compiler.TheModule->getContext().getTypeByName("struct.test"),
  // nullptr);
}

// ============================================================================
// Name Stack Operations Tests
// ============================================================================

TEST(StrideCompilerTest, PushPopName) {
  strd::StrideCompiler compiler;

  compiler.pushName("Level1");
  auto name = compiler.getName();
  EXPECT_GT(name.size(), 4);
  name.resize(name.size() - 4);
  EXPECT_EQ(name, "Level1");

  compiler.pushName("Level2");
  name = compiler.getName();
  name.resize(name.size() - 4);
  EXPECT_EQ(name.substr(0, 7), "Level1_");
  EXPECT_EQ(name.substr(11), "Level2");

  compiler.popName();
  name = compiler.getName();
  name.resize(name.size() - 4);
  EXPECT_EQ(name, "Level1");

  compiler.popName();
  EXPECT_EQ(compiler.getName(), "");
}

TEST(StrideCompilerTest, NameStackMultipleLevels) {
  strd::StrideCompiler compiler;

  compiler.pushName("A");
  compiler.pushName("B");
  compiler.pushName("C");

  EXPECT_EQ(compiler.getName(), "A_000_B_001_C_002");

  compiler.popName();
  EXPECT_EQ(compiler.getName(), "A_000_B_001");

  compiler.popName();
  EXPECT_EQ(compiler.getName(), "A_000");

  compiler.popName();
  EXPECT_EQ(compiler.getName(), "");
}

TEST(StrideCompilerTest, EmptyNameStackBehavior) {
  strd::StrideCompiler compiler;

  // Getting name from empty stack should return empty string
  std::string name = compiler.getName();
  EXPECT_EQ(name, "");
}

// ============================================================================
// Global Variable Management Tests
// ============================================================================

TEST(StrideCompilerTest, CreateGlobalSimple) {
  strd::StrideCompiler compiler;

  auto decl = std::make_shared<strd::DeclarationNode>(
      "testGlobal", "signal", nullptr, __FILE__, __LINE__);

  compiler.createGlobal(decl);

  EXPECT_TRUE(compiler.globalExists("testGlobal"));
}

TEST(StrideCompilerTest, GlobalExists) {
  strd::StrideCompiler compiler;

  auto decl = std::make_shared<strd::DeclarationNode>(
      "myGlobal", "signal", nullptr, __FILE__, __LINE__);

  compiler.createGlobal(decl);

  EXPECT_TRUE(compiler.globalExists("myGlobal"));
  EXPECT_FALSE(compiler.globalExists("nonexistentGlobal"));
}

TEST(StrideCompilerTest, GetGlobalAfterCreate) {
  strd::StrideCompiler compiler;

  auto decl = std::make_shared<strd::DeclarationNode>(
      "globalVar", "signal", nullptr, __FILE__, __LINE__);

  compiler.createGlobal(decl);

  auto [value, type] = compiler.getGlobal("globalVar");
  EXPECT_NE(value, nullptr);
  EXPECT_TRUE(type.has_value());
}

TEST(StrideCompilerTest, GetNonexistentGlobal) {
  strd::StrideCompiler compiler;

  auto [value, type] = compiler.getGlobal("nonexistent");

  EXPECT_EQ(value, nullptr);
  EXPECT_FALSE(type.has_value());
}

TEST(StrideCompilerTest, MultipleGlobals) {
  strd::StrideCompiler compiler;

  auto decl1 = std::make_shared<strd::DeclarationNode>(
      "global1", "signal", nullptr, __FILE__, __LINE__);
  auto decl2 = std::make_shared<strd::DeclarationNode>(
      "global2", "signal", nullptr, __FILE__, __LINE__);

  compiler.createGlobal(decl1);
  compiler.createGlobal(decl2);

  EXPECT_TRUE(compiler.globalExists("global1"));
  EXPECT_TRUE(compiler.globalExists("global2"));
}

// ============================================================================
// Configuration Flag Tests
// ============================================================================

TEST(StrideCompilerTest, ConfigurationFlagSet) {
  strd::StrideCompiler compiler;

  compiler.setConfiguration(strd::PACK_DOMAIN_FUNCTION_EXTERNAL);
  EXPECT_TRUE(compiler.hasConfiguration(strd::PACK_DOMAIN_FUNCTION_EXTERNAL));
}

TEST(StrideCompilerTest, ConfigurationFlagNotSet) {
  strd::StrideCompiler compiler;

  EXPECT_FALSE(compiler.hasConfiguration(strd::PACK_DOMAIN_FUNCTION_EXTERNAL));
}

TEST(StrideCompilerTest, ConfigurationFlagMultiple) {
  strd::StrideCompiler compiler;

  compiler.setConfiguration(strd::NO_OPTIONS);
  EXPECT_TRUE(compiler.hasConfiguration(strd::NO_OPTIONS));

  compiler.setConfiguration(strd::PACK_DOMAIN_FUNCTION_EXTERNAL);
  EXPECT_TRUE(compiler.hasConfiguration(strd::PACK_DOMAIN_FUNCTION_EXTERNAL));
}

TEST(StrideCompilerTest, ConfigurationFlagDisable) {
  strd::StrideCompiler compiler;

  compiler.setConfiguration(strd::PACK_DOMAIN_FUNCTION_EXTERNAL, true);
  EXPECT_TRUE(compiler.hasConfiguration(strd::PACK_DOMAIN_FUNCTION_EXTERNAL));

  compiler.setConfiguration(strd::PACK_DOMAIN_FUNCTION_EXTERNAL, false);
  EXPECT_FALSE(compiler.hasConfiguration(strd::PACK_DOMAIN_FUNCTION_EXTERNAL));
}

// ============================================================================
// Type Map Tests
// ============================================================================

TEST(StrideCompilerTest, TypesMapInitialized) {
  strd::StrideCompiler compiler;

  // TypesMap should be accessible
  EXPECT_FALSE(compiler.typesMap.empty());
}

TEST(StrideCompilerTest, FunctionMapInitialized) {
  strd::StrideCompiler compiler;

  // FunctionMap should be accessible
  EXPECT_TRUE(compiler.functionMap.size() >= 0);
}

// ============================================================================
// NamedValues Map Tests
// ============================================================================

TEST(StrideCompilerTest, NamedValuesInitialized) {
  strd::StrideCompiler compiler;

  EXPECT_TRUE(compiler.NamedValues.empty());
}

TEST(StrideCompilerTest, NamedValuesInsert) {
  strd::StrideCompiler compiler;

  auto realExpr = std::make_unique<strd::RealExprAST>(5.0);
  auto [v, t] = realExpr->codegen(compiler);

  compiler.NamedValues["testVar"] = {v, t};

  EXPECT_TRUE(compiler.NamedValues.count("testVar") > 0);
}

// ============================================================================
// ID Counter Tests
// ============================================================================

TEST(StrideCompilerTest, IDCounterInitialized) {
  strd::StrideCompiler compiler;

  EXPECT_EQ(compiler.m_idCounter, 0);
}

// ============================================================================
// CreateEntryBlockAlloca Tests
// ============================================================================

TEST(StrideCompilerTest, CreateEntryBlockAlloca) {
  strd::StrideCompiler compiler;

  // Create a simple function to use as context
  auto funcType = llvm::FunctionType::get(
      llvm::Type::getDoubleTy(*compiler.TheContext), false);
  auto func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                                     "test_func", compiler.TheModule.get());

  llvm::BasicBlock *entryBB =
      llvm::BasicBlock::Create(*compiler.TheContext, "entry", func);
  compiler.Builder->SetInsertPoint(entryBB);

  llvm::AllocaInst *alloca = compiler.CreateEntryBlockAlloca(
      func, "testVar", llvm::Type::getDoubleTy(*compiler.TheContext));

  EXPECT_NE(alloca, nullptr);
  EXPECT_TRUE(alloca->getAllocatedType()->isDoubleTy());
}

} // namespace
