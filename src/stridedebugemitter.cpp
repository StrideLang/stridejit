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

#include "stride/stridejit/stridedebugemitter.hpp"
#include "stride/utils/logger.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

namespace strd {

StrideDebugEmitter::StrideDebugEmitter(llvm::Module &module)
    : m_module(module), m_diBuilder(module) {}

void StrideDebugEmitter::initialize(const std::string &primaryRelFile,
                                    const std::string &primaryAbsFile,
                                    const std::string &compilationDir) {
  if (m_initialized) {
    return;
  }

  if (!compilationDir.empty()) {
    m_compilationDir = compilationDir;
  } else {
    std::error_code ec;
    m_compilationDir = std::filesystem::current_path(ec).generic_string();
  }

  m_primaryFile = getOrCreateFile(primaryRelFile, primaryAbsFile);

  // Set language to DW_LANG_C for maximum compatibility with GDB and LLDB
  m_compileUnit = m_diBuilder.createCompileUnit(
      llvm::dwarf::DW_LANG_C, m_primaryFile, "Stride Compiler", false, "", 0,
      llvm::StringRef(), llvm::DICompileUnit::DebugEmissionKind::FullDebug);

  // Add DWARF metadata flags to the module
  if (!m_module.getModuleFlag("Debug Info Version")) {
    m_module.addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                           llvm::DEBUG_METADATA_VERSION);
  }
  if (!m_module.getModuleFlag("Dwarf Version")) {
    m_module.addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);
  }

  // Pre-populate standard types
  m_basicTypes["double"] = m_diBuilder.createBasicType(
      "double", 64, llvm::dwarf::DW_ATE_float);
  m_basicTypes["float"] = m_diBuilder.createBasicType(
      "float", 32, llvm::dwarf::DW_ATE_float);
  m_basicTypes["int"] = m_diBuilder.createBasicType(
      "int", 32, llvm::dwarf::DW_ATE_signed);
  m_basicTypes["int32"] = m_basicTypes["int"];
  m_basicTypes["int64"] = m_diBuilder.createBasicType(
      "int64", 64, llvm::dwarf::DW_ATE_signed);
  m_basicTypes["bool"] = m_diBuilder.createBasicType(
      "bool", 8, llvm::dwarf::DW_ATE_boolean);

  m_initialized = true;
}

llvm::DIFile *StrideDebugEmitter::getOrCreateFile(
    const std::string &relFilename, const std::string &absFilename) {
  auto key = std::make_pair(relFilename, absFilename);
  auto it = m_fileMap.find(key);
  if (it != m_fileMap.end()) {
    return it->second;
  }

  std::string fileName;
  std::string dirName;

  if (!absFilename.empty()) {
    std::filesystem::path p(absFilename);
    fileName = p.filename().generic_string();
    dirName = p.parent_path().generic_string();
  } else if (!relFilename.empty()) {
    std::filesystem::path p(relFilename);
    fileName = p.filename().generic_string();
    dirName = p.has_parent_path() ? p.parent_path().generic_string()
                                  : m_compilationDir;
  } else {
    fileName = "source.stride";
    dirName = m_compilationDir;
  }

  llvm::DIFile *file = m_diBuilder.createFile(fileName, dirName);
  m_fileMap[key] = file;
  return file;
}

llvm::DISubprogram *StrideDebugEmitter::createSubprogram(
    llvm::Function *fn, const std::string &name,
    const std::string &linkageName, const std::string &relFilename,
    const std::string &absFilename, int line, int scopeLine,
    bool isDefinition) {
  if (!m_initialized || !fn) {
    return nullptr;
  }

  llvm::DIFile *file = getOrCreateFile(relFilename, absFilename);
  llvm::DIScope *parentScope = file;

  // Build subroutine type
  llvm::SmallVector<llvm::Metadata *, 8> paramTypes;
  if (fn->getReturnType()->isVoidTy()) {
    paramTypes.push_back(nullptr);
  } else {
    paramTypes.push_back(getOrCreateTypeForLLVMType(fn->getReturnType()));
  }

  for (auto &arg : fn->args()) {
    paramTypes.push_back(getOrCreateTypeForLLVMType(arg.getType()));
  }

  llvm::DITypeRefArray typeArray = m_diBuilder.getOrCreateTypeArray(paramTypes);
  llvm::DISubroutineType *subType = m_diBuilder.createSubroutineType(typeArray);

  llvm::DISubprogram::DISPFlags spFlags = llvm::DISubprogram::SPFlagZero;
  if (isDefinition) {
    spFlags |= llvm::DISubprogram::SPFlagDefinition;
  }
  if (fn->getLinkage() == llvm::GlobalValue::InternalLinkage ||
      fn->getLinkage() == llvm::GlobalValue::PrivateLinkage) {
    spFlags |= llvm::DISubprogram::SPFlagLocalToUnit;
  }

  int effectiveLine = line > 0 ? line : 1;
  int effectiveScopeLine = scopeLine > 0 ? scopeLine : effectiveLine;

  llvm::DISubprogram *subprogram = m_diBuilder.createFunction(
      parentScope, name, linkageName.empty() ? name : linkageName, file,
      effectiveLine, subType, effectiveScopeLine, llvm::DINode::FlagZero,
      spFlags);

  fn->setSubprogram(subprogram);
  return subprogram;
}

void StrideDebugEmitter::emitLocation(llvm::IRBuilder<> &builder, int line,
                                      int column, llvm::DIScope *scope) {
  if (!m_initialized) {
    return;
  }

  if (!scope) {
    scope = currentScope();
  }
  if (!scope) {
    return;
  }

  builder.SetCurrentDebugLocation(llvm::DILocation::get(
      m_module.getContext(), line > 0 ? line : 1, column > 0 ? column : 1,
      scope));
}

void StrideDebugEmitter::clearLocation(llvm::IRBuilder<> &builder) {
  builder.SetCurrentDebugLocation(llvm::DebugLoc());
}

llvm::DIType *StrideDebugEmitter::getOrCreateBasicType(
    const std::string &strideTypeName) {
  auto it = m_basicTypes.find(strideTypeName);
  if (it != m_basicTypes.end()) {
    return it->second;
  }

  if (strideTypeName == "_RealType" || strideTypeName == "_DoubleType") {
    return m_basicTypes["double"];
  }
  if (strideTypeName == "_FloatType") {
    return m_basicTypes["float"];
  }
  if (strideTypeName == "_IntType") {
    return m_basicTypes["int"];
  }
  if (strideTypeName == "_SwitchType") {
    return m_basicTypes["bool"];
  }

  return m_basicTypes["int"];
}

llvm::DIType *StrideDebugEmitter::getOrCreateTypeForLLVMType(
    llvm::Type *llvmType) {
  if (!llvmType) {
    return nullptr;
  }

  if (llvmType->isDoubleTy()) {
    return m_basicTypes["double"];
  }
  if (llvmType->isFloatTy()) {
    return m_basicTypes["float"];
  }
  if (llvmType->isIntegerTy(1)) {
    return m_basicTypes["bool"];
  }
  if (llvmType->isIntegerTy(32)) {
    return m_basicTypes["int"];
  }
  if (llvmType->isIntegerTy(64)) {
    return m_basicTypes["int64"];
  }
  if (llvmType->isPointerTy()) {
    return m_diBuilder.createPointerType(m_basicTypes["int"], 64);
  }
  if (llvmType->isArrayTy()) {
    auto *arrTy = llvm::cast<llvm::ArrayType>(llvmType);
    llvm::DIType *elemTy = getOrCreateTypeForLLVMType(arrTy->getElementType());
    llvm::SmallVector<llvm::Metadata *, 1> subscripts;
    subscripts.push_back(
        m_diBuilder.getOrCreateSubrange(0, arrTy->getNumElements()));
    return m_diBuilder.createArrayType(
        arrTy->getNumElements() * elemTy->getSizeInBits(), 0, elemTy,
        m_diBuilder.getOrCreateArray(subscripts));
  }

  return m_basicTypes["int"];
}

void StrideDebugEmitter::emitLocalVariable(
    llvm::IRBuilder<> &builder, llvm::Value *storage, const std::string &name,
    const std::string &relFilename, const std::string &absFilename, int line,
    int column, llvm::DIType *type, llvm::DIScope *scope) {
  if (!m_initialized || !storage) {
    return;
  }

  if (!scope) {
    scope = currentScope();
  }
  if (!scope) {
    return;
  }

  llvm::DIFile *file = getOrCreateFile(relFilename, absFilename);
  if (!type) {
    type = getOrCreateTypeForLLVMType(storage->getType());
  }
  if (!type) {
    type = getOrCreateBasicType("int");
  }

  int effectiveLine = line > 0 ? line : 1;
  int effectiveCol = column > 0 ? column : 1;

  llvm::DILocalVariable *var = m_diBuilder.createAutoVariable(
      scope, name, file, effectiveLine, type, true);

  llvm::DILocation *dl = llvm::DILocation::get(
      m_module.getContext(), effectiveLine, effectiveCol, scope);

  if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(storage)) {
    llvm::BasicBlock *bb = alloca->getParent();
    auto nextIt = std::next(alloca->getIterator());
    if (nextIt != bb->end()) {
      m_diBuilder.insertDeclare(alloca, var, m_diBuilder.createExpression(), dl,
                                &*nextIt);
    } else {
      m_diBuilder.insertDeclare(alloca, var, m_diBuilder.createExpression(), dl,
                                bb);
    }
  } else {
    if (auto *insertBlock = builder.GetInsertBlock()) {
      auto insertPoint = builder.GetInsertPoint();
      if (insertPoint != insertBlock->end()) {
        m_diBuilder.insertDeclare(storage, var, m_diBuilder.createExpression(),
                                  dl, &*insertPoint);
      } else {
        m_diBuilder.insertDeclare(storage, var, m_diBuilder.createExpression(),
                                  dl, insertBlock);
      }
    }
  }
}

llvm::DIType *StrideDebugEmitter::createStateStructType(
    const std::string &structName,
    const std::vector<std::pair<std::string, llvm::DIType *>> &fields,
    uint64_t sizeInBytes, const std::string &relFilename,
    const std::string &absFilename, int line) {
  if (!m_initialized) {
    return nullptr;
  }

  llvm::DIFile *file = getOrCreateFile(relFilename, absFilename);
  llvm::SmallVector<llvm::Metadata *, 16> elements;
  uint64_t offsetInBits = 0;

  for (const auto &[fieldName, fieldType] : fields) {
    llvm::DIType *fType = fieldType ? fieldType : m_basicTypes["int"];
    uint64_t fieldSizeInBits = fType->getSizeInBits();
    elements.push_back(m_diBuilder.createMemberType(
        file, fieldName, file, line > 0 ? line : 1, fieldSizeInBits, 0,
        offsetInBits, llvm::DINode::FlagZero, fType));
    offsetInBits += fieldSizeInBits;
  }

  llvm::DINodeArray elementArray = m_diBuilder.getOrCreateArray(elements);
  return m_diBuilder.createStructType(
      m_compileUnit, structName, file, line > 0 ? line : 1,
      sizeInBytes > 0 ? (sizeInBytes * 8) : offsetInBits, 0,
      llvm::DINode::FlagZero, nullptr, elementArray);
}

void StrideDebugEmitter::pushElementContext(const ElementContext &ctx) {
  m_elementContextStack.push_back(ctx);
}

void StrideDebugEmitter::popElementContext() {
  if (!m_elementContextStack.empty()) {
    m_elementContextStack.pop_back();
  }
}

const ElementContext *StrideDebugEmitter::currentElementContext() const {
  if (m_elementContextStack.empty()) {
    return nullptr;
  }
  return &m_elementContextStack.back();
}

void StrideDebugEmitter::emitElementContextVariable(
    llvm::IRBuilder<> &builder, const std::string &relFilename,
    const std::string &absFilename, int line, int column) {
  const ElementContext *ctx = currentElementContext();
  if (!ctx || !m_initialized) {
    return;
  }

  llvm::DIScope *scope = currentScope();
  if (!scope) {
    return;
  }

  llvm::Function *fn = builder.GetInsertBlock()
                           ? builder.GetInsertBlock()->getParent()
                           : nullptr;
  if (!fn) {
    return;
  }

  // Create an alloca in the entry block for the loop index / element variable
  llvm::IRBuilder<> entryBuilder(&fn->getEntryBlock(),
                                 fn->getEntryBlock().begin());
  llvm::AllocaInst *alloca =
      entryBuilder.CreateAlloca(builder.getInt32Ty(), nullptr, ctx->varName);

  // Store the current index value at the current builder position
  llvm::Value *val = ctx->currentVal;
  if (!val) {
    val = builder.getInt32(static_cast<int32_t>(ctx->currentIndex));
  }
  builder.CreateStore(val, alloca);

  // Emit debug info for the index variable
  emitLocalVariable(builder, alloca, ctx->varName, relFilename, absFilename,
                    line, column, m_basicTypes["int"], scope);
}

void StrideDebugEmitter::pushScope(llvm::DIScope *scope) {
  if (scope) {
    m_scopeStack.push_back(scope);
  }
}

void StrideDebugEmitter::popScope() {
  if (!m_scopeStack.empty()) {
    m_scopeStack.pop_back();
  }
}

llvm::DIScope *StrideDebugEmitter::currentScope() const {
  if (m_scopeStack.empty()) {
    return m_compileUnit ? static_cast<llvm::DIScope *>(m_compileUnit)
                         : nullptr;
  }
  return m_scopeStack.back();
}

void StrideDebugEmitter::finalize() {
  if (m_initialized && !m_finalized) {
    m_diBuilder.finalize();
    m_finalized = true;
  }
}

} // namespace strd
