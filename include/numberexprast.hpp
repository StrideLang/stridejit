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
  uint8_t NumBits{32};

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

class PortPropertyAST : public ExprAST {
  std::string Name;
  std::string Property;

public:
  PortPropertyAST(const std::string &Name, const std::string &Property)
      : Name(Name), Property(Property) {}

  llvm::Value *codegen(StrideCompiler &state) override;
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

#endif // NUMBEREXPRAST_HPP
