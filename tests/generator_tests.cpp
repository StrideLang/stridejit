#include "gtest/gtest.h"

#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"
#include "stride/utils/astquery.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"

namespace {

// ============================================================================
// Low-Level AST Code Generation Tests (migrated from basic.cpp)
// ============================================================================

TEST(StrideGeneratorTest, ManualStreamAssignmentCodegen) {
  auto decl = std::make_shared<strd::DeclarationNode>("G", "signal", nullptr,
                                                      __FILE__, __LINE__);
  auto value1 = std::make_shared<strd::ValueNode>(3.3, __FILE__, __LINE__);
  auto block = std::make_shared<strd::EntityNode>("G", __FILE__, __LINE__);

  auto str =
      std::make_shared<strd::StreamNode>(value1, block, __FILE__, __LINE__);

  strd::StrideEnvironment strenv;

  strenv.mStrideEnv.NamedValues[decl->getName()] =
      strd::RealExprAST(0.0).codegen(strenv.mStrideEnv);

  auto left = str->getLeft();
  auto right = str->getRight();
  std::unique_ptr<strd::ExprAST> n1, n2;
  if (left->getNodeType() == strd::AST::Real) {
    n1 = std::make_unique<strd::RealExprAST>(
        std::static_pointer_cast<strd::ValueNode>(left)->getRealValue());
  }
  if (right->getNodeType() == strd::AST::Entity) {
    strenv.mStrideEnv
        .NamedValues[std::static_pointer_cast<strd::EntityNode>(right)
                         ->getName()] = n1->codegen(strenv.mStrideEnv);
  }

  EXPECT_TRUE(strenv.mStrideEnv.NamedValues["G"].second.value()->isDoubleTy());
  llvm::ConstantFP *CFP = llvm::dyn_cast<llvm::ConstantFP>(
      strenv.mStrideEnv.NamedValues["G"].first);
  EXPECT_NE(CFP, nullptr);
  EXPECT_EQ(CFP->getValue().convertToDouble(), 3.3);
}

TEST(StrideGeneratorTest, CreateFunctionDeclaration) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);

  auto stream = tree->getChildren()[2];
  ASSERT_EQ(stream->getNodeType(), strd::AST::Stream);

  auto addFunc = std::static_pointer_cast<strd::FunctionNode>(
      std::static_pointer_cast<strd::StreamNode>(
          std::static_pointer_cast<strd::StreamNode>(stream)->getRight())
          ->getLeft());

  strd::ScopeStack scope;
  auto funcDecl =
      strd::ASTQuery::findDeclarationByName(addFunc->getName(), scope, tree);
  EXPECT_NE(funcDecl, nullptr);
  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, addFunc, tree, &scope, strenv.mStrideEnv);
  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  EXPECT_TRUE(v->getType()->isPointerTy());
  strenv.mStrideEnv.TheModule->print(llvm::outs(), nullptr);
  llvm::outs() << "\n";
}

} // namespace
