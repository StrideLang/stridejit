#include "gtest/gtest.h"

#include "exprast.hpp"
#include "numberexprast.hpp"
#include "strideenvironment.hpp"

#include "astfunctions.h"
#include "astquery.h"
#include "strideparser.h"

TEST(Function, Simple) {

  ASTNode tree;
  tree = ASTFunctions::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_NE(tree, nullptr);

  StrideEnvironment strenv;

  auto stream = tree->getChildren()[2];

  auto addFunc = std::static_pointer_cast<FunctionNode>(
      std::static_pointer_cast<StreamNode>(
          std::static_pointer_cast<StreamNode>(stream)->getRight())
          ->getLeft());
  auto prev = std::static_pointer_cast<StreamNode>(stream)->getLeft();

  auto next = std::static_pointer_cast<StreamNode>(
                  std::static_pointer_cast<StreamNode>(stream)->getRight())
                  ->getRight();
  auto func =
      createFunctionDeclaration(addFunc, prev, next, tree, strenv.state);
  auto *v = func->codegen(strenv.state);
  EXPECT_NE(v, nullptr);

  EXPECT_TRUE(v->getType()->isPointerTy());
  //  v->dump();
}

//%Input3 = load double, double* %Input1, align 8
//    double 2.000000e+00
//    define double @AddTwo(double %Input, double %Output) {
//        entry:
//                %Output2 = alloca double, align 8
//            %Input1 = alloca double, align 8
//            store double %Input, double* %Input1, align 8
//            store double %Output, double* %Output2, align 8
//            %Input3 = load double, double* %Input1, align 8
//            %addtmp = fadd double %Input3, 2.000000e+00
//            store double %addtmp, double* %Output2, align 8
//            ret double %addtmp
//    }

TEST(Value, Assignment) {
  auto decl = std::make_shared<DeclarationNode>("G", "signal", nullptr,
                                                __FILE__, __LINE__);
  auto value1 = std::make_shared<ValueNode>(3.3, __FILE__, __LINE__);
  auto block = std::make_shared<BlockNode>("G", __FILE__, __LINE__);

  auto str = std::make_shared<StreamNode>(value1, block, __FILE__, __LINE__);

  StrideEnvironment strenv;

  strenv.state.NamedValues[decl->getName()] =
      RealExprAST(0.0).codegen(strenv.state);

  auto left = str->getLeft();
  auto right = str->getRight();
  std::unique_ptr<ExprAST> n1, n2;
  if (left->getNodeType() == AST::Real) {
    n1 = std::make_unique<RealExprAST>(
        std::static_pointer_cast<ValueNode>(left)->getRealValue());
  }
  if (right->getNodeType() == AST::Block) {
    strenv.state
        .NamedValues[std::static_pointer_cast<BlockNode>(right)->getName()] =
        n1->codegen(strenv.state);
  }
  EXPECT_TRUE(strenv.state.NamedValues["G"]->getType()->isDoubleTy());
  llvm::ConstantFP *CFP =
      llvm::dyn_cast<llvm::ConstantFP>(strenv.state.NamedValues["G"]);
  EXPECT_NE(CFP, nullptr);
  EXPECT_EQ(CFP->getValue().convertToDouble(), 3.3);
}

TEST(Expressions, FloatLiterals) {
  auto value1 = std::make_shared<ValueNode>(3.0, __FILE__, __LINE__);
  auto value2 = std::make_shared<ValueNode>(5.1, __FILE__, __LINE__);

  auto expr = std::make_shared<ExpressionNode>(ExpressionNode::Add, value1,
                                               value2, __FILE__, __LINE__);

  StrideEnvironment strenv;

  auto left = expr->getLeft();
  auto right = expr->getRight();
  std::unique_ptr<ExprAST> n1, n2;

  if (left->getNodeType() == AST::Real) {
    n1 = std::make_unique<RealExprAST>(
        std::static_pointer_cast<ValueNode>(left)->getRealValue());
  } else if (left->getNodeType() == AST::Block) {
    n1 = std::make_unique<VariableExprAST>(
        std::static_pointer_cast<BlockNode>(left)->getName());
  }

  if (right->getNodeType() == AST::Real) {
    n2 = std::make_unique<RealExprAST>(
        std::static_pointer_cast<ValueNode>(right)->getRealValue());
  } else if (right->getNodeType() == AST::Block) {
    n2 = std::make_unique<VariableExprAST>(
        std::static_pointer_cast<BlockNode>(right)->getName());
  }
  auto binExpr = BinaryExprAST('+', std::move(n1), std::move(n2));
  auto *v = binExpr.codegen(strenv.state);
  EXPECT_TRUE(v->getType()->isDoubleTy());
  llvm::ConstantFP *CFP = llvm::dyn_cast<llvm::ConstantFP>(v);
  EXPECT_NE(CFP, nullptr);
  EXPECT_EQ(CFP->getValue().convertToDouble(), 8.1);
}

TEST(Function, FunctionCall) {

  ASTNode tree;
  tree = ASTFunctions::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_NE(tree, nullptr);

  StrideEnvironment strenv;

  generateCode(tree, strenv.state);

  strenv.state.TheModule->dump();

  //  EXPECT_NE(v, nullptr);
  //  EXPECT_TRUE(v->getType()->isPointerTy());
  //  v->dump();
}
