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

#ifndef EXPRAST_HPP
#define EXPRAST_HPP

#include <map>
#include <optional>
#include <string>
#include <variant>

namespace llvm {
class Value;
class Type;
} // namespace llvm

namespace strd {
using DefaultVariant = std::variant<double, int32_t, bool>;

class StrideCompiler;

class ExprAST {
public:
  ExprAST();

  virtual ~ExprAST() = default;

  virtual std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) = 0;

  virtual bool writesTo(const std::string &varName) const { return false; }

  std::string typecast;
};
} // namespace strd

#endif // EXPRAST_HPP
