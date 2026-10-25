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

#ifndef EXPRAST_HPP
#define EXPRAST_HPP

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace llvm {
class Value;
class Type;
} // namespace llvm

namespace strd {
// int64_t holds integer defaults that don't fit in 32 bits
using DefaultVariant =
    std::variant<double, int32_t, bool, std::string, int64_t>;

class AST;
class StrideCompiler;

class ExprAST {
public:
  ExprAST();

  virtual ~ExprAST() = default;

  virtual std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) = 0;

  virtual bool writesTo(const std::string &varName) const { return false; }

  int m_line{-1};
  int m_column{-1};
  std::string m_relativeFilename;
  std::string m_absoluteFilename;

  void setLocation(int line, int column = -1,
                   const std::string &relFile = "",
                   const std::string &absFile = "") {
    m_line = line;
    m_column = column;
    m_relativeFilename = relFile;
    m_absoluteFilename = absFile;
  }

  void setLocationFrom(const std::shared_ptr<AST> &node);

  int getLine() const { return m_line; }
  int getColumn() const { return m_column; }
  const std::string &getRelativeFilename() const { return m_relativeFilename; }
  const std::string &getAbsoluteFilename() const { return m_absoluteFilename; }

  // Data type to convert the value to, e.g. "Real"
  std::string typecast;
  // Data type of the value, e.g. "UInt8". Empty if unknown, in which case
  // integers are treated as signed.
  std::string dataType;
};

class StringExprAST : public ExprAST {
public:
  explicit StringExprAST(std::string val) : Val(std::move(val)) {}

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;

  const std::string &getValue() const { return Val; }

private:
  std::string Val;
};
} // namespace strd

#endif // EXPRAST_HPP
