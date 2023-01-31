#include "listexprast.hpp"

#include "strideenvironment.hpp"

#include <astfunctions.h>

ListExprAST::ListExprAST(std::vector<ASTNode> elements)
    : elementNodes(elements) {
  bool consistent = true;
  bool isMutable = false;
  std::string previousType = "";
  for (const auto &e : elementNodes) {
    members.push_back(createExpr(e));
    if (e->getNodeType() != AST::Int && e->getNodeType() != AST::Real) {
      isMutable = true;
    }
    if ((previousType != "") &&
        previousType != CodeQuery::resolveNodeOutDataType(e, {}, nullptr)) {
      consistent = false;
    }
  }
  if (!isMutable && consistent) {
    mType = Type::IMMUTABLE_CONSISTENT;
  } else if (isMutable && consistent) {
    mType = Type::MUTABLE_CONSISTENT;
  }
}

llvm::Value *ListExprAST::codegen(StrideCompiler &state) {
  // All elements are constant
  std::vector<double> values;

  for (const auto &elem : elementNodes) {
    if (elem->getNodeType() == AST::Int || elem->getNodeType() == AST::Real) {
      // Literal number list.
      double val = std::static_pointer_cast<ValueNode>(elem)->toReal();
      values.push_back(val);

    } else {
      // Non-literal list
      return llvm::ConstantTokenNone::get(*state.TheContext);
    }
  }
  return llvm::ConstantDataArray::get(*state.TheContext, values);
}
