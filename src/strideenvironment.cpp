#include "stride/utils/logger.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

// stride
#include "stride/codegen/coderesolver.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

// stridejit
#include "stride/stridejit/statemachine.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// llvm
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#if LLVM_VERSION_MAJOR >= 17
#include "llvm/Analysis/CGSCCPassManager.h"
// #include "llvm/Analysis/CGSCCAnalysisManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/TargetSelect.h"

#else
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#endif

// JIT
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
// #include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

// #include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

using namespace strd;

StrideEnvironment::StrideEnvironment(std::string strideRoot)
    : m_strideRoot(strideRoot) {
  if (m_strideRoot.size() == 0) {
    m_strideRoot = ASTFunctions::getDefaultStrideRoot();
  }
  LOG_INFO() << "Using STRIDEROOT = " << m_strideRoot << std::endl;
}

void StrideEnvironment::initializeJIT() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  if (auto JTMB = llvm::orc::JITTargetMachineBuilder::detectHost()) {
    if (auto TM = JTMB->createTargetMachine()) {
      m_dataLayout = (*TM)->createDataLayout();
      if (mStrideEnv.TheModule) {
        mStrideEnv.TheModule->setDataLayout(*m_dataLayout);
#if LLVM_VERSION_MAJOR >= 20
        mStrideEnv.TheModule->setTargetTriple((*TM)->getTargetTriple());
#else
        mStrideEnv.TheModule->setTargetTriple((*TM)->getTargetTriple().str());
#endif
      }
    }
  }
}

void StrideEnvironment::prepareTree(ASTNode tree) {
  auto systemNodes = ASTQuery::getSystemNodes(tree);
  if (systemNodes.size() == 0) {
    auto systemNode =
        std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__);
    tree->addChild(systemNode);
  }

  CodeResolver resolver(tree, ASTFunctions::getDefaultStrideRoot(),
                        SystemConfiguration());
  resolver.process();
}

bool StrideEnvironment::generateAllRootFunctions(ASTNode root) {
  if (!root)
    return false;

  ScopeStack scope;
  scope.push_back({root, {}});

  for (const auto &child : root->getChildren()) {
    if (child->getNodeType() == AST::Declaration ||
        child->getNodeType() == AST::ArrayDeclaration ||
        child->getNodeType() == AST::BundleDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      std::string objType = decl->getObjectType();
      std::string entType = decl->getEntityType();

      if (objType == "module" || entType == "module" ||
          objType == "reaction" || entType == "reaction" ||
          objType == "action" || entType == "action" ||
          objType == "function" || entType == "function") {
        if (objType == "domain" || entType == "domain" ||
            objType == "_domainDefinition" || entType == "_domainDefinition" ||
            objType == "platformModule" || entType == "platformModule") {
          continue;
        }

        std::string funcName = decl->getName();
        if (mStrideEnv.FunctionProtos.find(funcName) ==
            mStrideEnv.FunctionProtos.end()) {
          StrideGenerator::generateStandaloneFunction(decl, root, scope,
                                                      mStrideEnv);
        }
      }
    }
  }
  return true;
}

bool StrideEnvironment::generateIr(std::string path, bool emitAllFunctions) {
  ASTNode tree = AST::parseFile(path.c_str());
  if (!tree) {
    for (auto &error : AST::getParseErrors()) {
      LOG_ERROR() << error.getErrorText() << std::endl;
    }
    return false;
  }

  std::filesystem::path filePath(path);
  if (filePath.has_parent_path()) {
    std::string parentDir = filePath.parent_path().generic_string();
    if (std::find(m_includePaths.begin(), m_includePaths.end(), parentDir) ==
        m_includePaths.end()) {
      m_includePaths.insert(m_includePaths.begin(), parentDir);
    }
  }

  prepareTree(tree);
  return generateIr(tree, emitAllFunctions);
}

bool StrideEnvironment::generateIr(const std::vector<std::string> &paths,
                                   bool emitAllFunctions) {
  if (paths.empty()) {
    LOG_ERROR() << "No input files provided to generateIr." << std::endl;
    return false;
  }
  if (paths.size() == 1) {
    return generateIr(paths[0], emitAllFunctions);
  }

  for (const auto &p : paths) {
    std::filesystem::path filePath(p);
    if (filePath.has_parent_path()) {
      std::string parentDir = filePath.parent_path().generic_string();
      if (std::find(m_includePaths.begin(), m_includePaths.end(), parentDir) ==
          m_includePaths.end()) {
        m_includePaths.push_back(parentDir);
      }
    }
  }

  for (const auto &p : paths) {
    ASTNode tree = AST::parseFile(p.c_str());
    if (!tree) {
      for (auto &error : AST::getParseErrors()) {
        LOG_ERROR() << error.getErrorText() << std::endl;
      }
      return false;
    }

    prepareTree(tree);
    m_trees.push_back(tree);

    // Each file is compiled in its isolated scope into the shared LLVM Module
    ScopeStack globalScope;
    {
      globalScope.push_back({nullptr, {}});
      std::vector<ASTNode> platformlib = ASTFunctions::loadAllInDirectory(
          m_strideRoot + "/frameworks/JIT/1.0/platformlib");
      auto &frameworkScope = globalScope.back().second;

      for (const auto &member : platformlib) {
        if (member->getNodeType() == AST::Declaration ||
            member->getNodeType() == AST::ArrayDeclaration ||
            member->getNodeType() == AST::BundleDeclaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(member);
          if (decl->getObjectType() == "platformModule") {
            StrideGenerator::generatePlatformFunctionSignature(
                decl, frameworkScope, mStrideEnv);
          }
        }
      }
    }

    if (!ASTFunctions::preprocess(tree, &globalScope)) {
      return false;
    }
    StrideGenerator::compile(tree, globalScope, mStrideEnv);

    if (emitAllFunctions) {
      generateAllRootFunctions(tree);
    }
  }

  if (emitAllFunctions) {
    optimizeModule();
  } else {
    m_moduleOptimized = false;
  }
  return true;
}

bool StrideEnvironment::generateIr(ASTNode root, bool emitAllFunctions) {
  ScopeStack globalScope;
  {
    globalScope.push_back({nullptr, {}});
    // FIXME don't hardcode library version
    std::vector<ASTNode> platformlib = ASTFunctions::loadAllInDirectory(
        m_strideRoot + "/frameworks/JIT/1.0/platformlib");
    auto &frameworkScope = globalScope.back().second;

    for (const auto &member : platformlib) {
      if (member->getNodeType() == AST::Declaration ||
          member->getNodeType() == AST::ArrayDeclaration ||
          member->getNodeType() == AST::BundleDeclaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(member);
        if (decl->getObjectType() == "platformModule") {
          StrideGenerator::generatePlatformFunctionSignature(
              decl, frameworkScope, mStrideEnv);
        }
      }
    }
  }

  if (!ASTFunctions::preprocess(root, &globalScope)) {
    return false;
  }
  m_trees.push_back(root);
  StrideGenerator::compile(root, globalScope, mStrideEnv);

  if (emitAllFunctions) {
    generateAllRootFunctions(root);
    optimizeModule();
  } else {
    m_moduleOptimized = false;
  }
  return true;
}

void StrideEnvironment::optimizeModule() {
  m_moduleOptimized = true;
  if (m_optimizeCode) {
#if LLVM_VERSION_MAJOR >= 17
    // New Pass Manager
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM =
        PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

    MPM.run(*mStrideEnv.TheModule, MAM);
#else
    // Legacy Pass Manager
    std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
    TheFPM = std::make_unique<llvm::legacy::FunctionPassManager>(
        mStrideEnv.TheModule.get());

    { // Potential for loop vectorization?
      // From:
      // https://discourse.llvm.org/t/how-to-generate-ir-so-that-the-loop-vectorizer-can-vectorize-it/69096
      //      llvm::LoopAnalysisManager     lam;
      //      llvm::FunctionAnalysisManager fam;
      //      llvm::CGSCCAnalysisManager    cgam;
      //      llvm::ModuleAnalysisManager   mam;
      //      llvm::PassBuilder pb;
      //      pb.registerModuleAnalyses(mam);
      //      pb.registerCGSCCAnalyses(cgam);
      //      pb.registerFunctionAnalyses(fam);
      //      pb.registerLoopAnalyses(lam);
      //      pb.crossRegisterProxies(lam, fam, cgam, mam);
      //      llvm::ModulePassManager mpm =
      //      pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
      //      mpm.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::LoopVectorizePass()));
      //      mpm.run(*data->module, mam);
    }

    // Do simple "peephole" optimizations and bit-twiddling optzns.
    TheFPM->add(llvm::createInstructionCombiningPass());
    // Reassociate expressions.
    TheFPM->add(llvm::createReassociatePass());
    // Eliminate Common SubExpressions.
    TheFPM->add(llvm::createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    TheFPM->add(llvm::createCFGSimplificationPass());

    TheFPM->doInitialization();

    for (auto &F : *mStrideEnv.TheModule) {
      TheFPM->run(F);
    }
#endif
  }
  if (Logger::isDebugEnabled() && mStrideEnv.TheModule) {
    std::string s;
    llvm::raw_string_ostream rso(s);
    mStrideEnv.TheModule->print(rso, nullptr);
    LOG_DEBUG() << rso.str() << "\n";
  }
}

bool StrideEnvironment::generateStandaloneFunction(std::string funcName,
                                                   ScopeStack &scope,
                                                   ASTNode tree) {
  auto func = StrideGenerator::generateStandaloneFunction(funcName, tree, scope,
                                                          mStrideEnv);
  if (!func) {
    return false;
  }

  m_moduleOptimized = false;
  return true;
}

static void writeConstantToBuffer(llvm::Constant *C, char *buffer,
                                  const llvm::DataLayout &DL) {
  if (!C || !buffer) {
    return;
  }
  if (llvm::isa<llvm::ConstantAggregateZero>(C)) {
    size_t sz = DL.getTypeAllocSize(C->getType());
    memset(buffer, 0, sz);
    return;
  }
  if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(C)) {
    size_t sz = DL.getTypeAllocSize(CI->getType());
    uint64_t val = CI->getZExtValue();
    memcpy(buffer, &val, sz);
  } else if (auto *CFP = llvm::dyn_cast<llvm::ConstantFP>(C)) {
    size_t sz = DL.getTypeAllocSize(CFP->getType());
    llvm::APInt api = CFP->getValueAPF().bitcastToAPInt();
    memcpy(buffer, api.getRawData(), sz);
  } else if (auto *CS = llvm::dyn_cast<llvm::ConstantStruct>(C)) {
    auto *ST = CS->getType();
    const llvm::StructLayout *SL = DL.getStructLayout(ST);
    for (unsigned i = 0; i < CS->getNumOperands(); ++i) {
      auto *op = llvm::cast<llvm::Constant>(CS->getOperand(i));
      uint64_t offset = SL->getElementOffset(i);
      writeConstantToBuffer(op, buffer + offset, DL);
    }
  } else if (auto *CA = llvm::dyn_cast<llvm::ConstantArray>(C)) {
    llvm::Type *elemTy = CA->getType()->getElementType();
    size_t elemSz = DL.getTypeAllocSize(elemTy);
    for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
      auto *op = llvm::cast<llvm::Constant>(CA->getOperand(i));
      writeConstantToBuffer(op, buffer + (i * elemSz), DL);
    }
  } else if (auto *CDA = llvm::dyn_cast<llvm::ConstantDataArray>(C)) {
    llvm::StringRef raw = CDA->getRawDataValues();
    memcpy(buffer, raw.data(), raw.size());
  }
}

const llvm::DataLayout *StrideEnvironment::getDataLayout() const {
  if (m_dataLayout) {
    return &*m_dataLayout;
  }
  if (JIT) {
    m_dataLayout = JIT->getDataLayout();
    return &*m_dataLayout;
  }
  if (mStrideEnv.TheModule) {
    m_dataLayout = mStrideEnv.TheModule->getDataLayout();
    return &*m_dataLayout;
  }
  return nullptr;
}

void *StrideEnvironment::allocateState(const std::string &funcName) {
  const auto *info = mStrideEnv.getStateStructInfo(funcName);
  if (!info || !info->structType || info->allocSize == 0) {
    return nullptr;
  }
  void *mem = malloc(info->allocSize);
  if (!mem) {
    return nullptr;
  }
  memset(mem, 0, info->allocSize);
  if (info->defaultConstant) {
    const auto *DL = getDataLayout();
    if (DL) {
      writeConstantToBuffer(info->defaultConstant, static_cast<char *>(mem),
                            *DL);
    }
  }
  return mem;
}

std::shared_ptr<void>
StrideEnvironment::allocateSharedState(const std::string &funcName) {
  void *ptr = allocateState(funcName);
  if (!ptr) {
    return nullptr;
  }
  return std::shared_ptr<void>(ptr, [this](void *p) { deallocateState(p); });
}

void StrideEnvironment::deallocateState(void *statePtr) {
  if (statePtr) {
    free(statePtr);
  }
}

bool StrideEnvironment::hasState(const std::string &funcName) const {
  const auto *info = mStrideEnv.getStateStructInfo(funcName);
  return info && info->structType != nullptr;
}

size_t StrideEnvironment::getStateSize(const std::string &funcName) const {
  const auto *info = mStrideEnv.getStateStructInfo(funcName);
  if (!info || !info->structType) {
    return 0;
  }
  return info->allocSize;
}

static const StrideCompiler::StateStructInfo *
findStructInfo(const StrideCompiler &compilerState,
               const std::string &domainName) {
  if (domainName.empty()) {
    LOG_ERROR() << "findStructInfo called with empty domain name." << std::endl;
    return nullptr;
  }
  return compilerState.getStateStructInfo(domainName);
}

static std::optional<unsigned> findFieldByFunctionality(
    const StrideCompiler::StateStructInfo &info, StateMachineField field,
    const std::string &filter1 = "", const std::string &filter2 = "") {
  std::string suffix = StateMachine::getFieldSuffix(field);
  for (const auto &[key, idx] : info.varIndices) {
    if (key.length() >= suffix.length() &&
        key.compare(key.length() - suffix.length(), suffix.length(), suffix) ==
            0) {
      bool match1 = filter1.empty() || (key.find(filter1) != std::string::npos);
      bool match2 = filter2.empty() || (key.find(filter2) != std::string::npos);
      if (match1 && match2) {
        return idx;
      }
    }
  }
  return std::nullopt;
}

template <typename T>
std::optional<T>
StrideEnvironment::getStateVar(const void *statePtr, const std::string &varName,
                               std::optional<int> index,
                               const std::string &domainName) const {
  if (!statePtr)
    return std::nullopt;

  std::string baseName = varName;
  size_t arrayIdx = index.has_value() ? static_cast<size_t>(index.value()) : 0;

  const auto *info = findStructInfo(mStrideEnv, domainName);
  if (!info || !info->structType)
    return std::nullopt;

  unsigned fieldIdx = 0;
  auto it = info->varIndices.find(baseName);
  if (it != info->varIndices.end()) {
    fieldIdx = it->second;
  } else {
    it = info->varIndices.find(varName);
    if (it != info->varIndices.end()) {
      fieldIdx = it->second;
    } else {
      bool found = false;
      for (const auto &[key, idx] : info->varIndices) {
        if (key.find(baseName) != std::string::npos) {
          fieldIdx = idx;
          found = true;
          break;
        }
      }
      if (!found)
        return std::nullopt;
    }
  }

  if (fieldIdx >= info->fieldOffsets.size())
    return std::nullopt;

  uint64_t offset = info->fieldOffsets[fieldIdx];
  llvm::Type *fieldType = info->structType->getElementType(fieldIdx);

  const char *bytePtr = static_cast<const char *>(statePtr) + offset;
  if (fieldType->isArrayTy()) {
    llvm::Type *elemTy = fieldType->getArrayElementType();
    size_t elemSz = (fieldIdx < info->fieldAllocSizes.size() &&
                     fieldType->getArrayNumElements() > 0)
                        ? (info->fieldAllocSizes[fieldIdx] /
                           fieldType->getArrayNumElements())
                        : 0;
    if (elemSz == 0) {
      const auto *DL = getDataLayout();
      if (!DL)
        return std::nullopt;
      elemSz = DL->getTypeAllocSize(elemTy);
    }
    const char *elemPtr = bytePtr + (arrayIdx * elemSz);
    if (elemTy->isDoubleTy()) {
      return static_cast<T>(*reinterpret_cast<const double *>(elemPtr));
    } else if (elemTy->isFloatTy()) {
      return static_cast<T>(*reinterpret_cast<const float *>(elemPtr));
    } else if (elemTy->isIntegerTy(1) || elemTy->isIntegerTy(8)) {
      return static_cast<T>(*reinterpret_cast<const uint8_t *>(elemPtr));
    } else if (elemTy->isIntegerTy(16)) {
      return static_cast<T>(*reinterpret_cast<const int16_t *>(elemPtr));
    } else if (elemTy->isIntegerTy(32)) {
      return static_cast<T>(*reinterpret_cast<const int32_t *>(elemPtr));
    } else if (elemTy->isIntegerTy(64)) {
      return static_cast<T>(*reinterpret_cast<const int64_t *>(elemPtr));
    }
    return static_cast<T>(*reinterpret_cast<const int32_t *>(elemPtr));
  }

  if (fieldType->isDoubleTy()) {
    return static_cast<T>(*reinterpret_cast<const double *>(bytePtr));
  } else if (fieldType->isFloatTy()) {
    return static_cast<T>(*reinterpret_cast<const float *>(bytePtr));
  } else if (fieldType->isIntegerTy(1) || fieldType->isIntegerTy(8)) {
    return static_cast<T>(*reinterpret_cast<const uint8_t *>(bytePtr));
  } else if (fieldType->isIntegerTy(16)) {
    return static_cast<T>(*reinterpret_cast<const int16_t *>(bytePtr));
  } else if (fieldType->isIntegerTy(32)) {
    return static_cast<T>(*reinterpret_cast<const int32_t *>(bytePtr));
  } else if (fieldType->isIntegerTy(64)) {
    return static_cast<T>(*reinterpret_cast<const int64_t *>(bytePtr));
  }
  return static_cast<T>(*reinterpret_cast<const int32_t *>(bytePtr));
}

template <typename T>
bool StrideEnvironment::setStateVar(void *statePtr, const std::string &varName,
                                    T value, std::optional<int> index,
                                    const std::string &domainName) {
  if (!statePtr)
    return false;

  std::string baseName = varName;
  size_t arrayIdx = index.has_value() ? static_cast<size_t>(index.value()) : 0;
  auto bracketPos = varName.find('[');
  if (bracketPos != std::string::npos) {
    baseName = varName.substr(0, bracketPos);
    if (!index.has_value()) {
      auto closeBracketPos = varName.find(']', bracketPos);
      if (closeBracketPos != std::string::npos) {
        std::string idxStr =
            varName.substr(bracketPos + 1, closeBracketPos - bracketPos - 1);
        try {
          arrayIdx = std::stoul(idxStr);
        } catch (...) {
        }
      }
    }
  }

  const auto *info = findStructInfo(mStrideEnv, domainName);
  if (!info || !info->structType)
    return false;

  unsigned fieldIdx = 0;
  auto it = info->varIndices.find(baseName);
  if (it != info->varIndices.end()) {
    fieldIdx = it->second;
  } else {
    it = info->varIndices.find(varName);
    if (it != info->varIndices.end()) {
      fieldIdx = it->second;
    } else {
      bool found = false;
      for (const auto &[key, idx] : info->varIndices) {
        if (key.find(baseName) != std::string::npos) {
          fieldIdx = idx;
          found = true;
          break;
        }
      }
      if (!found)
        return false;
    }
  }

  if (fieldIdx >= info->fieldOffsets.size())
    return false;

  uint64_t offset = info->fieldOffsets[fieldIdx];
  llvm::Type *fieldType = info->structType->getElementType(fieldIdx);

  char *bytePtr = static_cast<char *>(statePtr) + offset;
  if (fieldType->isArrayTy()) {
    llvm::Type *elemTy = fieldType->getArrayElementType();
    size_t elemSz = (fieldIdx < info->fieldAllocSizes.size() &&
                     fieldType->getArrayNumElements() > 0)
                        ? (info->fieldAllocSizes[fieldIdx] /
                           fieldType->getArrayNumElements())
                        : 0;
    if (elemSz == 0) {
      const auto *DL = getDataLayout();
      if (!DL)
        return false;
      elemSz = DL->getTypeAllocSize(elemTy);
    }
    char *elemPtr = bytePtr + (arrayIdx * elemSz);
    if (elemTy->isIntegerTy(1) || elemTy->isIntegerTy(8)) {
      *reinterpret_cast<int8_t *>(elemPtr) = static_cast<int8_t>(value);
    } else if (elemTy->isIntegerTy(32)) {
      *reinterpret_cast<int32_t *>(elemPtr) = value;
    } else if (elemTy->isIntegerTy(64)) {
      *reinterpret_cast<int64_t *>(elemPtr) = static_cast<int64_t>(value);
    } else if (elemTy->isDoubleTy()) {
      *reinterpret_cast<double *>(elemPtr) = static_cast<double>(value);
    } else if (elemTy->isFloatTy()) {
      *reinterpret_cast<float *>(elemPtr) = static_cast<float>(value);
    } else {
      *reinterpret_cast<int32_t *>(elemPtr) = value;
    }
    return true;
  }

  if (fieldType->isIntegerTy(1) || fieldType->isIntegerTy(8)) {
    *reinterpret_cast<int8_t *>(bytePtr) = static_cast<int8_t>(value);
  } else if (fieldType->isIntegerTy(64)) {
    *reinterpret_cast<int64_t *>(bytePtr) = static_cast<int64_t>(value);
  } else if (fieldType->isDoubleTy()) {
    *reinterpret_cast<double *>(bytePtr) = static_cast<double>(value);
  } else if (fieldType->isFloatTy()) {
    *reinterpret_cast<float *>(bytePtr) = static_cast<float>(value);
  } else {
    *reinterpret_cast<int32_t *>(bytePtr) = value;
  }
  return true;
}

std::optional<int32_t>
StrideEnvironment::getActiveStateId(const void *statePtr,
                                    const std::string &smName,
                                    const std::string &domainName) const {
  if (!statePtr) {
    return std::nullopt;
  }
  const auto *info = findStructInfo(mStrideEnv, domainName);
  if (!info || !info->structType) {
    return std::nullopt;
  }

  auto fieldIdxOpt =
      findFieldByFunctionality(*info, StateMachineField::ActiveStateId, smName);
  if (!fieldIdxOpt || *fieldIdxOpt >= info->fieldOffsets.size())
    return std::nullopt;

  uint64_t offset = info->fieldOffsets[*fieldIdxOpt];
  const char *bytePtr = static_cast<const char *>(statePtr) + offset;
  return *reinterpret_cast<const int32_t *>(bytePtr);
}

std::optional<int32_t>
StrideEnvironment::getActiveStateId(const std::shared_ptr<void> &statePtr,
                                    const std::string &smName,
                                    const std::string &domainName) const {
  return getActiveStateId(statePtr.get(), smName, domainName);
}

bool StrideEnvironment::requestTransition(void *statePtr, int32_t transitionId,
                                          const std::string &smName,
                                          const std::string &domainName) {
  if (!statePtr) {
    return false;
  }
  const auto *info = findStructInfo(mStrideEnv, domainName);
  if (!info || !info->structType) {
    return false;
  }

  auto fieldIdxOpt = findFieldByFunctionality(
      *info, StateMachineField::TransitionRequestId, smName);
  if (!fieldIdxOpt || *fieldIdxOpt >= info->fieldOffsets.size()) {
    return false;
  }

  uint64_t offset = info->fieldOffsets[*fieldIdxOpt];
  char *bytePtr = static_cast<char *>(statePtr) + offset;
  *reinterpret_cast<int32_t *>(bytePtr) = transitionId;
  return true;
}

bool StrideEnvironment::requestTransition(const std::shared_ptr<void> &statePtr,
                                          int32_t transitionId,
                                          const std::string &smName,
                                          const std::string &domainName) {
  return requestTransition(statePtr.get(), transitionId, smName, domainName);
}

std::optional<int32_t>
StrideEnvironment::getTransitionRequestId(const void *statePtr,
                                          const std::string &smName,
                                          const std::string &domainName) const {
  if (!statePtr)
    return std::nullopt;
  const auto *info = findStructInfo(mStrideEnv, domainName);
  if (!info || !info->structType)
    return std::nullopt;

  auto fieldIdxOpt = findFieldByFunctionality(
      *info, StateMachineField::TransitionRequestId, smName);
  if (!fieldIdxOpt || *fieldIdxOpt >= info->fieldOffsets.size())
    return std::nullopt;

  uint64_t offset = info->fieldOffsets[*fieldIdxOpt];
  const char *bytePtr = static_cast<const char *>(statePtr) + offset;
  return *reinterpret_cast<const int32_t *>(bytePtr);
}

std::optional<int32_t>
StrideEnvironment::getTransitionRequestId(const std::shared_ptr<void> &statePtr,
                                          const std::string &smName,
                                          const std::string &domainName) const {
  return getTransitionRequestId(statePtr.get(), smName, domainName);
}

std::optional<int32_t> StrideEnvironment::getHistoryStateId(
    const void *statePtr, const std::string &stateName,
    const std::string &smName, const std::string &domainName) const {
  if (!statePtr)
    return std::nullopt;
  const auto *info = findStructInfo(mStrideEnv, domainName);
  if (!info || !info->structType)
    return std::nullopt;

  auto fieldIdxOpt = findFieldByFunctionality(
      *info, StateMachineField::HistoryStateId, stateName, smName);
  if (!fieldIdxOpt || *fieldIdxOpt >= info->fieldOffsets.size())
    return std::nullopt;

  uint64_t offset = info->fieldOffsets[*fieldIdxOpt];
  const char *bytePtr = static_cast<const char *>(statePtr) + offset;
  return *reinterpret_cast<const int32_t *>(bytePtr);
}

std::optional<int32_t> StrideEnvironment::getHistoryStateId(
    const std::shared_ptr<void> &statePtr, const std::string &stateName,
    const std::string &smName, const std::string &domainName) const {
  return getHistoryStateId(statePtr.get(), stateName, smName, domainName);
}

std::optional<int32_t>
StrideEnvironment::getStateId(const std::string &stateName,
                              const std::string &smName,
                              const std::string &domainName) const {
  for (const auto &smInfo : mStrideEnv.stateMachineInfos) {
    if (!domainName.empty() && smInfo.domainName != domainName)
      continue;
    if (!smName.empty() && smInfo.smName != smName)
      continue;
    auto it = smInfo.stateIdsByName.find(stateName);
    if (it != smInfo.stateIdsByName.end()) {
      return it->second;
    }
  }
  return std::nullopt;
}

std::optional<int32_t>
StrideEnvironment::getTransitionId(const std::string &transitionName,
                                   const std::string &smName,
                                   const std::string &domainName,
                                   const std::string &stateName) const {
  for (const auto &smInfo : mStrideEnv.stateMachineInfos) {
    if (!domainName.empty() && smInfo.domainName != domainName)
      continue;
    if (!smName.empty() && smInfo.smName != smName)
      continue;
    if (!stateName.empty()) {
      auto itState =
          smInfo.transitionIdsByStateAndName.find({stateName, transitionName});
      if (itState != smInfo.transitionIdsByStateAndName.end()) {
        return itState->second;
      }
    }
    auto it = smInfo.transitionIdsByName.find(transitionName);
    if (it != smInfo.transitionIdsByName.end()) {
      return it->second;
    }
  }
  return std::nullopt;
}

std::optional<int32_t>
StrideEnvironment::getTransitionIdForState(const std::string &transitionName,
                                           const std::string &stateName,
                                           const std::string &smName,
                                           const std::string &domainName) const {
  return getTransitionId(transitionName, smName, domainName, stateName);
}

static void fillTypeInfo(llvm::Type *ty, const llvm::DataLayout *DL,
                         DataType &outType, std::string &outTypeName,
                         size_t &outElemSize) {
  if (!ty) {
    outType = DataType::CUSTOM;
    outTypeName = "";
    outElemSize = 0;
    return;
  }
  if (ty->isDoubleTy()) {
    outType = DataType::DOUBLE;
    outTypeName = "_RealType";
    outElemSize = DL ? DL->getTypeAllocSize(ty) : 8;
  } else if (ty->isIntegerTy(32)) {
    outType = DataType::INT32;
    outTypeName = "_IntType";
    outElemSize = DL ? DL->getTypeAllocSize(ty) : 4;
  } else if (ty->isIntegerTy(1)) {
    outType = DataType::BOOL;
    outTypeName = "_SwitchType";
    outElemSize = DL ? DL->getTypeAllocSize(ty) : 1;
  } else if (ty->isIntegerTy(64)) {
    outType = DataType::INT64;
    outTypeName = "_IntType";
    outElemSize = DL ? DL->getTypeAllocSize(ty) : 8;
  } else if (ty->isFloatTy()) {
    outType = DataType::DOUBLE;
    outTypeName = "_RealType";
    outElemSize = DL ? DL->getTypeAllocSize(ty) : 4;
  } else if (ty->isStructTy()) {
    outType = DataType::STATE;
    outTypeName = ty->getStructName().str();
    outElemSize = DL ? DL->getTypeAllocSize(ty) : 0;
  } else {
    outType = DataType::CUSTOM;
    outTypeName = "";
    outElemSize = DL ? DL->getTypeAllocSize(ty) : 0;
  }
}

std::vector<FunctionArgInfo>
StrideEnvironment::getFunctionArgs(const std::string &funcName) const {
  std::vector<FunctionArgInfo> result;
  auto it = mStrideEnv.FunctionProtos.find(funcName);
  if (it == mStrideEnv.FunctionProtos.end()) {
    return result;
  }
  const auto *DL = getDataLayout();
  const auto &proto = it->second;

  assert(mStrideEnv.m_tree);
  auto funcDecl =
      ASTQuery::findDeclarationByName(funcName, {}, mStrideEnv.m_tree);
  assert(funcDecl);

  auto makeArgInfo = [&](const PrototypeArg &arg, FunctionArgInfo::Role role,
                         bool isPtr) {
    FunctionArgInfo info;
    info.name = arg.name;
    info.role = role;
    info.llvmType = arg.llvmType;
    info.isPointer = isPtr;
    // info.sizeProperty = prop.empty() ? arg.property : prop;

    fillTypeInfo(arg.llvmType, DL, info.type, info.typeName, info.elementSize);

    auto blockDecl = ASTQuery::findDeclarationByName(
        arg.name,
        {{funcDecl, funcDecl->getPropertyValue("blocks")->getChildren()}},
        nullptr);
    info.count = 1;
    if (blockDecl) {
      int sz = ASTQuery::getBlockDeclaredSize(blockDecl, {}, mStrideEnv.m_tree);
      if (sz > 0) {
        info.count = static_cast<size_t>(sz);
      } else {
        info.count = 0; // Undetermined dynamic size
        if (blockDecl->getNodeType() == AST::ArrayDeclaration ||
            blockDecl->getNodeType() == AST::BundleDeclaration) {
          // FIXME this should be calculated much earlier than here!
          auto bundle = blockDecl->getArrayIndex();
          if (bundle && bundle->index() &&
              !bundle->index()->getChildren().empty()) {
            auto firstIdx = bundle->index()->getChildren()[0];
            if (firstIdx->getNodeType() == AST::MemberAccess ||
                firstIdx->getNodeType() == AST::PortProperty) {
              auto pp = std::static_pointer_cast<MemberAccessNode>(firstIdx);
              info.sizeProperty = pp->getEntity() + "_" + pp->getPropertyName();
            }
          }
        }
      }
    }
    info.totalBytes = info.elementSize * info.count;
    return info;
  };

  for (const auto &arg : proto->getOutArgs()) {
    result.push_back(makeArgInfo(arg, FunctionArgInfo::Role::Output, true));
  }
  for (const auto &arg : proto->getInArgs()) {
    result.push_back(makeArgInfo(arg, FunctionArgInfo::Role::Input, true));
  }
  if (hasState(funcName)) {
    FunctionArgInfo info;
    info.name = "__state";
    info.role = FunctionArgInfo::Role::State;
    info.type = DataType::STATE;
    info.typeName = "struct." + funcName + "_state";
    info.isPointer = true;
    info.count = 1;
    info.elementSize = getStateSize(funcName);
    info.totalBytes = info.elementSize;

    const auto *stateInfo = mStrideEnv.getStateStructInfo(funcName);
    if (stateInfo && stateInfo->structType) {
      info.llvmType =
          llvm::PointerType::get(stateInfo->structType->getContext(), 0);
    } else if (mStrideEnv.TheContext) {
      info.llvmType = llvm::PointerType::get(*mStrideEnv.TheContext, 0);
    }
    result.push_back(info);
  }
  for (const auto &arg : proto->getExternalArgs()) {
    result.push_back(makeArgInfo(arg, FunctionArgInfo::Role::External, true));
  }
  for (const auto &arg : proto->getUsedPortProperties()) {
    result.push_back(
        makeArgInfo(arg, FunctionArgInfo::Role::PortProperty, false));
  }
  return result;
}

size_t
StrideEnvironment::getFunctionArgCount(const std::string &funcName) const {
  return getFunctionArgs(funcName).size();
}

std::optional<FunctionArgInfo>
StrideEnvironment::getFunctionArg(const std::string &funcName,
                                  size_t index) const {
  auto args = getFunctionArgs(funcName);
  if (index < args.size()) {
    return args[index];
  }
  return std::nullopt;
}

std::optional<FunctionArgInfo>
StrideEnvironment::getFunctionArg(const std::string &funcName,
                                  const std::string &argName) const {
  for (const auto &arg : getFunctionArgs(funcName)) {
    if (arg.name == argName) {
      return arg;
    }
  }
  return std::nullopt;
}

int StrideEnvironment::getFunctionArgIndex(const std::string &funcName,
                                           const std::string &argName) const {
  auto args = getFunctionArgs(funcName);
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i].name == argName) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

InvokerParameterList::InvokerParameterList(std::vector<FunctionArgInfo> args)
    : m_args(args.size(), nullptr), m_argInfos(std::move(args)) {
  for (size_t i = 0; i < m_argInfos.size(); ++i) {
    m_nameToIndex[m_argInfos[i].name] = i;
  }
}

bool InvokerParameterList::setArg(size_t index, void *ptr) {
  if (index >= m_args.size()) {
    return false;
  }
  const auto &info = m_argInfos[index];
  if (!info.sizeProperty.empty() &&
      m_nameToIndex.find(info.sizeProperty) != m_nameToIndex.end()) {
    return false; // MUST use setArrayArg
  }
  m_args[index] = ptr;
  return true;
}

bool InvokerParameterList::setArg(const std::string &name, void *ptr) {
  auto it = m_nameToIndex.find(name);
  if (it == m_nameToIndex.end()) {
    return false;
  }
  return setArg(it->second, ptr);
}

bool InvokerParameterList::setState(void *statePtr) {
  for (size_t i = 0; i < m_argInfos.size(); ++i) {
    if (m_argInfos[i].role == FunctionArgInfo::Role::State) {
      m_args[i] = statePtr;
      return true;
    }
  }
  return setArg("__state", statePtr);
}

bool InvokerParameterList::setProperty(const std::string &name, int32_t value) {
  auto it = m_nameToIndex.find(name);
  if (it == m_nameToIndex.end()) {
    return false;
  }
  // Store the value locally inside the FunctionArgInfo struct so its address is
  // stable
  m_argInfos[it->second].portPropertyValue = value;
  m_args[it->second] = &m_argInfos[it->second].portPropertyValue;
  return true;
}

bool InvokerParameterList::setArrayArg(const std::string &name, void *ptr,
                                       size_t size) {
  auto it = m_nameToIndex.find(name);
  if (it == m_nameToIndex.end()) {
    return false;
  }
  m_args[it->second] = ptr; // Bypass setArg validation

  const auto &propName = m_argInfos[it->second].sizeProperty;
  if (!propName.empty()) {
    return setProperty(propName, static_cast<int32_t>(size));
  }
  return true;
}

void *InvokerParameterList::getArg(size_t index) const {
  if (index < m_args.size()) {
    return m_args[index];
  }
  return nullptr;
}

void *InvokerParameterList::getArg(const std::string &name) const {
  auto it = m_nameToIndex.find(name);
  if (it != m_nameToIndex.end()) {
    return m_args[it->second];
  }
  return nullptr;
}

bool InvokerParameterList::isComplete() const {
  if (m_args.empty() && !m_argInfos.empty()) {
    return false;
  }
  for (void *ptr : m_args) {
    if (!ptr) {
      return false;
    }
  }
  return true;
}

const FunctionArgInfo *InvokerParameterList::getArgInfo(size_t index) const {
  if (index < m_argInfos.size()) {
    return &m_argInfos[index];
  }
  return nullptr;
}

const FunctionArgInfo *
InvokerParameterList::getArgInfo(const std::string &name) const {
  auto it = m_nameToIndex.find(name);
  if (it != m_nameToIndex.end()) {
    return &m_argInfos[it->second];
  }
  return nullptr;
}

InvokerParameterList
StrideEnvironment::createInvokerParamList(const std::string &funcName) const {
  return InvokerParameterList(getFunctionArgs(funcName));
}

int32_t StrideEnvironment::invoke(const std::string &funcName, void **args) {
  auto sym = getFunction(funcName + "_invoker");
  if (!sym) {
    auto err = sym.takeError();
    LOG_ERROR() << "Invoker for function not found: " << funcName + "_invoker"
                << ". Error: " << llvm::toString(std::move(err)) << std::endl;
    return -1;
  }
  auto *invoker = sym->toPtr<int32_t (*)(void **)>();
  return invoker(args);
}

int32_t StrideEnvironment::invoke(const std::string &funcName,
                                  InvokerParameterList &params) {
  return invoke(funcName, params.data());
}

bool StrideEnvironment::compileInMemory() {
  if (m_optimizeCode && !m_moduleOptimized) {
    optimizeModule();
  }
  initializeJIT();

  auto JTMB = llvm::orc::JITTargetMachineBuilder::detectHost();
  if (!JTMB) {
    LOG_ERROR() << " No machine builder" << std::endl;
    return false;
  }
  // JTMB->setCodeModel(llvm::CodeModel::Small);

  auto JIT_ =
      llvm::orc::LLJITBuilder()
          .setJITTargetMachineBuilder(std::move(*JTMB))
          //          .setObjectLinkingLayerCreator(
          //              [&](orc::ExecutionSession &ES, const Triple &TT) {
          //                  // Create ObjectLinkingLayer.
          //                  auto ObjLinkingLayer =
          //                  std::make_unique<orc::ObjectLinkingLayer>(
          //                      ES,
          // jitlink::InProcessMemoryManager::Create());
          //                  // Add an instance of our plugin.
          //// ObjLinkingLayer->addPlugin(std::make_unique<MyPlugin>());
          //                  return ObjLinkingLayer;
          //              })
          .create();
  if (!JIT_) {
    LOG_ERROR() << "JIT coulf not be created. Have you called initializeJIT()?"
                << std::endl;
    return false; // JIT.takeError();
  }
  JIT = std::move(*JIT_);
  auto librarySearch =
      llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
          JIT->getDataLayout().getGlobalPrefix());
  if (!librarySearch) {
    return false;
  }
  JIT->getMainJITDylib().addGenerator(std::move(*librarySearch));
  // TODO only load required libraries
  std::string err;
  if (!loadLibrary("m", err)) {
    LOG_ERROR() << "Failed to load m: " << err << std::endl;
  }
  // if (!loadLibrary("StrideLib", err)) {
  //   LOG_ERROR() << "Failed to load StrideLib: " << err << std::endl;
  // }
  if (!llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr, &err)) {
    LOG_ERROR() << "Failed to load current symbols: " << err << std::endl;
  }
  JIT->getMainJITDylib().addGenerator(llvm::cantFail(
      llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess('a')));
  llvm::orc::SymbolMap M;
  llvm::orc::MangleAndInterner Mangle(JIT->getExecutionSession(),
                                      JIT->getDataLayout());
  M[Mangle("__stride_Greater_d_dd")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&__stride_Greater_d_dd),
      llvm::JITSymbolFlags::Exported);
  llvm::cantFail(JIT->getMainJITDylib().define(llvm::orc::absoluteSymbols(M)));

  m_dataLayout = JIT->getDataLayout();
  TSCtx = llvm::orc::ThreadSafeContext(std::move(mStrideEnv.TheContext));
  if (auto Err = JIT->addIRModule(llvm::orc::ThreadSafeModule(
          std::move(mStrideEnv.TheModule), TSCtx))) {
    return false;
  }
  return true;
}

#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"

bool StrideEnvironment::compileObjectToDisk(std::string path) {
  if (m_optimizeCode && !m_moduleOptimized) {
    optimizeModule();
  }
  auto fspath = std::filesystem::path(path);
  if (!std::filesystem::exists(fspath)) {
    std::filesystem::create_directories(fspath);
  }
  { // Write header file
    auto headerFile = fspath.append("stride_include.h");
    std::ofstream f(headerFile.string());
    if (!f.good()) {
      LOG_ERROR() << "Could not create header." << std::endl;
      return false;
    }
    f << "// Auto generated by stridejit. Do not modify" << std::endl;
    f << "#pragma once" << std::endl << std::endl;
    for (const auto &domain : mStrideEnv.domainArgs) {
      f << "int"; // Return type
      f << " " << domain.first << "_process(";
      for (const auto &domainFunc : domain.second) {
        if (&domainFunc != &domain.second.front()) {
          f << ", ";
        }
        if (domainFunc.type == DataType::DOUBLE) {
          f << "double *";
        } else if (domainFunc.type == DataType::INT32) {
          f << "int *";
        } else if (domainFunc.type == DataType::BOOL) {
          f << "bool *";
          //        } else if (domainFunc.type == DataType::INT64) {
          //          f << "int64_t *";
        } else {
          f << "INVALID";
        }
        f << " " << domainFunc.name;
      }
      f << ");" << std::endl << std::endl;
    }
  }

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();

  // LLVMInitializeAArch64TargetInfo();
  // LLVMInitializeX86TargetInfo();
  // LLVMInitializeARMTargetInfo();
  // LLVMInitializeWebAssemblyTargetInfo();

  // LLVMInitializeAArch64Target();
  // LLVMInitializeX86Target();
  // LLVMInitializeARMTarget();
  // LLVMInitializeWebAssemblyTarget();

  // LLVMInitializeAArch64TargetMC();
  // LLVMInitializeX86TargetMC();
  // LLVMInitializeARMTargetMC();
  // LLVMInitializeWebAssemblyTargetMC();

  // LLVMInitializeAArch64AsmParser();
  // LLVMInitializeX86AsmParser();
  // LLVMInitializeARMAsmParser();
  // LLVMInitializeWebAssemblyAsmParser();

  // LLVMInitializeAArch64AsmPrinter();
  // LLVMInitializeX86AsmPrinter();
  // LLVMInitializeARMAsmPrinter();
  // LLVMInitializeWebAssemblyAsmPrinter();

  std::string stbuf;
  llvm::raw_string_ostream sstr(stbuf);
  llvm::TargetRegistry::printRegisteredTargetsForVersion(sstr);
  LOG_INFO() << stbuf << std::endl;
  for (auto target : llvm::TargetRegistry::targets()) {

    llvm::Triple TargetTriple;
    TargetTriple.setArchName(target.getName());
    TargetTriple.setVendorName("PC");
    TargetTriple.setOSName("Linux");
    TargetTriple.setEnvironmentName("GNU");

    std::string Error;
    auto Target = llvm::TargetRegistry::lookupTarget(target.getName(),
                                                     TargetTriple, Error);
    if (Target) {
      LOG_INFO() << Target->getName() << "   " << TargetTriple.getTriple()
                 << std::endl;
    }
  }

  auto TargetTriple = llvm::sys::getDefaultTargetTriple();

  if (!generateCompiledObject(path, TargetTriple)) {
    LOG_ERROR() << "Error creating object file" << std::endl;
    return false;
  }

  TargetTriple = "wasm32-wasi";
  if (!generateCompiledObject(path, TargetTriple)) {
    LOG_ERROR() << "Error creating object file" << std::endl;
    return false;
  }

  TargetTriple = "arm-none-eabi";
  if (!generateCompiledObject(path, TargetTriple)) {
    LOG_ERROR() << "Error creating object file" << std::endl;
    return false;
  }

  // TargetTriple = "riscv32-unknown-elf";
  // if (!generateCompiledObject(path, TargetTriple)) {
  //   LOG_ERROR() << "Error creating object file" << std::endl;
  //   return false;
  // }

  return true;
}

bool StrideEnvironment::generateCompiledObject(std::string path,
                                               std::string TargetTriple) {
  auto CPU = "generic";
  auto Features = "";
  std::string Error;
  llvm::Triple triple(TargetTriple);
#if LLVM_VERSION_MAJOR >= 20
  auto Target = llvm::TargetRegistry::lookupTarget(triple, Error);
#else
  auto Target = llvm::TargetRegistry::lookupTarget(triple.str(), Error);
#endif
  // Print an error and exit if we couldn't find the requested target.
  // This generally occurs if we've forgotten to initialise the
  // TargetRegistry or we have a bogus target triple.
  if (!Target) {
    LOG_ERROR() << Error;
    return false;
  }
  llvm::TargetOptions opt;
  auto RM = std::optional<llvm::Reloc::Model>();
#if LLVM_VERSION_MAJOR >= 20
  auto TargetMachine =
      Target->createTargetMachine(triple, CPU, Features, opt, RM, std::nullopt,
                                  llvm::CodeGenOptLevel::Default);
  mStrideEnv.TheModule->setDataLayout(TargetMachine->createDataLayout());
  mStrideEnv.TheModule->setTargetTriple(triple);
#else
  auto TargetMachine =
      Target->createTargetMachine(triple.str(), CPU, Features, opt, RM, std::nullopt,
                                  llvm::CodeGenOptLevel::Default);
  mStrideEnv.TheModule->setDataLayout(TargetMachine->createDataLayout());
  mStrideEnv.TheModule->setTargetTriple(triple.str());
#endif

  auto fspath = std::filesystem::path(path);
  fspath.append(TargetTriple + "/");
  std::filesystem::create_directories(fspath);
  fspath.replace_filename("output.o");
  std::string Filename = fspath.string();
  std::error_code EC;
  llvm::raw_fd_ostream dest(Filename.c_str(), EC, llvm::sys::fs::OF_None);

  if (EC) {
    LOG_ERROR() << "Could not open file: " << EC.message();
    return false;
  }
  llvm::legacy::PassManager pass;
  auto FileType = llvm::CodeGenFileType::ObjectFile;

  if (TargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    LOG_ERROR() << "TargetMachine can't emit a file of this type";
    return false;
  }

  pass.run(*mStrideEnv.TheModule);
  dest.flush();
  return true;
}

bool StrideEnvironment::loadLibrary(const char *libName, std::string &err) {
  // TODO Do a proper comprehensive search for libs and fix for other systems
  // On windows, just using the librery name seems to work, no need to add
  // path.
  std::vector<std::string> libPath = {"/lib/x86_64-linux-gnu/",
                                      "/usr/lib/x86_64-linux-gnu/"};
  for (const auto &path : libPath) {
#ifdef WIN32
    std::string libToLoad = libName;
#else
    //    auto libToLoad = path + "/lib" + libName + ".so";
    //    if (!std::filesystem::exists(libToLoad)) {
    //      continue;
    //    }
    //    if (std::filesystem::is_symlink(libToLoad)) {
    //      libToLoad = std::filesystem::read_symlink(libToLoad).string();
    //    }

    std::string libToLoad = libName;
    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(libToLoad.c_str(),
                                                          &err)) {
      LOG_INFO() << " Loaded lib: " << libToLoad << std::endl;
      return true;
    }
    LOG_INFO() << " Trying: " << libToLoad << std::endl;
#endif
    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(libToLoad.c_str(),
                                                          &err)) {
      LOG_INFO() << " Loaded lib: " << libToLoad << std::endl;
      return true;
    }
  }
  return false;
}

llvm::Expected<llvm::orc::ExecutorAddr>
StrideEnvironment::getFunction(std::string functionName) {
  if (!JIT) {
    return llvm::make_error<llvm::StringError>("JIT not available",
                                               llvm::inconvertibleErrorCode());
    ;
  }
  auto EntrySym = JIT->lookup(functionName.c_str());
  return EntrySym;
}

template std::optional<int32_t>
StrideEnvironment::getStateVar<int32_t>(const void *, const std::string &,
                                        std::optional<int>,
                                        const std::string &) const;
template std::optional<double>
StrideEnvironment::getStateVar<double>(const void *, const std::string &,
                                       std::optional<int>,
                                       const std::string &) const;

template bool StrideEnvironment::setStateVar<int32_t>(void *,
                                                      const std::string &,
                                                      int32_t,
                                                      std::optional<int>,
                                                      const std::string &);
template bool StrideEnvironment::setStateVar<double>(void *,
                                                     const std::string &,
                                                     double, std::optional<int>,
                                                     const std::string &);

template std::optional<bool>
StrideEnvironment::getStateVar<bool>(const void *, const std::string &,
                                     std::optional<int>,
                                     const std::string &) const;
template bool StrideEnvironment::setStateVar<bool>(void *, const std::string &,
                                                   bool, std::optional<int>,
                                                   const std::string &);

template std::optional<float>
StrideEnvironment::getStateVar<float>(const void *, const std::string &,
                                      std::optional<int>,
                                      const std::string &) const;
template bool StrideEnvironment::setStateVar<float>(void *, const std::string &,
                                                    float, std::optional<int>,
                                                    const std::string &);

template std::optional<int64_t>
StrideEnvironment::getStateVar<int64_t>(const void *, const std::string &,
                                        std::optional<int>,
                                        const std::string &) const;
template bool StrideEnvironment::setStateVar<int64_t>(void *,
                                                      const std::string &,
                                                      int64_t,
                                                      std::optional<int>,
                                                      const std::string &);

template std::optional<uint32_t>
StrideEnvironment::getStateVar<uint32_t>(const void *, const std::string &,
                                         std::optional<int>,
                                         const std::string &) const;
template bool StrideEnvironment::setStateVar<uint32_t>(void *,
                                                       const std::string &,
                                                       uint32_t,
                                                       std::optional<int>,
                                                       const std::string &);

bool StrideEnvironment::emitObjectFile(const std::string &outputPath,
                                       const std::string &targetTriple,
                                       const std::string &cpu,
                                       const std::string &features,
                                       const std::string &relocModel,
                                       const std::string &codeModel) {
  if (m_optimizeCode && !m_moduleOptimized) {
    optimizeModule();
  }
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllAsmPrinters();

  std::string tripleStr =
      targetTriple.empty() ? llvm::sys::getDefaultTargetTriple() : targetTriple;
  llvm::Triple theTriple(tripleStr);

  std::string error;
#if LLVM_VERSION_MAJOR >= 20
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(theTriple, error);
#else
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(theTriple.str(), error);
#endif
  if (!target) {
    LOG_ERROR() << "Unable to lookup LLVM target for triple '" << tripleStr
                << "': " << error << std::endl;
    return false;
  }

  std::string targetCPU = cpu.empty() ? "generic" : cpu;
  std::string targetFeatures = features;

  llvm::TargetOptions opt;
  std::optional<llvm::Reloc::Model> rm;
  if (relocModel == "static") {
    rm = llvm::Reloc::Static;
  } else if (relocModel == "pic") {
    rm = llvm::Reloc::PIC_;
  } else if (relocModel == "dynamic-no-pic") {
    rm = llvm::Reloc::DynamicNoPIC;
  } else if (relocModel == "ropi") {
    rm = llvm::Reloc::ROPI;
  } else if (relocModel == "rwpi") {
    rm = llvm::Reloc::RWPI;
  } else if (relocModel == "ropi-rwpi") {
    rm = llvm::Reloc::ROPI_RWPI;
  }

  std::optional<llvm::CodeModel::Model> cm;
  if (codeModel == "tiny") {
    cm = llvm::CodeModel::Tiny;
  } else if (codeModel == "small") {
    cm = llvm::CodeModel::Small;
  } else if (codeModel == "kernel") {
    cm = llvm::CodeModel::Kernel;
  } else if (codeModel == "medium") {
    cm = llvm::CodeModel::Medium;
  } else if (codeModel == "large") {
    cm = llvm::CodeModel::Large;
  }

#if LLVM_VERSION_MAJOR >= 20
  auto targetMachine =
      target->createTargetMachine(theTriple, targetCPU, targetFeatures, opt, rm,
                                  cm, llvm::CodeGenOptLevel::Default);
#else
  auto targetMachine =
      target->createTargetMachine(theTriple.str(), targetCPU, targetFeatures, opt, rm,
                                  cm, llvm::CodeGenOptLevel::Default);
#endif

  if (!targetMachine) {
    LOG_ERROR() << "Could not create TargetMachine for triple " << tripleStr
                << std::endl;
    return false;
  }

  mStrideEnv.TheModule->setDataLayout(targetMachine->createDataLayout());
#if LLVM_VERSION_MAJOR >= 20
  mStrideEnv.TheModule->setTargetTriple(theTriple);
#else
  mStrideEnv.TheModule->setTargetTriple(theTriple.str());
#endif

  std::filesystem::path outPath(outputPath);
  if (outPath.has_parent_path()) {
    std::filesystem::create_directories(outPath.parent_path());
  }

  std::error_code ec;
  llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
  if (ec) {
    LOG_ERROR() << "Could not open output object file: " << ec.message()
                << std::endl;
    return false;
  }

  llvm::legacy::PassManager pass;
  auto fileType = llvm::CodeGenFileType::ObjectFile;

  if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
    LOG_ERROR() << "TargetMachine cannot emit an object file for " << tripleStr
                << std::endl;
    return false;
  }

  pass.run(*mStrideEnv.TheModule);
  dest.flush();
  return true;
}

static std::string toUpperSnake(const std::string &str) {
  std::string result;
  for (size_t i = 0; i < str.size(); ++i) {
    char c = str[i];
    if (std::isupper(static_cast<unsigned char>(c)) && i > 0 &&
        !std::isupper(static_cast<unsigned char>(str[i - 1])) &&
        str[i - 1] != '_') {
      result += '_';
    }
    result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return result;
}

std::string
StrideEnvironment::generateCHeaderString(const std::string &domainName) const {
  std::ostringstream ss;
  std::string guardBase =
      domainName.empty() ? "STRIDE_GENERATED" : toUpperSnake(domainName);
  std::string guard = guardBase + "_H";

  ss << "/* Auto-generated by Stride Compiler - Do Not Edit */\n";
  ss << "#ifndef " << guard << "\n";
  ss << "#define " << guard << "\n\n";
  ss << "#include <stdint.h>\n";
  ss << "#include <stdbool.h>\n";
  ss << "#include <stddef.h>\n\n";
  ss << "#ifdef __cplusplus\n";
  ss << "extern \"C\" {\n";
  ss << "#endif\n\n";

  // Collect target domain names
  std::vector<std::string> domains;
  if (!domainName.empty()) {
    domains.push_back(domainName);
  } else {
    for (const auto &t : m_trees) {
      if (!t) continue;
      for (const auto &node : t->getChildren()) {
        if (node->getNodeType() == AST::Declaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(node);
          std::string objType = decl->getObjectType();
          std::string entType = decl->getEntityType();
          if (objType == "_domainDefinition" || entType == "_domainDefinition" ||
              objType == "domain" || entType == "domain") {
            if (std::find(domains.begin(), domains.end(), decl->getName()) ==
                domains.end()) {
              domains.push_back(decl->getName());
            }
          }
        }
      }
    }
    if (mStrideEnv.m_tree) {
      for (const auto &node : mStrideEnv.m_tree->getChildren()) {
        if (node->getNodeType() == AST::Declaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(node);
          std::string objType = decl->getObjectType();
          std::string entType = decl->getEntityType();
          if (objType == "_domainDefinition" || entType == "_domainDefinition" ||
              objType == "domain" || entType == "domain") {
            if (std::find(domains.begin(), domains.end(), decl->getName()) ==
                domains.end()) {
              domains.push_back(decl->getName());
            }
          }
        }
      }
    }
    // Fallback: search function protos for <domain>_process
    for (const auto &protoPair : mStrideEnv.FunctionProtos) {
      const std::string &fname = protoPair.first;
      if (fname.find("_invoker") != std::string::npos)
        continue;
      size_t pos = fname.find("_process");
      if (pos != std::string::npos) {
        std::string dom = fname.substr(0, pos);
        if (std::find(domains.begin(), domains.end(), dom) == domains.end()) {
          domains.push_back(dom);
        }
      }
    }
    // Fallback 2: search LLVM Module for any <domain>_process
    if (mStrideEnv.TheModule) {
      for (const auto &F : mStrideEnv.TheModule->functions()) {
        std::string fname = F.getName().str();
        if (fname.find("_invoker") != std::string::npos || fname.rfind("llvm.", 0) == 0)
          continue;
        size_t pos = fname.find("_process");
        if (pos != std::string::npos) {
          std::string dom = fname.substr(0, pos);
          if (std::find(domains.begin(), domains.end(), dom) == domains.end()) {
            domains.push_back(dom);
          }
        }
      }
    }
  }

  for (const auto &dom : domains) {
    std::string initFunc = dom + "_init";
    std::string processFunc = dom + "_process";

    std::shared_ptr<DeclarationNode> domDecl;
    for (const auto &t : m_trees) {
      if (!t) continue;
      domDecl = ASTQuery::findDeclarationByName(dom, {}, t);
      if (domDecl) break;
    }
    if (!domDecl && mStrideEnv.m_tree) {
      domDecl = ASTQuery::findDeclarationByName(dom, {}, mStrideEnv.m_tree);
    }

    std::set<std::string> outputNames;
    if (domDecl) {
      if (auto outProp = domDecl->getPropertyValue("outputs")) {
        for (const auto &child : outProp->getChildren()) {
          if (child->getNodeType() == AST::Declaration) {
            outputNames.insert(
                std::static_pointer_cast<DeclarationNode>(child)->getName());
          }
        }
      }
    }

    // 1. Emit init function prototype
    auto initIt = mStrideEnv.FunctionProtos.find(initFunc);
    auto *initLlvm = mStrideEnv.TheModule ? mStrideEnv.TheModule->getFunction(initFunc) : nullptr;
    if (initIt != mStrideEnv.FunctionProtos.end()) {
      ss << "/**\n";
      ss << " * @brief Initializes or resets the " << dom << " domain state.\n";
      ss << " */\n";

      const auto &proto = initIt->second;
      auto extArgs = proto->getExternalArgs();
      auto inArgs = proto->getInArgs();
      auto outArgs = proto->getOutArgs();

      if (extArgs.empty() && inArgs.empty() && outArgs.empty()) {
        ss << "void " << initFunc << "(void);\n\n";
      } else {
        ss << "void " << initFunc << "(";
        bool first = true;
        for (const auto &arg : extArgs) {
          if (!first)
            ss << ", ";
          first = false;
          std::string typeName = "double";
          if (arg.llvmType) {
            if (arg.llvmType->isIntegerTy(1))
              typeName = "bool";
            else if (arg.llvmType->isIntegerTy(32))
              typeName = "int32_t";
            else if (arg.llvmType->isIntegerTy(64))
              typeName = "int64_t";
            else if (arg.llvmType->isFloatTy())
              typeName = "float";
            else if (arg.llvmType->isDoubleTy())
              typeName = "double";
          }
          ss << typeName << "* " << arg.name;
        }
        ss << ");\n\n";
      }
    } else if (initLlvm) {
      ss << "/**\n";
      ss << " * @brief Initializes or resets the " << dom << " domain state.\n";
      ss << " */\n";
      if (initLlvm->arg_empty()) {
        ss << "void " << initFunc << "(void);\n\n";
      } else {
        ss << "void " << initFunc << "(";
        bool first = true;
        for (const auto &arg : initLlvm->args()) {
          if (!first) ss << ", ";
          first = false;
          ss << "void* " << arg.getName().str();
        }
        ss << ");\n\n";
      }
    }

    // 2. Emit process function prototype
    auto procIt = mStrideEnv.FunctionProtos.find(processFunc);
    auto *procLlvm = mStrideEnv.TheModule ? mStrideEnv.TheModule->getFunction(processFunc) : nullptr;
    if (procIt != mStrideEnv.FunctionProtos.end()) {
      const auto &proto = procIt->second;
      auto extArgs = proto->getExternalArgs();

      ss << "/**\n";
      ss << " * @brief Executes one step of the " << dom << " domain.\n";
      for (const auto &arg : extArgs) {
        bool isOut = (outputNames.count(arg.name) > 0);
        ss << " * @param[" << (isOut ? "out" : "in") << "] " << arg.name
           << "\n";
      }
      ss << " */\n";

      ss << "void " << processFunc << "(";
      if (extArgs.empty()) {
        ss << "void";
      } else {
        bool first = true;
        for (const auto &arg : extArgs) {
          if (!first)
            ss << ", ";
          first = false;
          bool isOut = (outputNames.count(arg.name) > 0);
          std::string typeName = "double";
          if (arg.llvmType) {
            if (arg.llvmType->isIntegerTy(1))
              typeName = "bool";
            else if (arg.llvmType->isIntegerTy(32))
              typeName = "int32_t";
            else if (arg.llvmType->isIntegerTy(64))
              typeName = "int64_t";
            else if (arg.llvmType->isFloatTy())
              typeName = "float";
            else if (arg.llvmType->isDoubleTy())
              typeName = "double";
          }
          if (isOut) {
            ss << typeName << "* " << arg.name;
          } else {
            ss << "const " << typeName << "* " << arg.name;
          }
        }
      }
      ss << ");\n\n";
    } else if (procLlvm) {
      ss << "/**\n";
      ss << " * @brief Executes one step of the " << dom << " domain.\n";
      for (const auto &arg : procLlvm->args()) {
        std::string argName = arg.getName().str();
        bool isOut = (outputNames.count(argName) > 0);
        ss << " * @param[" << (isOut ? "out" : "in") << "] " << argName << "\n";
      }
      ss << " */\n";

      ss << "void " << processFunc << "(";
      if (procLlvm->arg_empty()) {
        ss << "void";
      } else {
        bool first = true;
        for (const auto &arg : procLlvm->args()) {
          if (!first) ss << ", ";
          first = false;
          std::string argName = arg.getName().str();
          bool isOut = (outputNames.count(argName) > 0);
          std::string typeName = "double";
          if (isOut) {
            ss << typeName << "* " << argName;
          } else {
            ss << "const " << typeName << "* " << argName;
          }
        }
      }
      ss << ");\n\n";
    }
  }

  // Standalone functions if any
  for (const auto &protoPair : mStrideEnv.FunctionProtos) {
    const std::string &fname = protoPair.first;
    if (fname.find("_init") != std::string::npos ||
        fname.find("_process") != std::string::npos ||
        fname.find("_invoker") != std::string::npos) {
      continue;
    }
    const auto &proto = protoPair.second;
    ss << "/**\n";
    ss << " * @brief Standalone function: " << fname << "\n";
    ss << " */\n";
    ss << "void " << fname << "(";
    bool first = true;
    for (const auto &arg : proto->getOutArgs()) {
      if (!first)
        ss << ", ";
      first = false;
      std::string typeName = "double";
      if (arg.llvmType) {
        if (arg.llvmType->isIntegerTy(1))
          typeName = "bool";
        else if (arg.llvmType->isIntegerTy(32))
          typeName = "int32_t";
        else if (arg.llvmType->isIntegerTy(64))
          typeName = "int64_t";
        else if (arg.llvmType->isFloatTy())
          typeName = "float";
        else if (arg.llvmType->isDoubleTy())
          typeName = "double";
      }
      ss << typeName << "* " << arg.name;
    }
    for (const auto &arg : proto->getInArgs()) {
      if (!first)
        ss << ", ";
      first = false;
      std::string typeName = "double";
      if (arg.llvmType) {
        if (arg.llvmType->isIntegerTy(1))
          typeName = "bool";
        else if (arg.llvmType->isIntegerTy(32))
          typeName = "int32_t";
        else if (arg.llvmType->isIntegerTy(64))
          typeName = "int64_t";
        else if (arg.llvmType->isFloatTy())
          typeName = "float";
        else if (arg.llvmType->isDoubleTy())
          typeName = "double";
      }
      ss << "const " << typeName << "* " << arg.name;
    }
    for (const auto &arg : proto->getPropertyArgs()) {
      if (!first)
        ss << ", ";
      first = false;
      std::string typeName = "double";
      if (arg.llvmType) {
        if (arg.llvmType->isIntegerTy(1))
          typeName = "bool";
        else if (arg.llvmType->isIntegerTy(32))
          typeName = "int32_t";
        else if (arg.llvmType->isIntegerTy(64))
          typeName = "int64_t";
        else if (arg.llvmType->isFloatTy())
          typeName = "float";
        else if (arg.llvmType->isDoubleTy())
          typeName = "double";
      }
      ss << "const " << typeName << "* " << arg.name;
    }
    for (const auto &arg : proto->getInternalArgs()) {
      if (!first)
        ss << ", ";
      first = false;
      ss << "void* " << arg.name;
    }
    for (const auto &arg : proto->getExternalArgs()) {
      if (!first)
        ss << ", ";
      first = false;
      std::string typeName = "double";
      if (arg.llvmType) {
        if (arg.llvmType->isIntegerTy(1))
          typeName = "bool";
        else if (arg.llvmType->isIntegerTy(32))
          typeName = "int32_t";
        else if (arg.llvmType->isIntegerTy(64))
          typeName = "int64_t";
        else if (arg.llvmType->isFloatTy())
          typeName = "float";
        else if (arg.llvmType->isDoubleTy())
          typeName = "double";
      }
      ss << typeName << "* " << arg.name;
    }
    for (const auto &arg : proto->getUsedPortProperties()) {
      if (!first)
        ss << ", ";
      first = false;
      std::string typeName = "double";
      if (arg.llvmType) {
        if (arg.llvmType->isIntegerTy(1))
          typeName = "bool";
        else if (arg.llvmType->isIntegerTy(32))
          typeName = "int32_t";
        else if (arg.llvmType->isIntegerTy(64))
          typeName = "int64_t";
        else if (arg.llvmType->isFloatTy())
          typeName = "float";
        else if (arg.llvmType->isDoubleTy())
          typeName = "double";
      }
      ss << typeName << " " << arg.name;
    }
    if (first) {
      ss << "void";
    }
    ss << ");\n\n";
  }

  ss << "#ifdef __cplusplus\n";
  ss << "}\n";
  ss << "#endif\n\n";
  ss << "#endif /* " << guard << " */\n";

  return ss.str();
}

bool StrideEnvironment::emitCHeader(const std::string &outputPath,
                                    const std::string &domainName) const {
  std::string content = generateCHeaderString(domainName);
  if (content.empty()) {
    return false;
  }
  std::filesystem::path outPath(outputPath);
  if (outPath.has_parent_path()) {
    std::filesystem::create_directories(outPath.parent_path());
  }
  std::ofstream outFile(outputPath);
  if (!outFile.is_open()) {
    LOG_ERROR() << "Could not open C header file for writing: " << outputPath
                << std::endl;
    return false;
  }
  outFile << content;
  outFile.close();
  return true;
}
