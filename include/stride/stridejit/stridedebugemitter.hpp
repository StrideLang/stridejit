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

#ifndef STRIDEDEBUGEMITTER_HPP
#define STRIDEDEBUGEMITTER_HPP

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

namespace llvm {
class Value;
class Function;
class AllocaInst;
class Type;
} // namespace llvm

namespace strd {

struct ElementContext {
  enum class Kind { Generator, Slice, ArrayInstance };
  Kind kind{Kind::Generator};
  std::string varName{"Index"};
  int64_t currentIndex{0};
  int64_t totalCount{0};
  llvm::Value *currentVal{nullptr};
  bool isVector{false};
};

class StrideDebugEmitter {
public:
  explicit StrideDebugEmitter(llvm::Module &module);
  ~StrideDebugEmitter() = default;

  void initialize(const std::string &primaryRelFile = "",
                  const std::string &primaryAbsFile = "",
                  const std::string &compilationDir = "");

  llvm::DIFile *getOrCreateFile(const std::string &relFilename,
                                const std::string &absFilename);

  llvm::DISubprogram *createSubprogram(llvm::Function *fn,
                                       const std::string &name,
                                       const std::string &linkageName,
                                       const std::string &relFilename,
                                       const std::string &absFilename,
                                       int line, int scopeLine,
                                       bool isDefinition = true);

  void emitLocation(llvm::IRBuilder<> &builder, int line, int column,
                    llvm::DIScope *scope = nullptr);
  void clearLocation(llvm::IRBuilder<> &builder);

  llvm::DIType *getOrCreateBasicType(const std::string &strideTypeName);
  llvm::DIType *getOrCreateTypeForLLVMType(llvm::Type *llvmType);

  void emitLocalVariable(llvm::IRBuilder<> &builder, llvm::Value *storage,
                         const std::string &name,
                         const std::string &relFilename,
                         const std::string &absFilename, int line, int column,
                         llvm::DIType *type = nullptr,
                         llvm::DIScope *scope = nullptr);

  llvm::DIType *createStateStructType(
      const std::string &structName,
      const std::vector<std::pair<std::string, llvm::DIType *>> &fields,
      uint64_t sizeInBytes, const std::string &relFilename = "",
      const std::string &absFilename = "", int line = 1);

  // Element Context management
  void pushElementContext(const ElementContext &ctx);
  void popElementContext();
  const ElementContext *currentElementContext() const;
  void emitElementContextVariable(llvm::IRBuilder<> &builder,
                                  const std::string &relFilename,
                                  const std::string &absFilename, int line,
                                  int column);

  // Scope stack management
  void pushScope(llvm::DIScope *scope);
  void popScope();
  llvm::DIScope *currentScope() const;

  void finalize();

  llvm::DIBuilder &getDIBuilder() { return m_diBuilder; }
  llvm::DICompileUnit *getCompileUnit() { return m_compileUnit; }
  bool isInitialized() const { return m_initialized; }

private:
  llvm::Module &m_module;
  llvm::DIBuilder m_diBuilder;
  llvm::DICompileUnit *m_compileUnit{nullptr};
  llvm::DIFile *m_primaryFile{nullptr};
  std::string m_compilationDir;
  bool m_initialized{false};
  bool m_finalized{false};

  std::map<std::pair<std::string, std::string>, llvm::DIFile *> m_fileMap;
  std::vector<llvm::DIScope *> m_scopeStack;
  std::vector<ElementContext> m_elementContextStack;

  std::map<std::string, llvm::DIType *> m_basicTypes;
};

} // namespace strd

#endif // STRIDEDEBUGEMITTER_HPP
