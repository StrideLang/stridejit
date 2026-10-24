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

#include "stride/stridejit/namemangler.hpp"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"

using namespace strd;

namespace {

std::string lengthPrefixed(const std::string &component) {
  return std::to_string(component.size()) + component;
}

// Key for the overload map. Only the scope and name, so all type variants of
// a symbol share it.
std::string overloadKey(const std::string &name,
                        const std::vector<std::string> &scope) {
  std::string key;
  for (const auto &s : scope) {
    key += lengthPrefixed(s);
  }
  return key + lengthPrefixed(name);
}

} // namespace

std::string NameMangler::typeCode(llvm::Type *type) {
  if (!type) {
    return "u";
  }
  if (type->isVoidTy()) {
    return "v";
  }
  if (type->isIntegerTy()) {
    switch (type->getIntegerBitWidth()) {
    case 1:
      return "b";
    case 8:
      return "c";
    case 16:
      return "s";
    case 32:
      return "i";
    case 64:
      return "l";
    default:
      return "I" + std::to_string(type->getIntegerBitWidth()) + "_";
    }
  }
  if (type->isFloatTy()) {
    return "f";
  }
  if (type->isDoubleTy()) {
    return "d";
  }
  if (type->isPointerTy()) {
    return "p";
  }
  if (type->isArrayTy()) {
    return "A" + std::to_string(type->getArrayNumElements()) + "_" +
           typeCode(type->getArrayElementType());
  }
  if (auto *structTy = llvm::dyn_cast<llvm::StructType>(type)) {
    if (structTy->hasName()) {
      return "S" + lengthPrefixed(structTy->getName().str());
    }
    std::string code = "S";
    for (auto *elem : structTy->elements()) {
      code += typeCode(elem);
    }
    return code + "E";
  }
  return "u";
}

std::string NameMangler::encode(const std::string &name,
                                const std::vector<std::string> &scope,
                                const std::vector<llvm::Type *> &types) {
  if (scope.empty() && types.empty()) {
    return name;
  }
  std::string mangled = "_S" + overloadKey(name, scope);
  if (!types.empty()) {
    mangled += "_T";
    for (auto *t : types) {
      mangled += typeCode(t);
    }
    mangled += "E";
  }
  return mangled;
}

std::string NameMangler::mangle(const std::string &name,
                                const std::vector<std::string> &scope,
                                const std::vector<llvm::Type *> &types) {
  std::string mangled = encode(name, scope, types);
  if (m_symbols.find(mangled) == m_symbols.end()) {
    m_symbols[mangled] = MangledSymbol{name, scope, types, mangled};
    m_overloads[overloadKey(name, scope)].push_back(mangled);
  }
  return mangled;
}

std::optional<std::string>
NameMangler::lookup(const std::string &name,
                    const std::vector<std::string> &scope,
                    const std::vector<llvm::Type *> &types) const {
  std::string mangled = encode(name, scope, types);
  if (m_symbols.find(mangled) == m_symbols.end()) {
    return std::nullopt;
  }
  return mangled;
}

const MangledSymbol *NameMangler::resolve(const std::string &mangled) const {
  auto it = m_symbols.find(mangled);
  return it == m_symbols.end() ? nullptr : &it->second;
}

std::vector<const MangledSymbol *>
NameMangler::overloads(const std::string &name,
                       const std::vector<std::string> &scope) const {
  std::vector<const MangledSymbol *> result;
  auto it = m_overloads.find(overloadKey(name, scope));
  if (it != m_overloads.end()) {
    for (const auto &mangled : it->second) {
      result.push_back(&m_symbols.at(mangled));
    }
  }
  return result;
}

std::optional<std::string>
NameMangler::resolveUnique(const std::string &name,
                           const std::vector<std::string> &scope) const {
  auto variants = overloads(name, scope);
  if (variants.size() == 1) {
    return variants.front()->mangled;
  }
  return std::nullopt;
}

void NameMangler::clear() {
  m_symbols.clear();
  m_overloads.clear();
}
