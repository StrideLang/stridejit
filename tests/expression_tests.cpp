#include "gtest/gtest.h"

#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridejit/binaryexprast.hpp"
#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/stridejit/strideenvironment.hpp"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

#include <limits>

namespace {

// ============================================================================
// VariableExprAST Tests
// ============================================================================

TEST(VariableExprASTTest, SimpleVariableDeclaration) {
  strd::StrideCompiler state;
  auto realExpr = std::make_unique<strd::RealExprAST>(42.0);
  auto [v, t] = realExpr->codegen(state);

  state.NamedValues["test_var"] = {v, t};

  strd::VariableExprAST var("test_var");
  auto [result, resultType] = var.codegen(state);

  ASSERT_NE(result, nullptr);
  EXPECT_TRUE(resultType.value()->isDoubleTy());
}

TEST(VariableExprASTTest, VariableNotFound) {
  strd::StrideCompiler state;

  strd::VariableExprAST var("nonexistent_var");
  auto [result, resultType] = var.codegen(state);

  // When variable doesn't exist, should return nullptr
  EXPECT_EQ(result, nullptr);
}

TEST(VariableExprASTTest, MultipleVariables) {
  strd::StrideCompiler state;

  auto real1 = std::make_unique<strd::RealExprAST>(10.5);
  auto [v1, t1] = real1->codegen(state);
  state.NamedValues["var1"] = {v1, t1};

  auto real2 = std::make_unique<strd::RealExprAST>(20.5);
  auto [v2, t2] = real2->codegen(state);
  state.NamedValues["var2"] = {v2, t2};

  strd::VariableExprAST var1("var1");
  strd::VariableExprAST var2("var2");

  auto [r1, rt1] = var1.codegen(state);
  auto [r2, rt2] = var2.codegen(state);

  ASSERT_NE(r1, nullptr);
  ASSERT_NE(r2, nullptr);
  EXPECT_TRUE(rt1.value()->isDoubleTy());
  EXPECT_TRUE(rt2.value()->isDoubleTy());
}

// ============================================================================
// BinaryExprAST Operator Tests
// ============================================================================

TEST(BinaryExprASTTest, AdditionDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(3.5);
  auto rhs = std::make_unique<strd::RealExprAST>(2.5);
  strd::BinaryExprAST bin('+', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

TEST(BinaryExprASTTest, SubtractionDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(10.0);
  auto rhs = std::make_unique<strd::RealExprAST>(3.0);
  strd::BinaryExprAST bin('-', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

TEST(BinaryExprASTTest, MultiplicationDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(4.0);
  auto rhs = std::make_unique<strd::RealExprAST>(5.0);
  strd::BinaryExprAST bin('*', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

TEST(BinaryExprASTTest, DivisionDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(20.0);
  auto rhs = std::make_unique<strd::RealExprAST>(4.0);
  strd::BinaryExprAST bin('/', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

TEST(BinaryExprASTTest, LessThanDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(5.0);
  auto rhs = std::make_unique<strd::RealExprAST>(10.0);
  strd::BinaryExprAST bin('<', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(BinaryExprASTTest, GreaterThanDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(15.0);
  auto rhs = std::make_unique<strd::RealExprAST>(10.0);
  strd::BinaryExprAST bin('>', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(BinaryExprASTTest, EqualityDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(5.0);
  auto rhs = std::make_unique<strd::RealExprAST>(5.0);
  strd::BinaryExprAST bin(':', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(BinaryExprASTTest, InequalityDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(5.0);
  auto rhs = std::make_unique<strd::RealExprAST>(3.0);
  strd::BinaryExprAST bin('!', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(BinaryExprASTTest, StringAddition) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::StringExprAST>("Hello ");
  auto rhs = std::make_unique<strd::StringExprAST>("World");
  strd::BinaryExprAST bin('+', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(t.value(), state.typesMap["String"]);
}

TEST(BinaryExprASTTest, StringEquality) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::StringExprAST>("Test");
  auto rhs = std::make_unique<strd::StringExprAST>("Test");
  strd::BinaryExprAST bin(':', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(t.value(), llvm::Type::getInt1Ty(*state.TheContext));
}

TEST(BinaryExprASTTest, StringInequality) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::StringExprAST>("Test");
  auto rhs = std::make_unique<strd::StringExprAST>("Other");
  strd::BinaryExprAST bin('!', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(t.value(), llvm::Type::getInt1Ty(*state.TheContext));
}

TEST(BinaryExprASTTest, StringLessThan) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::StringExprAST>("A");
  auto rhs = std::make_unique<strd::StringExprAST>("B");
  strd::BinaryExprAST bin('<', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(t.value(), llvm::Type::getInt1Ty(*state.TheContext));
}

TEST(BinaryExprASTTest, StringGreaterThan) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::StringExprAST>("B");
  auto rhs = std::make_unique<strd::StringExprAST>("A");
  strd::BinaryExprAST bin('>', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(t.value(), llvm::Type::getInt1Ty(*state.TheContext));
}

TEST(BinaryExprASTTest, FloatLiteralAddition) {
  auto value1 = std::make_shared<strd::ValueNode>(3.0, __FILE__, __LINE__);
  auto value2 = std::make_shared<strd::ValueNode>(5.1, __FILE__, __LINE__);

  auto expr = std::make_shared<strd::ExpressionNode>(
      strd::ExpressionNode::Add, value1, value2, __FILE__, __LINE__);

  strd::StrideEnvironment strenv;

  auto left = expr->getLeft();
  auto right = expr->getRight();
  std::unique_ptr<strd::ExprAST> n1, n2;

  if (left->getNodeType() == strd::AST::Real) {
    n1 = std::make_unique<strd::RealExprAST>(
        std::static_pointer_cast<strd::ValueNode>(left)->getRealValue());
  } else if (left->getNodeType() == strd::AST::Entity) {
    n1 = std::make_unique<strd::VariableExprAST>(
        std::static_pointer_cast<strd::EntityNode>(left)->getName());
  }

  if (right->getNodeType() == strd::AST::Real) {
    n2 = std::make_unique<strd::RealExprAST>(
        std::static_pointer_cast<strd::ValueNode>(right)->getRealValue());
  } else if (right->getNodeType() == strd::AST::Entity) {
    n2 = std::make_unique<strd::VariableExprAST>(
        std::static_pointer_cast<strd::EntityNode>(right)->getName());
  }
  auto binExpr = strd::BinaryExprAST('+', std::move(n1), std::move(n2));
  auto v = binExpr.codegen(strenv.mStrideEnv);
  EXPECT_TRUE(v.second.value()->isDoubleTy());
  llvm::ConstantFP *CFP = llvm::dyn_cast<llvm::ConstantFP>(v.first);
  EXPECT_NE(CFP, nullptr);
  EXPECT_EQ(CFP->getValue().convertToDouble(), 8.1);
}

// ============================================================================
// BoolExprAST Tests
// ============================================================================

TEST(BoolExprASTTest, BoolExprFalse) {
  strd::StrideCompiler state;
  strd::BoolExprAST b(false);
  auto [v, t] = b.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(1));
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(v)) {
    EXPECT_EQ(constant->getZExtValue(), 0);
  }
}

// ============================================================================
// Integer Expression Edge Cases
// ============================================================================

TEST(IntExprASTTest, ZeroValue) {
  strd::StrideCompiler state;
  strd::IntExprAST num(0);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(32));
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(v)) {
    EXPECT_EQ(constant->getSExtValue(), 0);
  }
}

TEST(IntExprASTTest, NegativeValue) {
  strd::StrideCompiler state;
  strd::IntExprAST num(-42);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(32));
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(v)) {
    EXPECT_EQ(constant->getSExtValue(), -42);
  }
}

TEST(IntExprASTTest, MaxInt32) {
  strd::StrideCompiler state;
  strd::IntExprAST num(2147483647, 32);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(32));
}

TEST(IntExprASTTest, MinInt32) {
  strd::StrideCompiler state;
  strd::IntExprAST num(std::numeric_limits<std::int32_t>::min(), 32);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(32));
}

// ============================================================================
// Real Expression Edge Cases
// ============================================================================

TEST(RealExprASTTest, ZeroValue) {
  strd::StrideCompiler state;
  strd::RealExprAST num(0.0);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
  if (auto *constant = llvm::dyn_cast<llvm::ConstantFP>(v)) {
    EXPECT_EQ(constant->getValueAPF().convertToDouble(), 0.0);
  }
}

TEST(RealExprASTTest, NegativeValue) {
  strd::StrideCompiler state;
  strd::RealExprAST num(-3.14);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
  if (auto *constant = llvm::dyn_cast<llvm::ConstantFP>(v)) {
    EXPECT_DOUBLE_EQ(constant->getValueAPF().convertToDouble(), -3.14);
  }
}

TEST(RealExprASTTest, LargeValue) {
  strd::StrideCompiler state;
  strd::RealExprAST num(1.7976931348623157e+308);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

// ============================================================================
// Type Metadata Tests
// ============================================================================

TEST(ExprASTTest, TypecastMetadata) {
  strd::StrideCompiler state;
  strd::RealExprAST expr(5.0);
  expr.typecast = "i32";

  auto [v, t] = expr.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(expr.typecast, "i32");
}

// ============================================================================
// MemberAccessAST Tests (migrated from environment_tests.cpp)
// ============================================================================

TEST(MemberAccessASTTest, BasicConstruction) {
  strd::StrideCompiler state;

  strd::MemberAccessAST prop("Port", "size");
  auto [v, t] = prop.codegen(state);

  // Member access codegen behavior depends on implementation
  // Just verify it doesn't crash
}

// ============================================================================
// ResetExprAST Tests (migrated from environment_tests.cpp)
// ============================================================================

TEST(ResetExprASTTest, BasicConstruction) {
  strd::StrideCompiler state;

  auto condition = std::make_unique<strd::BoolExprAST>(true);
  std::vector<std::unique_ptr<strd::ExprAST>> expressions;
  expressions.push_back(std::make_unique<strd::RealExprAST>(0.0));

  strd::ResetExprAST reset("signal", std::move(condition),
                           std::move(expressions));
  // Just verify it doesn't crash
}

} // namespace
