#include "gtest/gtest.h"

#include "stride/stridejit/binaryexprast.hpp"
#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

namespace {

// ============================================================================
// VariableExprAST Tests
// ============================================================================

TEST(VariableExprAST, SimpleVariableDeclaration) {
  strd::StrideCompiler state;
  auto realExpr = std::make_unique<strd::RealExprAST>(42.0);
  auto [v, t] = realExpr->codegen(state);

  state.NamedValues["test_var"] = {v, t};

  strd::VariableExprAST var("test_var");
  auto [result, resultType] = var.codegen(state);

  ASSERT_NE(result, nullptr);
  EXPECT_TRUE(resultType.value()->isDoubleTy());
}

TEST(VariableExprAST, VariableNotFound) {
  strd::StrideCompiler state;

  strd::VariableExprAST var("nonexistent_var");
  auto [result, resultType] = var.codegen(state);

  // When variable doesn't exist, should return nullptr
  EXPECT_EQ(result, nullptr);
}

TEST(VariableExprAST, MultipleVariables) {
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

TEST(BinaryExprAST, AdditionDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(3.5);
  auto rhs = std::make_unique<strd::RealExprAST>(2.5);
  strd::BinaryExprAST bin('+', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

TEST(BinaryExprAST, SubtractionDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(10.0);
  auto rhs = std::make_unique<strd::RealExprAST>(3.0);
  strd::BinaryExprAST bin('-', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

TEST(BinaryExprAST, MultiplicationDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(4.0);
  auto rhs = std::make_unique<strd::RealExprAST>(5.0);
  strd::BinaryExprAST bin('*', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

TEST(BinaryExprAST, DivisionDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(20.0);
  auto rhs = std::make_unique<strd::RealExprAST>(4.0);
  strd::BinaryExprAST bin('/', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

// TEST(BinaryExprAST, ModuloInteger) {
//   strd::StrideCompiler state;
//   auto lhs = std::make_unique<strd::IntExprAST>(10);
//   auto rhs = std::make_unique<strd::IntExprAST>(3);
//   strd::BinaryExprAST bin('%', std::move(lhs), std::move(rhs));

//   auto [v, t] = bin.codegen(state);
//   ASSERT_NE(v, nullptr);
//   EXPECT_TRUE(v->getType()->isIntegerTy(32));
// }

TEST(BinaryExprAST, LessThanDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(5.0);
  auto rhs = std::make_unique<strd::RealExprAST>(10.0);
  strd::BinaryExprAST bin('<', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  // TODO output bool
  // EXPECT_TRUE(v->getType()->isIntegerTy(1)); // Boolean result
}

TEST(BinaryExprAST, GreaterThanDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(15.0);
  auto rhs = std::make_unique<strd::RealExprAST>(10.0);
  strd::BinaryExprAST bin('>', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);

  // TODO output bool
  // EXPECT_TRUE(v->getType()->isIntegerTy(1));
}

TEST(BinaryExprAST, EqualityDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(5.0);
  auto rhs = std::make_unique<strd::RealExprAST>(5.0);
  strd::BinaryExprAST bin(':', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);

  // TODO output bool
  // EXPECT_TRUE(v->getType()->isIntegerTy(1));
}

TEST(BinaryExprAST, InequalityDouble) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(5.0);
  auto rhs = std::make_unique<strd::RealExprAST>(3.0);
  strd::BinaryExprAST bin('!', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);

  // TODO output bool
  // EXPECT_TRUE(v->getType()->isIntegerTy(1));
}

// TODO complete implementation of operators.

// TEST(BinaryExprAST, LogicalAndBool) {
//   strd::StrideCompiler state;
//   auto lhs = std::make_unique<strd::BoolExprAST>(true);
//   auto rhs = std::make_unique<strd::BoolExprAST>(true);
//   strd::BinaryExprAST bin('&', std::move(lhs), std::move(rhs));

//   auto [v, t] = bin.codegen(state);
//   ASSERT_NE(v, nullptr);
//   EXPECT_TRUE(v->getType()->isIntegerTy(1));
// }

// TEST(BinaryExprAST, LogicalOrBool) {
//   strd::StrideCompiler state;
//   auto lhs = std::make_unique<strd::BoolExprAST>(true);
//   auto rhs = std::make_unique<strd::BoolExprAST>(false);
//   strd::BinaryExprAST bin('|', std::move(lhs), std::move(rhs));

//   auto [v, t] = bin.codegen(state);
//   ASSERT_NE(v, nullptr);
//   EXPECT_TRUE(v->getType()->isIntegerTy(1));
// }

// TEST(BinaryExprAST, LessThanOrEqualDouble) {
//   strd::StrideCompiler state;
//   auto lhs = std::make_unique<strd::RealExprAST>(5.0);
//   auto rhs = std::make_unique<strd::RealExprAST>(5.0);
//   strd::BinaryExprAST bin('L', std::move(lhs), std::move(rhs));

//   auto [v, t] = bin.codegen(state);
//   ASSERT_NE(v, nullptr);
//   EXPECT_TRUE(v->getType()->isIntegerTy(1));
// }

// TEST(BinaryExprAST, GreaterThanOrEqualDouble) {
//   strd::StrideCompiler state;
//   auto lhs = std::make_unique<strd::RealExprAST>(10.0);
//   auto rhs = std::make_unique<strd::RealExprAST>(5.0);
//   strd::BinaryExprAST bin('G', std::move(lhs), std::move(rhs));

//   auto [v, t] = bin.codegen(state);
//   ASSERT_NE(v, nullptr);
//   EXPECT_TRUE(v->getType()->isIntegerTy(1));
// }

// TEST(BinaryExprAST, BitwiseAndInteger) {
//   strd::StrideCompiler state;
//   auto lhs = std::make_unique<strd::IntExprAST>(12);            // 1100
//   auto rhs = std::make_unique<strd::IntExprAST>(10);            // 1010
//   strd::BinaryExprAST bin('a', std::move(lhs), std::move(rhs)); // bitwise
//   and

//   auto [v, t] = bin.codegen(state);
//   ASSERT_NE(v, nullptr);
// }

// TEST(BinaryExprAST, BitwiseOrInteger) {
//   strd::StrideCompiler state;
//   auto lhs = std::make_unique<strd::IntExprAST>(12);            // 1100
//   auto rhs = std::make_unique<strd::IntExprAST>(10);            // 1010
//   strd::BinaryExprAST bin('o', std::move(lhs), std::move(rhs)); // bitwise or

//   auto [v, t] = bin.codegen(state);
//   ASSERT_NE(v, nullptr);
// }

// ============================================================================
// BoolExprAST Tests
// ============================================================================

TEST(ASTNodeTest, BoolExprFalse) {
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

TEST(IntExprAST, ZeroValue) {
  strd::StrideCompiler state;
  strd::IntExprAST num(0);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(32));
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(v)) {
    EXPECT_EQ(constant->getSExtValue(), 0);
  }
}

TEST(IntExprAST, NegativeValue) {
  strd::StrideCompiler state;
  strd::IntExprAST num(-42);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(32));
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(v)) {
    EXPECT_EQ(constant->getSExtValue(), -42);
  }
}

TEST(RealExprAST, ZeroValue) {
  strd::StrideCompiler state;
  strd::RealExprAST num(0.0);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
  if (auto *constant = llvm::dyn_cast<llvm::ConstantFP>(v)) {
    EXPECT_EQ(constant->getValueAPF().convertToDouble(), 0.0);
  }
}

TEST(RealExprAST, NegativeValue) {
  strd::StrideCompiler state;
  strd::RealExprAST num(-3.14);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
  if (auto *constant = llvm::dyn_cast<llvm::ConstantFP>(v)) {
    EXPECT_DOUBLE_EQ(constant->getValueAPF().convertToDouble(), -3.14);
  }
}

TEST(RealExprAST, LargeValue) {
  strd::StrideCompiler state;
  strd::RealExprAST num(1.7976931348623157e+308);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
}

TEST(IntExprAST, MaxInt32) {
  strd::StrideCompiler state;
  strd::IntExprAST num(2147483647, 32);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(32));
}

TEST(IntExprAST, MinInt32) {
  strd::StrideCompiler state;
  strd::IntExprAST num(std::numeric_limits<std::int32_t>::min(), 32);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(32));
}

// ============================================================================
// Type Metadata Tests
// ============================================================================

TEST(ExprAST, TypecastMetadata) {
  strd::StrideCompiler state;
  strd::RealExprAST expr(5.0);
  expr.typecast = "i32";

  auto [v, t] = expr.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(expr.typecast, "i32");
}

} // namespace
