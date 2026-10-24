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

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
class Type;
}

namespace strd {

// A symbol registered with the NameMangler: the Stride name, the namespace
// and scope it was declared in (outermost first) and, for polymorphic
// callables, the LLVM types that select this variant.
struct MangledSymbol {
  std::string name;
  std::vector<std::string> scope;
  std::vector<llvm::Type *> types;
  std::string mangled;
};

// Builds unique symbol names for generated code and keeps a map to resolve
// them back to their Stride name, scope and type signature.
//
// Encoding: a symbol with no scope and no types keeps its plain name, so
// existing non-polymorphic symbols are unchanged. Otherwise:
//   _S <len><scope>... <len><name> [ _T <type codes> E ]
// e.g. Abs taking and returning double -> "_S3Abs_TddE". Components are
// length-prefixed so names containing '_' cannot collide.
class NameMangler {
public:
  // Returns the mangled name and records it. Mangling the same symbol twice
  // returns the same name.
  std::string mangle(const std::string &name,
                     const std::vector<std::string> &scope = {},
                     const std::vector<llvm::Type *> &types = {});

  // Mangled name of a symbol if it has been registered.
  std::optional<std::string>
  lookup(const std::string &name, const std::vector<std::string> &scope = {},
         const std::vector<llvm::Type *> &types = {}) const;

  // The symbol a mangled name refers to, or nullptr if unknown.
  const MangledSymbol *resolve(const std::string &mangled) const;

  // All registered variants of name in scope, in registration order.
  std::vector<const MangledSymbol *>
  overloads(const std::string &name,
            const std::vector<std::string> &scope = {}) const;

  // Resolves a plain (unmangled) name to a generated symbol when it is
  // unambiguous: the plain name itself if registered as such, or the single
  // registered variant.
  std::optional<std::string>
  resolveUnique(const std::string &name,
                const std::vector<std::string> &scope = {}) const;

  void clear();

  // Short code for a type used in mangled names (e.g. i32 -> "i").
  static std::string typeCode(llvm::Type *type);

  static std::string encode(const std::string &name,
                            const std::vector<std::string> &scope,
                            const std::vector<llvm::Type *> &types);

private:
  // mangled name -> symbol
  std::map<std::string, MangledSymbol> m_symbols;
  // encoded (scope, name) -> mangled names in registration order
  std::map<std::string, std::vector<std::string>> m_overloads;
};

} // namespace strd
