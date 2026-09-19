#include <filesystem>
#include "stride/utils/logger.h"
#include <fstream>
#include <iostream>

// stride
#include "stride/codegen/coderesolver.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

// stridejit
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// llvm
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
// #include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
// #include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
// #include "llvm/ExecutionEngine/Orc/EPCIndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
// #include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
// #include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
// #include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
// #include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
// #include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"

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
}

void StrideEnvironment::prepareTree(ASTNode tree) {
  auto systemNodes = ASTQuery::getSystemNodes(tree);
  if (systemNodes.size() == 0) {
    auto systemNode =
        std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__);
    tree->addChild(systemNode);
  }

  CodeResolver resolver(tree, ASTFunctions::getDefaultStrideRoot());
  resolver.process();
}

bool StrideEnvironment::generateIr(std::string path) {
  ASTNode tree;
  tree = AST::parseFile(path.c_str());
  if (!tree) {
    for (auto &error : AST::getParseErrors()) {
      LOG_ERROR() << error.getErrorText() << std::endl;
    }
    return false;
  }

  prepareTree(tree);
  return generateIr(tree);
}

bool StrideEnvironment::generateIr(ASTNode root) {
  ScopeStack globalScope;
  {
    //    StrideLibrary library;
    //    library.initializeLibrary(m_strideRoot);

    globalScope.push_back({nullptr, {}});
    std::vector<ASTNode> platformlib = ASTFunctions::loadAllInDirectory(
        m_strideRoot + "/frameworks/JIT/1.0/platformlib");
    auto &frameworkScope = globalScope.back().second;

    for (const auto &member : platformlib) {
      if (member->getNodeType() == AST::Declaration ||
          member->getNodeType() == AST::BundleDeclaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(member);
        if (decl->getObjectType() == "platformModule") {
          StrideGenerator::generatePlatformFunctionSignature(
              decl, frameworkScope, state);
        }
      }
    }
  }

  if (!ASTFunctions::preprocess(root, &globalScope)) {
    return false;
  }
  StrideGenerator::compile(root, globalScope, state);
  //  if (mVerbose) {
  //    state.TheModule->print(llvm::outs(), nullptr);
  //    llvm::outs() << "\n";
  //  }

  optimizeModule();
  return true;
}

void StrideEnvironment::optimizeModule() {
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

    MPM.run(*state.TheModule, MAM);
#else
    // Legacy Pass Manager
    std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
    TheFPM = std::make_unique<llvm::legacy::FunctionPassManager>(
        state.TheModule.get());

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

    for (auto &F : *state.TheModule) {
      TheFPM->run(F);
    }
#endif
  }
  if (m_verbose) {
    state.TheModule->print(llvm::outs(), nullptr);
    llvm::outs() << "\n";
  }
}


bool StrideEnvironment::generateStandaloneFunction(std::string funcName,
                                                   ScopeStack &scope,
                                                   ASTNode tree) {
  auto func =
      StrideGenerator::generateStandaloneFunction(funcName, tree, scope, state);
  if (!func) {
    return false;
  }

  optimizeModule();
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
  if (state.TheModule) {
    m_dataLayout = state.TheModule->getDataLayout();
    return &*m_dataLayout;
  }
  return nullptr;
}

void *StrideEnvironment::allocateState(const std::string &funcName) {
  const auto *info = state.getStateStructInfo(funcName);
  if (!info || !info->structType) {
    return nullptr;
  }
  const auto *DL = getDataLayout();
  if (!DL) {
    return nullptr;
  }
  size_t allocSize = DL->getTypeAllocSize(info->structType);
  if (allocSize == 0) {
    return nullptr;
  }
  void *mem = malloc(allocSize);
  if (!mem) {
    return nullptr;
  }
  memset(mem, 0, allocSize);
  if (info->defaultConstant) {
    writeConstantToBuffer(info->defaultConstant, static_cast<char *>(mem), *DL);
  }
  return mem;
}

void StrideEnvironment::deallocateState(void *statePtr) {
  if (statePtr) {
    free(statePtr);
  }
}

bool StrideEnvironment::hasState(const std::string &funcName) const {
  const auto *info = state.getStateStructInfo(funcName);
  return info && info->structType != nullptr;
}

size_t StrideEnvironment::getStateSize(const std::string &funcName) const {
  const auto *info = state.getStateStructInfo(funcName);
  if (!info || !info->structType) {
    return 0;
  }
  const auto *DL = getDataLayout();
  if (!DL) {
    return 0;
  }
  return DL->getTypeAllocSize(info->structType);
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
  auto it = state.FunctionProtos.find(funcName);
  if (it == state.FunctionProtos.end()) {
    return result;
  }
  const auto *DL = getDataLayout();
  const auto &proto = it->second;

  auto makeArgInfo = [&](const PrototypeArg &arg, FunctionArgInfo::Role role,
                         bool isPtr, const std::string &prop = "") {
    FunctionArgInfo info;
    info.name = arg.name;
    info.role = role;
    info.llvmType = arg.llvmType;
    info.isPointer = isPtr;
    info.property = prop.empty() ? arg.property : prop;

    fillTypeInfo(arg.llvmType, DL, info.type, info.typeName, info.elementSize);

    info.count = 1;
    if (state.m_tree) {
      auto decl = ASTQuery::findDeclarationByName(arg.name, {}, state.m_tree);
      if (decl) {
        int sz = ASTQuery::getBlockDeclaredSize(decl, {}, state.m_tree);
        if (sz > 0) {
          info.count = static_cast<size_t>(sz);
        } else if (!info.property.empty()) {
          info.count = 0; // Undetermined dynamic size
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

    const auto *stateInfo = state.getStateStructInfo(funcName);
    if (stateInfo && stateInfo->structType) {
      info.llvmType =
          llvm::PointerType::get(stateInfo->structType->getContext(), 0);
    } else if (state.TheContext) {
      info.llvmType = llvm::PointerType::get(*state.TheContext, 0);
    }
    result.push_back(info);
  }
  for (const auto &arg : proto->getExternalArgs()) {
    result.push_back(makeArgInfo(arg, FunctionArgInfo::Role::External, true));
  }
  for (const auto &arg : proto->getUsedPortProperties()) {
    result.push_back(makeArgInfo(arg, FunctionArgInfo::Role::PortProperty, false,
                                 arg.property));
  }
  return result;
}

size_t StrideEnvironment::getFunctionArgCount(const std::string &funcName) const {
  return getFunctionArgs(funcName).size();
}

std::optional<FunctionArgInfo>
StrideEnvironment::getFunctionArg(const std::string &funcName, size_t index) const {
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
  if (!info.property.empty() && m_nameToIndex.find(info.property) != m_nameToIndex.end()) {
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
  // Store the value locally inside the FunctionArgInfo struct so its address is stable
  m_argInfos[it->second].portPropertyValue = value;
  m_args[it->second] = &m_argInfos[it->second].portPropertyValue;
  return true;
}

bool InvokerParameterList::setArrayArg(const std::string &name, void *ptr, size_t size) {
  auto it = m_nameToIndex.find(name);
  if (it == m_nameToIndex.end()) {
    return false;
  }
  m_args[it->second] = ptr; // Bypass setArg validation
  
  const auto &propName = m_argInfos[it->second].property;
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
    llvm::consumeError(sym.takeError());
    LOG_ERROR() << "Invoker for function not found: " << funcName + "_invoker"
              << std::endl;
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
  TSCtx = llvm::orc::ThreadSafeContext(std::move(state.TheContext));
  if (auto Err = JIT->addIRModule(
          llvm::orc::ThreadSafeModule(std::move(state.TheModule), TSCtx))) {
    return false;
  }
  return true;
}

#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"

bool StrideEnvironment::compileObjectToDisk(std::string path) {
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
    for (const auto &domain : state.domainArgs) {
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

  return true;
}

bool StrideEnvironment::generateCompiledObject(std::string path,
                                               std::string TargetTriple) {
  auto CPU = "generic";
  auto Features = "";
  std::string Error;
  auto Target = llvm::TargetRegistry::lookupTarget(TargetTriple, Error);
  // Print an error and exit if we couldn't find the requested target.
  // This generally occurs if we've forgotten to initialise the
  // TargetRegistry or we have a bogus target triple.
  if (!Target) {
    LOG_ERROR() << Error;
    return false;
  }
  llvm::TargetOptions opt;
  auto RM = std::optional<llvm::Reloc::Model>();
  auto TargetMachine = Target->createTargetMachine(
      llvm::Triple(TargetTriple), CPU, Features, opt, RM,
      llvm::CodeModel::Large, // 👈 Force Large Code Model here
      llvm::CodeGenOptLevel::Default);
  state.TheModule->setDataLayout(TargetMachine->createDataLayout());
  state.TheModule->setTargetTriple(llvm::Triple(TargetTriple));

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

  pass.run(*state.TheModule);
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
