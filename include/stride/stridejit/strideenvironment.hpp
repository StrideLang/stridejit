#ifndef STRIDEENVIRONMENT_HPP
#define STRIDEENVIRONMENT_HPP

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "stride/parser/ast.h"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/utils/logger.h"

// llvm forward declarations
namespace llvm {
class Value;
}; // namespace llvm

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

namespace strd {
struct StrideExternalVariable {
  std::string name;
  llvm::Type *type;
};

struct FunctionArgInfo {
  enum class Role { Output, Input, State, External, PortProperty };

  std::string name;
  Role role{Role::Input};
  DataType type{DataType::INT32};
  std::string typeName;
  llvm::Type *llvmType{nullptr};
  bool isPointer{true};
  std::string sizeProperty; // Mane of size port property if dynamic size array
  size_t elementSize{0};
  size_t count{1};
  size_t totalBytes{0};

  // Returns true if this argument is an array.
  // Static size arrays have count > 1.
  // Undetermined dynamic size arrays have count == 0.
  bool isArray() const { return count != 1; }

  // Returns the static size of the array.
  // If the array has an undetermined size (dynamic), this returns 0.
  size_t getArraySize() const { return count; }

  int32_t portPropertyValue{0};
};

class InvokerParameterList {
public:
  InvokerParameterList() = default;
  explicit InvokerParameterList(std::vector<FunctionArgInfo> args);

  bool setArg(size_t index, void *ptr);
  bool setArg(const std::string &name, void *ptr);
  bool setState(void *statePtr);
  bool setProperty(const std::string &name, int32_t value);
  bool setArrayArg(const std::string &name, void *ptr, size_t size);

  void *getArg(size_t index) const;
  void *getArg(const std::string &name) const;

  size_t size() const { return m_args.size(); }
  bool isComplete() const;
  void **data() { return m_args.data(); }
  const void *const *data() const { return m_args.data(); }
  const std::vector<FunctionArgInfo> &getArgInfos() const { return m_argInfos; }
  const FunctionArgInfo *getArgInfo(size_t index) const;
  const FunctionArgInfo *getArgInfo(const std::string &name) const;

private:
  std::vector<void *> m_args;
  std::vector<FunctionArgInfo> m_argInfos;
  std::unordered_map<std::string, size_t> m_nameToIndex;
};

class StrideEnvironment {
public:
  StrideEnvironment(std::string strideroot = std::string());

  // IR generator
  bool generateIr(std::string path);
  bool generateIr(strd::ASTNode root);

  // Standalone function generator
  bool generateStandaloneFunction(std::string funcName, ScopeStack &scope,
                                  ASTNode tree = nullptr);

  // State struct management
  void *allocateState(const std::string &funcName);
  void deallocateState(void *statePtr);
  bool hasState(const std::string &funcName) const;
  size_t getStateSize(const std::string &funcName) const;

  // Programmatic function inspection and dynamic invocation
  std::vector<FunctionArgInfo>
  getFunctionArgs(const std::string &funcName) const;
  size_t getFunctionArgCount(const std::string &funcName) const;
  std::optional<FunctionArgInfo> getFunctionArg(const std::string &funcName,
                                                size_t index) const;
  std::optional<FunctionArgInfo>
  getFunctionArg(const std::string &funcName, const std::string &argName) const;
  int getFunctionArgIndex(const std::string &funcName,
                          const std::string &argName) const;

  InvokerParameterList
  createInvokerParamList(const std::string &funcName) const;
  int32_t invoke(const std::string &funcName, void **args);
  int32_t invoke(const std::string &funcName, InvokerParameterList &params);

  void prepareTree(ASTNode tree);

  // JIT
  void initializeJIT();
  bool compileInMemory();
  bool compileObjectToDisk(std::string path);

  StrideCompiler state;
  std::unique_ptr<llvm::orc::LLJIT> JIT;
  llvm::orc::ThreadSafeContext TSCtx;

  llvm::Expected<llvm::orc::ExecutorAddr> getFunction(std::string);
  template <typename T> T *getGlobal(std::string varName);

private:
  mutable std::optional<llvm::DataLayout> m_dataLayout;
  const llvm::DataLayout *getDataLayout() const;
  void optimizeModule();
  bool loadLibrary(const char *libName, std::string &err);
  bool generateCompiledObject(std::string path, std::string TargetTriple);

  // Configuration
  std::string m_strideRoot;
  bool m_optimizeCode{true};
  bool m_verbose{true};
};

template <typename T> T *StrideEnvironment::getGlobal(std::string varName) {
  auto Symbol = JIT->lookup(varName);

  if (Symbol) {
    T *host_ptr = nullptr;
    try {
      host_ptr = Symbol->toPtr<T *>();
    } catch (...) {
      // std::cerr << "Can't cast variable to type" << std::endl;
    }
    return host_ptr;
  } else {
    auto err = Symbol.takeError();
    LOG_ERROR() << "Global variable not found: " << varName
                << ". Error: " << llvm::toString(std::move(err)) << std::endl;
  }
  return nullptr;
}

} // namespace strd

#endif // STRIDEENVIRONMENT_HPP
