#ifndef BINARYEXPRAST_HPP
#define BINARYEXPRAST_HPP

#include <memory>

#include "exprast.hpp"

namespace strd {
/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;
  // std::optional<llvm::Type *> LHT, RHT;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS);

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};
} // namespace strd

#endif // BINARYEXPRAST_HPP
