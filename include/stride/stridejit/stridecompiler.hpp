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

#ifndef STRIDECOMPILER_HPP
#define STRIDECOMPILER_HPP

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "stride/stridegen/codeanalysis.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/stridejit/functionast.hpp"
#include "stride/stridejit/namemangler.hpp"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "stride/stridejit/stridedebugemitter.hpp"

// forward declarations for llvm
namespace llvm {
class Value;
class Constant;
class Module;

} // namespace llvm

#if defined(_MSC_VER)
//  Microsoft
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
//  GCC
#define EXPORT __attribute__((visibility("default")))
#define IMPORT
#else
//  do nothing and hope for the best?
#define EXPORT
#define IMPORT
#pragma warning Unknown dynamic link import / export semantics.
#endif

#include "stride/stridejit/strideruntime_string.h"

extern "C" {
EXPORT double __stride_Greater_d_dd(double a, double b);
}

namespace strd {
enum class DataType {
  DOUBLE,
  BOOL,
  INT32,
  INT64,
  STRING,
  CUSTOM,
  STATE,
  FLOAT32,
  INT8,
  INT16
};

struct ExternalFunction {
  std::string name;
  llvm::FunctionType *llvmFunctionType{nullptr};
  bool isConstrainedList{false};
  bool isHeterogeneousVariadic{false};
  llvm::Type *elemType{nullptr};
  std::vector<llvm::Type *> prefixTypes;
  std::vector<llvm::Type *> variadicTypes;
  int minItems{0};
  int maxItems{-1};
};

struct DomainArg {
  std::string name;
  DataType type;
};

enum StrideConfig {
  NO_OPTIONS = 0x0,
  PACK_DOMAIN_FUNCTION_EXTERNAL = 0x1,
};

class StrideCompiler {
public:
  StrideCompiler();
  ~StrideCompiler();

  std::optional<ExternalFunction>
  getExternalFunction(std::string strideName, llvm::Type *returnType,
                      std::vector<llvm::Type *> argTypes,
                      bool allowConversion = false);

  llvm::Type *getLLVMType(std::shared_ptr<strd::DeclarationNode> decl);

  // Reads the frameworkDataType declarations in tree into typesMap and
  // dataTypes, so each framework decides e.g. the size of Int and Real.
  void registerDataTypes(ASTNode tree);
  // The LLVM type for a data type: integers by bits, Float32 and Float64,
  // Switch is i1 and String is the {ptr, i32} string view.
  llvm::Type *getLLVMTypeForDataType(const DataTypes::Info &info);
  // The description of a data type name. Not valid() for unknown names.
  DataTypes::Info getDataTypeInfo(const std::string &name);
  // Name of the data type of decl, e.g. "Int" for signal X { type: Int }.
  // Empty if unknown.
  static std::string
  getDataTypeName(std::shared_ptr<strd::DeclarationNode> decl);
  // Converts V to toType. Integers are extended, truncated and converted to
  // floats according to their signedness. Values of other types are
  // returned unchanged.
  llvm::Value *convertValue(llvm::Value *V, llvm::Type *toType,
                            bool fromSigned = true, bool toSigned = true);
  // Converts V, whose data type is fromDataType (may be empty), to the data
  // type named toDataType.
  llvm::Value *convertToDataType(llvm::Value *V,
                                 const std::string &fromDataType,
                                 const std::string &toDataType);
  llvm::Type *
  getLLVMTypeForCodegenEntity(std::shared_ptr<strd::DeclarationNode> decl,
                              std::shared_ptr<DeclarationNode> funcDecl,
                              std::shared_ptr<FunctionNode> functionInstance);

  llvm::Function *getFunctionInModule(std::string Name);

  llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction,
                                           llvm::StringRef VarName,
                                           llvm::Type *dataType);
  llvm::AllocaInst *CreateEntryBlockAllocaArray(llvm::Function *TheFunction,
                                                llvm::StringRef VarName,
                                                llvm::Type *dataType,
                                                size_t size);
  llvm::AllocaInst *
  CreateEntryBlockAllocaArrayConst(llvm::Function *TheFunction,
                                   llvm::StringRef VarName,
                                   llvm::Type *dataType, size_t size);

  // Globals in current name prefix
  void createGlobal(std::shared_ptr<DeclarationNode> globalDecl);
  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  getGlobal(std::string globalName);
  bool globalExists(std::string globalName);

  llvm::Type *getElementType(llvm::Value *V);

  // Name prefix. Used to name instances and globals
  void pushName(std::string name);
  void popName();
  std::string getName();

  std::unique_ptr<ExprAST> LogError(const char *Str) {
    LOG_ERROR() << "Error: " << Str << std::endl;
    return nullptr;
  }

  llvm::Value *LogErrorV(const char *Str) {
    LogError(Str);
    return nullptr;
  }

  template <typename T>
  static void dumpLLVM(const T *val, const std::string &prefix = "") {
    if (Logger::isDebugEnabled() && val) {
      std::string s;
      llvm::raw_string_ostream rso(s);
      if (!prefix.empty()) {
        rso << prefix;
      }
      val->print(rso);
      LOG_DEBUG() << rso.str() << "\n";
    }
  }

  template <typename T>
  static void dumpLLVM(const T &val, const std::string &prefix = "") {
    if (Logger::isDebugEnabled()) {
      std::string s;
      llvm::raw_string_ostream rso(s);
      if (!prefix.empty()) {
        rso << prefix;
      }
      val.print(rso);
      LOG_DEBUG() << rso.str() << "\n";
    }
  }

  void setConfiguration(StrideConfig option, bool enable = true);
  bool hasConfiguration(StrideConfig option);

  void setDebugInfoEnabled(bool enable = true) { m_debugInfo = enable; }
  bool isDebugInfoEnabled() const { return m_debugInfo; }
  void initializeDebugInfo(const std::string &primaryRelFile = "",
                           const std::string &primaryAbsFile = "");
  void finalizeDebugInfo();

  std::unique_ptr<StrideDebugEmitter> debugEmitter;
  bool m_debugInfo{false};

  std::unique_ptr<llvm::LLVMContext> TheContext;
  std::unique_ptr<llvm::Module> TheModule;
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::map<std::string, std::pair<llvm::Value *, std::optional<llvm::Type *>>>
      NamedValues;
  std::map<std::string, llvm::Value *> PortBlockMap;
  std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
  // Names of generated callables that are mangled, e.g. one variant per type
  // signature of a polymorphic module. Resolves them back to Stride names.
  NameMangler mangler;

  // Scope of the function instance whose parameter types are being
  // resolved. Used to find declarations of the nodes connected to its ports.
  struct ConnectionScope {
    const ScopeStack *scope;
    ASTNode tree;
  };
  std::vector<ConnectionScope> connectionScopes;

  // Pushes a ConnectionScope for the lifetime of the guard.
  class ConnectionScopeGuard {
  public:
    ConnectionScopeGuard(StrideCompiler &state, const ScopeStack *scope,
                         ASTNode tree)
        : m_state(state) {
      m_state.connectionScopes.push_back({scope, tree});
    }
    ~ConnectionScopeGuard() { m_state.connectionScopes.pop_back(); }
    ConnectionScopeGuard(const ConnectionScopeGuard &) = delete;
    ConnectionScopeGuard &operator=(const ConnectionScopeGuard &) = delete;

  private:
    StrideCompiler &m_state;
  };
  std::map<char, int> BinopPrecedence;

  CodeAnalysis::TypeTree m_intanceTree;

  std::unordered_map<std::string, std::vector<ExternalFunction>> functionMap;
  // LLVM type and description of each data type name (Int, Real, UInt8, ...)
  std::unordered_map<std::string, llvm::Type *> typesMap;
  std::unordered_map<std::string, DataTypes::Info> dataTypes;

  std::unordered_map<std::string, std::vector<DomainArg>> domainArgs;

  // Track pointer element types for opaque pointers (LLVM >= 17)
  std::map<llvm::Value *, llvm::Type *> pointerElementTypes;

  // State struct tracking for nested function calls
  struct StateStructInfo {
    llvm::StructType *structType{nullptr};
    llvm::Constant *defaultConstant{nullptr};
    std::map<std::string, unsigned> varIndices;
    std::map<ASTNode, unsigned> childIndices;

    size_t allocSize{0};
    std::vector<uint64_t> fieldOffsets;
    std::vector<size_t> fieldAllocSizes;
    // Serialized defaultConstant (allocSize bytes). defaultConstant can dangle
    // once the module is optimized or handed to the JIT, so runtime state
    // allocation must use this copy.
    std::vector<char> defaultBytes;

    llvm::Constant *getDefaultValue(const std::string &varName) const;
    llvm::Constant *getChildDefaultConstant(ASTNode childInstance) const;
  };

  struct DynamicDomainField {
    std::string name;
    llvm::Type *type;
    llvm::Constant *defaultVal;
  };
  std::map<ASTNode, std::vector<DynamicDomainField>> dynamicDomainFields;
  void addDynamicDomainField(ASTNode domainNode, const std::string &name,
                             llvm::Type *type, llvm::Constant *defaultVal);

  struct StateMachineInfo {
    std::string domainName;
    std::string smName;
    std::map<std::string, int32_t> stateIdsByName;
    std::map<std::string, int32_t> transitionIdsByName;
    std::map<std::pair<std::string, std::string>, int32_t>
        transitionIdsByStateAndName;
  };
  std::vector<StateMachineInfo> stateMachineInfos;

  std::map<ASTNode, StateStructInfo> stateStructMap;
  llvm::Value *currentFunctionStatePtr{nullptr};
  const StateStructInfo *currentFunctionStateInfo{nullptr};
  ASTNode m_tree{nullptr};

  bool doesNodeNeedState(const CodeAnalysis::TypeTree *node);
  // Single source of truth for whether a domain's process/init functions take
  // a __state struct. Domains only own state for their dynamic fields (domain
  // signals, state machine variables); stateful children alone do not make a
  // domain struct.
  bool doesDomainNeedState(ASTNode domainDecl) const;
  const std::vector<DynamicDomainField> *
  findDynamicDomainFields(ASTNode domainDecl) const;
  // Builds a compile-time default constant of `varType` from a `default:`
  // AST value (Int/Real/Switch/String scalars, broadcast to arrays, or a List
  // matching an array's length). Returns nullptr if it cannot be converted.
  llvm::Constant *makeDefaultConstant(ASTNode defaultNode, llvm::Type *varType);
  void buildNodeState(const CodeAnalysis::TypeTree &nodeTree);
  void buildStateStructTypes(const CodeAnalysis::TypeTree &tree,
                             ScopeStack &scope, ASTNode root);
  const StateStructInfo *getStateStructInfo(ASTNode instance) const;
  const StateStructInfo *getStateStructInfo(const std::string &name) const;
  const CodeAnalysis::TypeTree *
  findTypeTreeNode(ASTNode node,
                   const CodeAnalysis::TypeTree *tree = nullptr) const;

  std::vector<std::string> m_nameStack;
  uint32_t m_idCounter{0};

private:
  // Type of a block declared `type: <Port>.type`, from what the instance
  // connects to that port. nullptr if it cannot be determined.
  llvm::Type *resolvePortMemberType(std::shared_ptr<MemberAccessNode> typeProp,
                                    std::shared_ptr<DeclarationNode> funcDecl,
                                    std::shared_ptr<FunctionNode> instance,
                                    int depth);
  // Type of a port's block for a given function instance.
  llvm::Type *getPortBlockType(std::shared_ptr<DeclarationNode> portDecl,
                               std::shared_ptr<DeclarationNode> funcDecl,
                               std::shared_ptr<FunctionNode> instance,
                               int depth);
  // Type of the value carried by a node connected to a port. isDownstream is
  // true when the node receives the value, so a function node is described
  // by its input port; otherwise by its output port.
  llvm::Type *getConnectedNodeType(ASTNode node, bool isDownstream, int depth);
  // Declaration of a node connected to a port, from the node or by name in
  // the current ConnectionScope. Platform modules are not returned.
  std::shared_ptr<DeclarationNode> findConnectedDeclaration(ASTNode node) const;

  uint64_t m_configuration{NO_OPTIONS};
  std::map<std::string, std::pair<llvm::Value *, std::optional<llvm::Type *>>>
      m_globals;
};

} // namespace strd

#endif // STRIDECOMPILER_HPP
