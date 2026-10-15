/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

    Author: Andres Cabrera
*/

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
  bool Signed{true};

public:
  IntExprAST(int64_t Val, int8_t numBits = 32, bool Signed = true)
      : Val(Val), NumBits(numBits), Signed(Signed) {}
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
  std::vector<std::variant<size_t, std::string>> getIndices() const;
};

class MemberAccessAST : public ExprAST {
  std::string Name;
  std::string Property;

public:
  MemberAccessAST(const std::string &Name, const std::string &Property)
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

  bool writesTo(const std::string &varName) const override;
};
} // namespace strd

#endif // NUMBEREXPRAST_HPP
