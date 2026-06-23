#include "stride/parser/ast.h"
#include "stride/parser/valuenode.h"
#include "stride/stridejit/binaryexprast.hpp"
#include "stride/stridejit/functionast.hpp"
#include "stride/stridejit/listexprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"
#include "gtest/gtest.h"

// llvm
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

namespace {

TEST(ASTNodeTest, NumberExprDouble) {
  strd::StrideCompiler state;
  strd::RealExprAST num(3.14);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
  if (auto *constant = llvm::dyn_cast<llvm::ConstantFP>(v)) {
    EXPECT_DOUBLE_EQ(constant->getValueAPF().convertToDouble(), 3.14);
  }
}

TEST(ASTNodeTest, NumberExprInt32) {
  strd::StrideCompiler state;
  strd::IntExprAST num((int32_t)42);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(32));
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(v)) {
    EXPECT_EQ(constant->getSExtValue(), 42);
  }
}

TEST(ASTNodeTest, NumberExprInt64) {
  strd::StrideCompiler state;
  strd::IntExprAST num((int64_t)1234567890123LL, 64);
  auto [v, t] = num.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(64));
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(v)) {
    EXPECT_EQ(constant->getSExtValue(), 1234567890123LL);
  }
}

TEST(ASTNodeTest, BoolExprTrue) {
  strd::StrideCompiler state;
  strd::BoolExprAST b(true);
  auto [v, t] = b.codegen(state);

  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isIntegerTy(1));
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(v)) {
    EXPECT_EQ(constant->getZExtValue(), 1);
  }
}

TEST(ASTNodeTest, BinaryExprAdd) {
  strd::StrideCompiler state;
  auto lhs = std::make_unique<strd::RealExprAST>(1.0);
  auto rhs = std::make_unique<strd::RealExprAST>(2.0);
  strd::BinaryExprAST bin('+', std::move(lhs), std::move(rhs));

  auto [v, t] = bin.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isDoubleTy());
  // Note: BinaryExprAST usually generates IR instructions, not just constants
  // if it's dynamic, but here it might optimize or just produce the
  // instruction.
}

TEST(ASTNodeExprTest, ListExprImmutableConsistent) {
  strd::StrideCompiler state;

  // Create actual Number nodes to put in the list
  auto n1 = std::make_shared<strd::ValueNode>(1.0, "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(2.0, "test.stride", 2);

  std::vector<strd::ASTNode> elements = {n1, n2};
  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isArrayTy());
}

} // namespace
