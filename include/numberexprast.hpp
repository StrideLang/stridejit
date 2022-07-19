#ifndef NUMBEREXPRAST_HPP
#define NUMBEREXPRAST_HPP

#include "exprast.hpp"

#include <map>

class JitState;

class RealExprAST : public ExprAST {
  double Val;

public:
  RealExprAST(double Val) : Val(Val) {}
  llvm::Value *codegen(JitState &state) override;
};

class IntExprAST : public ExprAST {
  int64_t Val;

public:
  IntExprAST(int64_t Val) : Val(Val) {}
  llvm::Value *codegen(JitState &state) override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(const std::string &Name) : Name(Name) {}

  llvm::Value *codegen(JitState &state) override;
  const std::string &getName() const { return Name; }
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS);

  llvm::Value *codegen(JitState &state) override;
};

#endif // NUMBEREXPRAST_HPP
