#ifndef NUMBEREXPRAST_HPP
#define NUMBEREXPRAST_HPP

#include "stride/stridejit/exprast.hpp"

#include <map>
#include <memory>
#include <variant>
#include <vector>

namespace strd {
class StrideCompiler;

class RealExprAST : public ExprAST {
  double Val;

public:
  RealExprAST(double Val) : Val(Val) {}
  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};

class IntExprAST : public ExprAST {
  int64_t Val;
  uint8_t NumBits{32};

public:
  IntExprAST(int64_t Val, int8_t numBits = 32) : Val(Val), NumBits(numBits) {}
  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};

class BoolExprAST : public ExprAST {
  bool Val;

public:
  BoolExprAST(int64_t Val) : Val(Val) {}
  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;
  std::string Type;
  std::vector<std::variant<size_t, std::string>> Indeces;

  enum Location {
    LOCATION_GLOBAL,
    LOCATION_STACK,
    LOCATION_HEAP,
  };

  Location varLocation = LOCATION_GLOBAL;

public:
  VariableExprAST(
      const std::string &Name,
      const std::vector<std::variant<size_t, std::string>> &Indeces =
          std::vector<std::variant<size_t, std::string>>{})
      : Name(Name), Indeces(Indeces) {}

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
  const std::string &getName() const { return Name; }
  std::vector<std::variant<size_t, std::string>> getIndeces() const;
};

class PortPropertyAST : public ExprAST {
  std::string Name;
  std::string Property;

public:
  PortPropertyAST(const std::string &Name, const std::string &Property)
      : Name(Name), Property(Property) {}

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};

class ResetExprAST : public ExprAST {
  std::string Name;
  std::unique_ptr<ExprAST> Condition;
  std::vector<std::unique_ptr<ExprAST>> Expressions;

public:
  ResetExprAST(std::string Name, std::unique_ptr<ExprAST> Condition,
               std::vector<std::unique_ptr<ExprAST>> Expressions);

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};
} // namespace strd

#endif // NUMBEREXPRAST_HPP
