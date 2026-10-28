/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

    Copyright (C) 2017-2026 Andres Cabrera
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

#ifndef BINARYEXPRAST_HPP
#define BINARYEXPRAST_HPP

#include <memory>
#include <string>

#include "exprast.hpp"

namespace strd {
class DeclarationNode;

/// BufferPushExprAST - Expression class for pushing a value into a buffer.
class BufferPushExprAST : public ExprAST {
  std::shared_ptr<DeclarationNode> BufferDecl;
  std::string BufferName;
  int Capacity{0};
  int Hop{0};
  std::string TriggerMode{"TriggerFull"};
  std::unique_ptr<ExprAST> ValueExpr;

public:
  BufferPushExprAST(std::shared_ptr<DeclarationNode> bufferDecl,
                    std::unique_ptr<ExprAST> valueExpr);

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;

  bool writesTo(const std::string &varName) const override;
};

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

  bool writesTo(const std::string &varName) const override;
};
} // namespace strd

#endif // BINARYEXPRAST_HPP
