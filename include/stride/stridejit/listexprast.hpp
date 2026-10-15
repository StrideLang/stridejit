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

#pragma once

#include "stride/stridejit/exprast.hpp"

// From stride parser
#include "stride/parser/ast.h"

namespace strd {
class StrideCompiler;

class ListExprAST : public ExprAST {
public:
  enum class Type {
    IMMUTABLE_CONSISTENT, // Made of literals
    MUTABLE_CONSISTENT,   // Consistent types, but at least one mutable
    UNSUPPORTED
  };

  ListExprAST(std::vector<strd::ASTNode> elements);

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
  std::vector<std::unique_ptr<ExprAST>> &elements() { return members; }
  Type getType() { return mType; }

private:
  std::vector<std::unique_ptr<ExprAST>> members;
  std::vector<strd::ASTNode> elementNodes;

  Type mType{Type::UNSUPPORTED};
};
} // namespace strd
