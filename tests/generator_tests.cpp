#include "gtest/gtest.h"

#include "stride/parser/blocknode.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/propertynode.h"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"
#include "stride/codegen/codeanalysis.hpp"

#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

namespace {

// ============================================================================
// StrideGenerator ResolveFunctionOverload Tests
// ============================================================================

TEST(StrideGeneratorTest, ResolveFunctionOverload_EmptyDecls) {
  strd::StrideCompiler compiler;
  std::vector<std::shared_ptr<strd::DeclarationNode>> decls;
  auto func = std::make_shared<strd::FunctionNode>("test_func", nullptr, __FILE__, __LINE__);
  std::vector<llvm::Type*> argTypes;

  auto resolved = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, argTypes);
  EXPECT_EQ(resolved, nullptr);
}

TEST(StrideGeneratorTest, ResolveFunctionOverload_NonPlatformModule) {
  strd::StrideCompiler compiler;
  auto decl1 = std::make_shared<strd::DeclarationNode>("func1", "module", nullptr, __FILE__, __LINE__);
  auto decl2 = std::make_shared<strd::DeclarationNode>("func2", "module", nullptr, __FILE__, __LINE__);
  std::vector<std::shared_ptr<strd::DeclarationNode>> decls = {decl1, decl2};
  auto func = std::make_shared<strd::FunctionNode>("test_func", nullptr, __FILE__, __LINE__);
  std::vector<llvm::Type*> argTypes;

  auto resolved = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, argTypes);
  EXPECT_EQ(resolved, decl2); // Returns the last non-platform module
}

TEST(StrideGeneratorTest, ResolveFunctionOverload_PlatformModuleNoArgs) {
  strd::StrideCompiler compiler;
  auto decl = std::make_shared<strd::DeclarationNode>("func1", "platformModule", nullptr, __FILE__, __LINE__);
  std::vector<std::shared_ptr<strd::DeclarationNode>> decls = {decl};
  auto func = std::make_shared<strd::FunctionNode>("test_func", nullptr, __FILE__, __LINE__);
  std::vector<llvm::Type*> argTypes;

  auto resolved = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, argTypes);
  EXPECT_EQ(resolved, decl);
}

TEST(StrideGeneratorTest, ResolveFunctionOverload_PlatformModuleWithArgs) {
  strd::StrideCompiler compiler;
  compiler.typesMap["_IntType"] = llvm::Type::getInt32Ty(*compiler.TheContext);
  compiler.typesMap["_RealType"] = llvm::Type::getDoubleTy(*compiler.TheContext);

  auto decl1 = std::make_shared<strd::DeclarationNode>("func1", "platformModule", nullptr, __FILE__, __LINE__);
  auto inputs1 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  inputs1->addChild(std::make_shared<strd::BlockNode>("_IntType", __FILE__, __LINE__));
  decl1->addProperty(std::make_shared<strd::PropertyNode>("inputs", inputs1, __FILE__, __LINE__));
  auto outputs1 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  outputs1->addChild(std::make_shared<strd::BlockNode>("_RealType", __FILE__, __LINE__));
  decl1->addProperty(std::make_shared<strd::PropertyNode>("outputs", outputs1, __FILE__, __LINE__));
  auto decl2 = std::make_shared<strd::DeclarationNode>("func2", "platformModule", nullptr, __FILE__, __LINE__);
  auto inputs2 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  inputs2->addChild(std::make_shared<strd::BlockNode>("_RealType", __FILE__, __LINE__));
  decl2->addProperty(std::make_shared<strd::PropertyNode>("inputs", inputs2, __FILE__, __LINE__));
  auto outputs2 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  outputs2->addChild(std::make_shared<strd::BlockNode>("_IntType", __FILE__, __LINE__));
  decl2->addProperty(std::make_shared<strd::PropertyNode>("outputs", outputs2, __FILE__, __LINE__));
  std::vector<std::shared_ptr<strd::DeclarationNode>> decls = {decl1, decl2};
  auto func = std::make_shared<strd::FunctionNode>("test_func", nullptr, __FILE__, __LINE__);
  
  std::vector<llvm::Type*> argTypesInt = {llvm::Type::getInt32Ty(*compiler.TheContext)};
  
  // Test with inputs only (outputs ignored since typeTree is null)
  auto resolvedInt = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, argTypesInt);
  EXPECT_EQ(resolvedInt, decl1);

  // Test with inputs and matching outputs
  strd::CodeAnalysis::TypeTree typeTreeOutReal;
  typeTreeOutReal.output.push_back({nullptr, "_RealType"});
  auto resolvedIntOutReal = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, argTypesInt, &typeTreeOutReal);
  EXPECT_EQ(resolvedIntOutReal, decl1);

  // Test with inputs and mismatching outputs
  strd::CodeAnalysis::TypeTree typeTreeOutInt;
  typeTreeOutInt.output.push_back({nullptr, "_IntType"});
  auto resolvedIntOutInt = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, argTypesInt, &typeTreeOutInt);
  EXPECT_EQ(resolvedIntOutInt, nullptr);

  std::vector<llvm::Type*> argTypesReal = {llvm::Type::getDoubleTy(*compiler.TheContext)};
  auto resolvedReal = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, argTypesReal);
  EXPECT_EQ(resolvedReal, decl2);
}

TEST(StrideGeneratorTest, ResolveFunctionOverload_TypeTreeInference) {
  strd::StrideCompiler compiler;
  compiler.typesMap["_IntType"] = llvm::Type::getInt32Ty(*compiler.TheContext);
  compiler.typesMap["_RealType"] = llvm::Type::getDoubleTy(*compiler.TheContext);

  auto decl1 = std::make_shared<strd::DeclarationNode>("func1", "platformModule", nullptr, __FILE__, __LINE__);
  auto inputs1 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  inputs1->addChild(std::make_shared<strd::BlockNode>("_IntType", __FILE__, __LINE__));
  decl1->addProperty(std::make_shared<strd::PropertyNode>("inputs", inputs1, __FILE__, __LINE__));

  auto decl2 = std::make_shared<strd::DeclarationNode>("func2", "platformModule", nullptr, __FILE__, __LINE__);
  auto inputs2 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  inputs2->addChild(std::make_shared<strd::BlockNode>("_RealType", __FILE__, __LINE__));
  decl2->addProperty(std::make_shared<strd::PropertyNode>("inputs", inputs2, __FILE__, __LINE__));

  std::vector<std::shared_ptr<strd::DeclarationNode>> decls = {decl1, decl2};
  auto func = std::make_shared<strd::FunctionNode>("test_func", nullptr, __FILE__, __LINE__);
  
  std::vector<llvm::Type*> emptyArgTypes;

  strd::CodeAnalysis::TypeTree typeTreeInt;
  typeTreeInt.input.push_back({nullptr, "_IntType"});

  auto resolvedInt = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, emptyArgTypes, &typeTreeInt);
  EXPECT_EQ(resolvedInt, decl1);

  strd::CodeAnalysis::TypeTree typeTreeReal;
  typeTreeReal.input.push_back({nullptr, "_RealType"});

  auto resolvedReal = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, emptyArgTypes, &typeTreeReal);
  EXPECT_EQ(resolvedReal, decl2);
}

TEST(StrideGeneratorTest, ResolveFunctionOverload_TypeTreeInference_WithOutputs) {
  strd::StrideCompiler compiler;
  compiler.typesMap["_IntType"] = llvm::Type::getInt32Ty(*compiler.TheContext);
  compiler.typesMap["_RealType"] = llvm::Type::getDoubleTy(*compiler.TheContext);

  auto decl1 = std::make_shared<strd::DeclarationNode>("func1", "platformModule", nullptr, __FILE__, __LINE__);
  auto inputs1 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  inputs1->addChild(std::make_shared<strd::BlockNode>("_IntType", __FILE__, __LINE__));
  decl1->addProperty(std::make_shared<strd::PropertyNode>("inputs", inputs1, __FILE__, __LINE__));
  auto outputs1 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  outputs1->addChild(std::make_shared<strd::BlockNode>("_IntType", __FILE__, __LINE__));
  decl1->addProperty(std::make_shared<strd::PropertyNode>("outputs", outputs1, __FILE__, __LINE__));

  auto decl2 = std::make_shared<strd::DeclarationNode>("func2", "platformModule", nullptr, __FILE__, __LINE__);
  auto inputs2 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  inputs2->addChild(std::make_shared<strd::BlockNode>("_IntType", __FILE__, __LINE__));
  decl2->addProperty(std::make_shared<strd::PropertyNode>("inputs", inputs2, __FILE__, __LINE__));
  auto outputs2 = std::make_shared<strd::ListNode>(__FILE__, __LINE__);
  outputs2->addChild(std::make_shared<strd::BlockNode>("_RealType", __FILE__, __LINE__));
  decl2->addProperty(std::make_shared<strd::PropertyNode>("outputs", outputs2, __FILE__, __LINE__));

  std::vector<std::shared_ptr<strd::DeclarationNode>> decls = {decl1, decl2};
  auto func = std::make_shared<strd::FunctionNode>("test_func", nullptr, __FILE__, __LINE__);
  
  std::vector<llvm::Type*> emptyArgTypes;

  strd::CodeAnalysis::TypeTree typeTreeOutInt;
  typeTreeOutInt.input.push_back({nullptr, "_IntType"});
  typeTreeOutInt.output.push_back({nullptr, "_IntType"});

  auto resolvedOutInt = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, emptyArgTypes, &typeTreeOutInt);
  EXPECT_EQ(resolvedOutInt, decl1);

  strd::CodeAnalysis::TypeTree typeTreeOutReal;
  typeTreeOutReal.input.push_back({nullptr, "_IntType"});
  typeTreeOutReal.output.push_back({nullptr, "_RealType"});

  auto resolvedOutReal = strd::StrideGenerator::resolveFunctionOverload(decls, func, compiler, emptyArgTypes, &typeTreeOutReal);
  EXPECT_EQ(resolvedOutReal, decl2);
}

TEST(StrideGeneratorTest, ResolveFunctionOverload_FromFile) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "function_overload.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("TestDomain_process");
  EXPECT_TRUE((bool)EntrySym);
}

} // namespace
