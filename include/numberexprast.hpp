#ifndef NUMBEREXPRAST_HPP
#define NUMBEREXPRAST_HPP

#include "exprast.hpp"

#include <map>

class StrideCompiler;

class RealExprAST : public ExprAST {
  double Val;

public:
  RealExprAST(double Val) : Val(Val) {}
  llvm::Value *codegen(StrideCompiler &state) override;
};

class IntExprAST : public ExprAST {
  int64_t Val;

public:
  IntExprAST(int64_t Val) : Val(Val) {}
  llvm::Value *codegen(StrideCompiler &state) override;
};

class BoolExprAST : public ExprAST {
  bool Val;

public:
  BoolExprAST(int64_t Val) : Val(Val) {}
  llvm::Value *codegen(StrideCompiler &state) override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;
  std::string Type;
  std::vector<size_t> Indeces;

public:
  VariableExprAST(const std::string &Name,
                  const std::vector<size_t> &&Indeces = std::vector<size_t>{})
      : Name(Name), Indeces(Indeces) {}

  llvm::Value *codegen(StrideCompiler &state) override;
  const std::string &getName() const { return Name; }
  std::vector<size_t> getIndeces() const;
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS);

  llvm::Value *codegen(StrideCompiler &state) override;
};

class ListExprAST : public ExprAST {
  std::vector<std::unique_ptr<ExprAST>> members;

public:
  ListExprAST() {}

  void addElement(std::unique_ptr<ExprAST> elem);
  llvm::Value *codegen(StrideCompiler &state) override;
  std::vector<std::unique_ptr<ExprAST>> &elements() { return members; }
};

#endif // NUMBEREXPRAST_HPP
