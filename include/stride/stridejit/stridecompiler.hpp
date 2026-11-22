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
#include <variant>
#include <vector>

#include "stride/stridegen/codeanalysis.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/stridejit/bufferspec.hpp"
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
  // The platform module's resultSize: how to bound a String result that has
  // new characters ("sum", "format", "repeat"), or empty. Such a function
  // takes scratch space and its size before the result pointer.
  std::string resultSize;
  // status: on. The function reports a stride_status_t: returned when its
  // result goes through the out pointer, else through a trailing pointer.
  bool hasStatus{false};
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

  llvm::Type *getLLVMType(std::shared_ptr<strd::DeclarationNode> decl,
                          const ScopeStack &scope = {});

  // Reads the frameworkDataType declarations in tree into typesMap and
  // dataTypes, so each framework decides e.g. the size of Int and Real.
  void registerDataTypes(ASTNode tree);
  // The LLVM type for a data type: integers by bits, Float32 and Float64,
  // Switch is i1 and String is the {ptr, i32} string view.
  llvm::Type *getLLVMTypeForDataType(const DataTypes::Info &info);
  // The framework's Real and Int: double and i32 for JIT, float and i16 on
  // AVR. Use these, never a fixed LLVM type, wherever code means the
  // language's default real or integer type.
  llvm::Type *realType() { return typesMap.at("Real"); }
  llvm::Type *intType() { return typesMap.at("Int"); }
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
  // The symbol of the function generated for callable `decl` under `name`.
  // Different declarations can share a name (reactions nested in different
  // modules, modules of different packages): the first keeps `name`, the
  // others get `name_1`, `name_2`... so one never runs the other's code.
  std::string callableSymbol(const DeclarationNode *decl,
                             const std::string &name);

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
  // Every global, by declared name
  const std::map<std::string,
                 std::pair<llvm::Value *, std::optional<llvm::Type *>>> &
  globals() const {
    return m_globals;
  }

  // ---- String storage ----
  // A String variable is a {ptr, length} view plus a companion variable,
  // <name>__chars, that holds its characters: [maxSize x i8], or
  // [count x [maxSize x i8]] for an array of strings. After every write the
  // characters are copied into the companion (cut off at maxSize) and the
  // view is pointed at it, so the value no longer depends on where it came
  // from (a literal, the runtime's result arena, the host).
  static constexpr int kDefaultStringMaxSize = 128;
  static std::string stringCharsName(const std::string &name) {
    return name + "__chars";
  }
  bool isStringType(llvm::Type *type);
  // The maxSize property of decl, or kDefaultStringMaxSize.
  static int stringMaxSize(std::shared_ptr<DeclarationNode> decl);
  // The companion type for a variable of type varType (a String or an array
  // of String) declared by decl, or nullptr when varType holds no strings.
  llvm::Type *stringCharsType(std::shared_ptr<DeclarationNode> decl,
                              llvm::Type *varType);
  // After a write to the String variable name (element indices[0] for an
  // array): copies its characters into the variable's own buffer. Does
  // nothing for a variable without one. The store's status becomes the
  // variable's last write status; when the write was a call that already
  // recorded its own status (freshWrite false), the store's is ORed in.
  void commitStringWrite(
      const std::string &name,
      const std::vector<std::variant<size_t, std::string>> &indices,
      bool freshWrite = true, bool reportStatus = true);
  // The maxSize of the String variable name (of each element, for an array),
  // when it has its own buffer.
  std::optional<uint64_t> stringCapacity(const std::string &name);

  // ---- Scratch for string results ----
  // Concat, Repeat and Format (platform modules with a resultSize) write
  // their characters into scratch space on the stack of the function that
  // calls them, sized when compiling. Results alive together in one stream
  // get separate space; each stream reuses the space from its start, so a
  // function's scratch is its largest stream's need, and a function that
  // never makes such a result has none. ISR domains and threads run their
  // own functions on their own stacks, so each gets its own scratch.
  struct ScratchFrame {
    llvm::AllocaInst *alloca{nullptr};
    uint64_t offset{0}; // next free byte in the current stream
    uint64_t size{0};   // largest need so far
  };
  ScratchFrame scratch;
  // Space for a result of up to bytes characters, valid until the current
  // stream ends.
  llvm::Value *takeScratch(uint64_t bytes);
  // Sizes the current function's scratch once its body is generated.
  void finishScratch();

  // ---- Runtime status ----
  // A runtime call that can fail reports a stride_status_t (one bit per
  // kind of failure). Stride code never sees it: the compiler ORs it into
  // the domain's status word, <Domain>_status, a UInt8 that stays set until
  // the program writes it, and stores it as the last write's code on the
  // variable it wrote, <Name>_status. Both are read as members:
  // RootDomain.status, Title.status. A domain gets a word only when
  // something in it can report a status.
  static std::string statusName(const std::string &name) {
    return name + "_status";
  }
  // The current domain's status word, or nullptr when it has none (then the
  // status is dropped).
  llvm::Value *statusPtr{nullptr};
  // Debug builds only: the domain's log of failed calls, a ring of
  // kStatusLogSize statusLogEntryType() entries ({line, column, code}), and
  // how many failures there were in all. nullptr when not kept.
  // Strict: a String write that can be cut off, or whose size cannot be
  // bounded when compiling, is an error instead of a warning. strictErrors
  // counts them; generateIr fails when there are any.
  bool strict{false};
  unsigned strictErrors{0};
  static constexpr unsigned kStatusLogSize = 8;
  static std::string statusLogName(const std::string &domain) {
    return domain + "_statusLog";
  }
  static std::string statusCountName(const std::string &domain) {
    return domain + "_statusCount";
  }
  llvm::StructType *statusLogEntryType();
  llvm::Value *statusLogPtr{nullptr};
  llvm::Value *statusCountPtr{nullptr};
  // ORs status (an integer) into the domain's word.
  void recordStatus(llvm::Value *status);
  // Also stores it as varName's last write status, when varName has one
  // (ORed into it when accumulate: a later step of the same write).
  void recordStatus(llvm::Value *status, const std::string &varName,
                    bool accumulate = false);

  llvm::Type *getElementType(llvm::Value *V);

  /// Resolves a variable's array index (constant or named scalar) into an
  /// integer LLVM Value suitable for GEP, converting floating-point indices
  /// to 64-bit signed integers.
  std::vector<llvm::Value *> resolveGepIndices(
      const std::vector<std::variant<size_t, std::string>> &indices);

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
  std::map<std::string, llvm::Value *> PortEntityMap;
  std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
  // Names of generated callables that are mangled, e.g. one variant per type
  // signature of a polymorphic module. Resolves them back to Stride names.
  NameMangler mangler;
  // callableSymbol's assignments: (declaration, name) -> symbol, and the
  // declaration each symbol belongs to
  std::map<std::pair<const DeclarationNode *, std::string>, std::string>
      m_callableSymbols;
  std::map<std::string, const DeclarationNode *> m_callableSymbolOwners;

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
    std::map<std::string, std::string> varResetSignals;

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
  // Domain whose streams push into each buffer. Its _init resets the buffer.
  std::map<std::string, std::string> bufferDomains;
  // Resolved buffer declarations, for reads such as `Buf.frame`
  std::map<std::string, BufferSpec> bufferSpecs;
  void addDynamicDomainField(ASTNode domainNode, const std::string &name,
                             llvm::Type *type, llvm::Constant *defaultVal);

  struct StateMachineInfo {
    std::string domainName;
    std::string smName;
    std::map<std::string, int32_t> stateIdsByName;
    std::map<std::string, int32_t> transitionIdsByName;
    std::map<std::pair<std::string, std::string>, int32_t>
        transitionIdsByStateAndName;
    std::vector<std::string> triggerLatchVars;
  };
  std::vector<StateMachineInfo> stateMachineInfos;

  // Switch name -> latch variables of the transitions it triggers. A write
  // that turns the switch on sets them (see BinaryExprAST '=').
  std::map<std::string, std::vector<std::string>> triggerLatches;

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
  llvm::Constant *makeDefaultConstant(ASTNode defaultNode, llvm::Type *varType,
                                      const ScopeStack &scope = {},
                                      int depth = 0);
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
  llvm::Type *getPortEntityType(std::shared_ptr<DeclarationNode> portDecl,
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
