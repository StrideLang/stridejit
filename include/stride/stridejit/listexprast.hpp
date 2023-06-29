#pragma once

#include "stride/stridejit/exprast.hpp"

// From stride parser
#include "stride/parser/ast.h"

class StrideCompiler;

class ListExprAST : public ExprAST {
public:
  enum class Type {
    IMMUTABLE_CONSISTENT, // Made of literals
    MUTABLE_CONSISTENT,   // Consistent types, but at least one mutable
    UNSUPPORTED
  };

  ListExprAST(std::vector<ASTNode> elements);

  llvm::Value *codegen(StrideCompiler &state) override;
  std::vector<std::unique_ptr<ExprAST>> &elements() { return members; }
  Type getType() { return mType; }

private:
  std::vector<std::unique_ptr<ExprAST>> members;
  std::vector<ASTNode> elementNodes;

  Type mType{Type::UNSUPPORTED};
};
