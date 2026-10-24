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

#ifndef STRIDEENVIRONMENT_HPP
#define STRIDEENVIRONMENT_HPP

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "stride/parser/ast.h"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/utils/cachemanager.h"
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
  enum class Role { Output, Input, Property, State, External, MemberAccess };

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

  int32_t propertyValue{0};
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

enum class BuildType { Debug, Release, RelWithDebInfo, MinSizeRel };
enum class DebugInfoLevel { None, LineTablesOnly, Full };

class StrideEnvironment {
public:
  using BuildType = strd::BuildType;
  using DebugInfoLevel = strd::DebugInfoLevel;

  StrideEnvironment(std::string strideroot = std::string());

  // Include paths
  void addIncludePath(const std::string &path) {
    m_includePaths.push_back(path);
  }
  void setIncludePaths(const std::vector<std::string> &paths) {
    m_includePaths = paths;
  }
  const std::vector<std::string> &getIncludePaths() const {
    return m_includePaths;
  }

  // AST Caching Controls (.strdcache)
  void enableCaching(bool enabled) {
    CacheManager::instance().setEnabled(enabled);
  }
  bool isCachingEnabled() const {
    return CacheManager::instance().isEnabled();
  }
  void setCacheDirectory(const std::string &dirPath) {
    CacheManager::instance().setCacheDirectory(dirPath);
  }
  std::string getCacheDirectory() const {
    return CacheManager::instance().getCacheDirectory().generic_string();
  }
  void clearCache(const std::string &sourceOrCacheDir = "") {
    if (sourceOrCacheDir.empty()) {
      CacheManager::instance().clearCache();
    } else {
      CacheManager::instance().clearCache(sourceOrCacheDir);
    }
  }
  CacheStats getCacheStats() const {
    return CacheManager::instance().getStats();
  }
  void resetCacheStats() { CacheManager::instance().resetStats(); }

  void enableObjectCaching(bool enabled) { m_cacheObjectCode = enabled; }
  bool isObjectCachingEnabled() const { return m_cacheObjectCode; }
  void enableBitcodeCaching(bool enabled) { m_cacheBitcode = enabled; }
  bool isBitcodeCachingEnabled() const { return m_cacheBitcode; }
  void enableDebugInfo(bool enabled = true) {
    m_debugInfo = enabled;
    mStrideEnv.setDebugInfoEnabled(enabled);
  }
  bool isDebugInfoEnabled() const { return m_debugInfo; }
  void setBuildType(BuildType type) {
    m_buildType = type;
    switch (type) {
    case BuildType::Debug:
      enableDebugInfo(true);
      m_debugInfoLevel = DebugInfoLevel::Full;
      m_optimizationLevel = 0;
      m_optimizeCode = false;
      break;
    case BuildType::RelWithDebInfo:
      enableDebugInfo(true);
      m_debugInfoLevel = DebugInfoLevel::LineTablesOnly;
      m_optimizationLevel = 2;
      m_optimizeCode = true;
      break;
    case BuildType::Release:
      enableDebugInfo(false);
      m_debugInfoLevel = DebugInfoLevel::None;
      m_optimizationLevel = 3;
      m_optimizeCode = true;
      break;
    case BuildType::MinSizeRel:
      enableDebugInfo(false);
      m_debugInfoLevel = DebugInfoLevel::None;
      m_optimizationLevel = 2;
      m_optimizeCode = true;
      break;
    }
  }
  BuildType getBuildType() const { return m_buildType; }

  void setDebugInfoLevel(DebugInfoLevel level) {
    m_debugInfoLevel = level;
    enableDebugInfo(level != DebugInfoLevel::None);
  }
  DebugInfoLevel getDebugInfoLevel() const { return m_debugInfoLevel; }

  void setOptimizationLevel(int level) {
    m_optimizationLevel = level;
    m_optimizeCode = (level > 0);
  }
  int getOptimizationLevel() const { return m_optimizationLevel; }
  void setPrimarySourcePath(const std::string &path) { m_primarySourcePath = path; }
  const std::string &getPrimarySourcePath() const { return m_primarySourcePath; }
  std::string getJitObjectTag() const;

  // IR generator
  bool generateIr(std::string path, bool emitAllFunctions = false);
  bool generateIr(const std::vector<std::string> &paths,
                  bool emitAllFunctions = false);
  bool generateIr(strd::ASTNode root, bool emitAllFunctions = false);
  bool generateAllRootFunctions(strd::ASTNode root);

  // Standalone function generator
  bool generateStandaloneFunction(std::string funcName, ScopeStack &scope,
                                  ASTNode tree = nullptr);

  // State struct management
  void *allocateState(const std::string &funcName);
  std::shared_ptr<void> allocateSharedState(const std::string &funcName);
  void deallocateState(void *statePtr);
  bool hasState(const std::string &funcName) const;
  size_t getStateSize(const std::string &funcName) const;

  // Dynamic state variable access by name or pattern
  template <typename T>
  std::optional<T> getStateVar(const void *statePtr, const std::string &varName,
                               std::optional<int> index = std::nullopt,
                               const std::string &domainName = "") const;

  std::optional<int32_t> getStateVar(const void *statePtr,
                                     const std::string &varName,
                                     std::optional<int> index = std::nullopt,
                                     const std::string &domainName = "") const {
    return getStateVar<int32_t>(statePtr, varName, index, domainName);
  }
  template <typename T>
  bool setStateVar(void *statePtr, const std::string &varName, T value,
                   std::optional<int> index = std::nullopt,
                   const std::string &domainName = "");

  // Functional state machine accessors (no hardcoded string mangling required)
  std::optional<int32_t>
  getActiveStateId(const void *statePtr, const std::string &smName = "",
                   const std::string &domainName = "") const;
  std::optional<int32_t>
  getActiveStateId(const std::shared_ptr<void> &statePtr,
                   const std::string &smName = "",
                   const std::string &domainName = "") const;

  bool requestTransition(void *statePtr, int32_t transitionId,
                         const std::string &smName = "",
                         const std::string &domainName = "",
                         std::optional<int32_t> transitionArg = std::nullopt);
  bool requestTransition(const std::shared_ptr<void> &statePtr,
                         int32_t transitionId, const std::string &smName = "",
                         const std::string &domainName = "",
                         std::optional<int32_t> transitionArg = std::nullopt);

  std::optional<int32_t>
  getTransitionRequestId(const void *statePtr, const std::string &smName = "",
                         const std::string &domainName = "") const;
  std::optional<int32_t>
  getTransitionRequestId(const std::shared_ptr<void> &statePtr,
                         const std::string &smName = "",
                         const std::string &domainName = "") const;

  std::optional<int32_t>
  getHistoryStateId(const void *statePtr, const std::string &stateName = "",
                    const std::string &smName = "",
                    const std::string &domainName = "") const;
  std::optional<int32_t> getHistoryStateId(
      const std::shared_ptr<void> &statePtr, const std::string &stateName = "",
      const std::string &smName = "", const std::string &domainName = "") const;

  std::optional<int32_t> getStateId(const std::string &stateName,
                                    const std::string &smName = "",
                                    const std::string &domainName = "") const;
  std::optional<int32_t>
  getTransitionId(const std::string &transitionName,
                  const std::string &smName = "",
                  const std::string &domainName = "",
                  const std::string &stateName = "") const;
  std::optional<int32_t> getTransitionIdForState(
      const std::string &transitionName, const std::string &stateName,
      const std::string &smName = "", const std::string &domainName = "") const;

  const std::vector<StrideCompiler::StateMachineInfo> &
  getStateMachineInfos() const {
    return mStrideEnv.stateMachineInfos;
  }

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
  int32_t stepTick(const std::string &domainOrFuncName, void *statePtr = nullptr,
                   const std::vector<void *> &inOutArgs = {});

  void prepareTree(ASTNode tree);

  // JIT
  void initializeJIT();
  bool compileInMemory();
  bool compileObjectToDisk(std::string path);

  // Ahead-Of-Time (AOT) Object & C Header Emission
  bool emitObjectFile(const std::string &outputPath,
                      const std::string &targetTriple = "",
                      const std::string &cpu = "",
                      const std::string &features = "",
                      const std::string &relocModel = "",
                      const std::string &codeModel = "");

  bool emitCHeader(const std::string &outputPath,
                   const std::string &domainName = "") const;
  std::string generateCHeaderString(const std::string &domainName = "") const;

  StrideCompiler mStrideEnv;
  std::unique_ptr<llvm::orc::LLJIT> JIT;
  llvm::orc::ThreadSafeContext TSCtx;

  llvm::Expected<llvm::orc::ExecutorAddr> getFunction(std::string);
  template <typename T> T *getGlobal(std::string varName);

private:
  mutable std::optional<llvm::DataLayout> m_dataLayout;
  const llvm::DataLayout *getDataLayout() const;
  void optimizeModule();
  // Serializes every StateStructInfo::defaultConstant into defaultBytes while
  // the LLVM module is still intact. Must run before optimization (GlobalDCE
  // can destroy string-literal globals, and with them any constant referencing
  // them) and before the module is handed to the JIT.
  void snapshotStateDefaults();
  bool loadLibrary(const char *libName, std::string &err);
  bool generateCompiledObject(std::string path, std::string TargetTriple);

  // Configuration
  std::string m_strideRoot;
  std::string m_primarySourcePath;
  std::vector<std::string> m_includePaths;
  std::vector<ASTNode> m_trees;
  bool m_optimizeCode{true};
  bool m_moduleOptimized{false};
  bool m_cacheObjectCode{true};
  bool m_cacheBitcode{true};
  bool m_debugInfo{false};
  BuildType m_buildType{BuildType::Release};
  DebugInfoLevel m_debugInfoLevel{DebugInfoLevel::None};
  int m_optimizationLevel{2};
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
