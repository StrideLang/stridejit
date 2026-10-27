#include "gtest/gtest.h"

#include "stride/stridejit/listexprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include "stride/parser/generatorrangenode.h"
#include "stride/parser/valuenode.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"

namespace {

// ============================================================================
// ListExprAST Type Detection Tests
// ============================================================================

TEST(ListExprASTTest, EmptyList) {
  strd::StrideCompiler state;

  std::vector<strd::ASTNode> elements;
  strd::ListExprAST list(elements);

  auto [v, t] = list.codegen(state);
  // Empty list behavior depends on implementation
  EXPECT_TRUE(list.getType() == strd::ListExprAST::Type::IMMUTABLE_CONSISTENT ||
              list.getType() == strd::ListExprAST::Type::UNSUPPORTED);
}

TEST(ListExprASTTest, ImmutableConsistentIntList) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(static_cast<int64_t>(1),
                                              "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(static_cast<int64_t>(2),
                                              "test.stride", 2);
  auto n3 = std::make_shared<strd::ValueNode>(static_cast<int64_t>(3),
                                              "test.stride", 3);

  std::vector<strd::ASTNode> elements = {n1, n2, n3};
  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(ListExprASTTest, ImmutableConsistentDoubleList) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(1.5, "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(2.5, "test.stride", 2);

  std::vector<strd::ASTNode> elements = {n1, n2};
  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isArrayTy());
}

TEST(ListExprASTTest, SingleElementList) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(42.0, "test.stride", 1);

  std::vector<strd::ASTNode> elements = {n1};
  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(ListExprASTTest, LargeImmutableList) {
  strd::StrideCompiler state;

  std::vector<strd::ASTNode> elements;
  for (int i = 0; i < 100; i++) {
    elements.push_back(std::make_shared<strd::ValueNode>(static_cast<double>(i),
                                                         "test.stride", i));
  }

  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isArrayTy());
  if (auto *arrTy = llvm::dyn_cast<llvm::ArrayType>(v->getType())) {
    EXPECT_EQ(arrTy->getNumElements(), 100);
  }
}

// ============================================================================
// ListExprAST Element Access Tests
// ============================================================================

TEST(ListExprASTTest, ElementsAccessor) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(1.0, "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(2.0, "test.stride", 2);

  std::vector<strd::ASTNode> elements = {n1, n2};
  strd::ListExprAST list(elements);

  auto &members = list.elements();
  EXPECT_EQ(members.size(), 2);
}

// ============================================================================
// ListExprAST with Mixed Types Tests
// ============================================================================

TEST(ListExprASTTest, MixedIntDoubleList) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(static_cast<int64_t>(1),
                                              "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(2.5, "test.stride", 2);

  std::vector<strd::ASTNode> elements = {n1, n2};
  strd::ListExprAST list(elements);

  // Mixed types should be marked as unsupported or handled appropriately
  auto type = list.getType();
  EXPECT_TRUE(type == strd::ListExprAST::Type::UNSUPPORTED ||
              type == strd::ListExprAST::Type::MUTABLE_CONSISTENT);
}

TEST(ListExprASTTest, MixedIntDoubleListWithTypecast) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(static_cast<int64_t>(1),
                                              "test.stride", 1);
  n1->setCompilerProperty(
      "typecast",
      std::make_shared<strd::ValueNode>("Real", "test.stride", 1));
  auto n2 = std::make_shared<strd::ValueNode>(2.5, "test.stride", 2);

  std::vector<strd::ASTNode> elements = {n1, n2};
  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->getType()->isArrayTy());
}


// ============================================================================
// ListExprAST Consistency Tests
// ============================================================================

TEST(ListExprASTTest, TypeConsistencyAllDoubles) {
  strd::StrideCompiler state;

  std::vector<strd::ASTNode> elements;
  for (int i = 0; i < 5; i++) {
    elements.push_back(std::make_shared<strd::ValueNode>(
        static_cast<double>(i) + 0.5, "test.stride", i));
  }

  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);
}

TEST(ListExprASTTest, TypeConsistencyAllIntegers) {
  strd::StrideCompiler state;

  std::vector<strd::ASTNode> elements;
  for (int64_t i = 0; i < 5; i++) {
    elements.push_back(std::make_shared<strd::ValueNode>(i, "test.stride", i));
  }

  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);
}

// ============================================================================
// ListExprAST Codegen Type Results
// ============================================================================

TEST(ListExprASTTest, CodegenReturnsArray) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(1.0, "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(2.0, "test.stride", 2);
  auto n3 = std::make_shared<strd::ValueNode>(3.0, "test.stride", 3);

  std::vector<strd::ASTNode> elements = {n1, n2, n3};
  strd::ListExprAST list(elements);

  auto [v, t] = list.codegen(state);

  ASSERT_NE(v, nullptr);
  // ASSERT_NE(t, ); // TODO verify type
  EXPECT_TRUE(v->getType()->isArrayTy());
}

// ============================================================================
// ListExprAST Edge Cases
// ============================================================================

TEST(ListExprASTTest, DuplicateValues) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(5.0, "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(5.0, "test.stride", 2);
  auto n3 = std::make_shared<strd::ValueNode>(5.0, "test.stride", 3);

  std::vector<strd::ASTNode> elements = {n1, n2, n3};
  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(ListExprASTTest, VeryLargeValues) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(1e100, "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(2e100, "test.stride", 2);

  std::vector<strd::ASTNode> elements = {n1, n2};
  strd::ListExprAST list(elements);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(ListExprASTTest, VerySmallValues) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(1e-100, "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(2e-100, "test.stride", 2);

  std::vector<strd::ASTNode> elements = {n1, n2};
  strd::ListExprAST list(elements);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(ListExprASTTest, NegativeValues) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(-1.5, "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(-2.5, "test.stride", 2);
  auto n3 = std::make_shared<strd::ValueNode>(-3.5, "test.stride", 3);

  std::vector<strd::ASTNode> elements = {n1, n2, n3};
  strd::ListExprAST list(elements);

  EXPECT_EQ(list.getType(), strd::ListExprAST::Type::IMMUTABLE_CONSISTENT);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(ListExprASTTest, ZeroValues) {
  strd::StrideCompiler state;

  auto n1 = std::make_shared<strd::ValueNode>(0.0, "test.stride", 1);
  auto n2 = std::make_shared<strd::ValueNode>(0.0, "test.stride", 2);

  std::vector<strd::ASTNode> elements = {n1, n2};
  strd::ListExprAST list(elements);

  auto [v, t] = list.codegen(state);
  ASSERT_NE(v, nullptr);
}

TEST(GeneratorRangeNodeTest, BasicConstruction) {
  auto start = std::make_shared<strd::ValueNode>(static_cast<int64_t>(0), "test.stride", 1);
  auto end = std::make_shared<strd::ValueNode>(static_cast<int64_t>(8), "test.stride", 1);
  auto gen = std::make_shared<strd::GeneratorRangeNode>(start, end, "test.stride", 1);

  EXPECT_EQ(gen->getNodeType(), strd::AST::GeneratorRange);
  EXPECT_EQ(gen->startIndex(), start);
  EXPECT_EQ(gen->endIndex(), end);
  EXPECT_EQ(gen->stepIndex(), nullptr);
}

TEST(GeneratorRangeNodeTest, SteppedConstruction) {
  auto start = std::make_shared<strd::ValueNode>(static_cast<int64_t>(0), "test.stride", 1);
  auto step = std::make_shared<strd::ValueNode>(static_cast<int64_t>(2), "test.stride", 1);
  auto end = std::make_shared<strd::ValueNode>(static_cast<int64_t>(10), "test.stride", 1);
  auto gen = std::make_shared<strd::GeneratorRangeNode>(start, step, end, "test.stride", 1);

  EXPECT_EQ(gen->getNodeType(), strd::AST::GeneratorRange);
  EXPECT_EQ(gen->startIndex(), start);
  EXPECT_EQ(gen->stepIndex(), step);
  EXPECT_EQ(gen->endIndex(), end);
}

} // namespace
