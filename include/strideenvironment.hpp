#ifndef STRIDEENVIRONMENT_HPP
#define STRIDEENVIRONMENT_HPP

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include <map>

#include "functionast.hpp"

class JitState {
public:
  JitState() {
    TheContext = std::make_unique<llvm::LLVMContext>();
    TheModule = std::make_unique<llvm::Module>("StrideJit", *this->TheContext);
    Builder = std::make_unique<llvm::IRBuilder<>>(*this->TheContext);

    BinopPrecedence['='] = 2;
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40; // highest.

    TheFPM =
        std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());

    // Do simple "peephole" optimizations and bit-twiddling optzns.
    TheFPM->add(llvm::createInstructionCombiningPass());
    // Reassociate expressions.
    TheFPM->add(llvm::createReassociatePass());
    // Eliminate Common SubExpressions.
    TheFPM->add(llvm::createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    TheFPM->add(llvm::createCFGSimplificationPass());

    TheFPM->doInitialization();
  }

  llvm::Function *getFunction(std::string Name) {
    // First, see if the function has already been added to the current module.
    if (auto *F = TheModule->getFunction(Name))
      return F;
    // If not, check whether we can codegen the declaration from some existing
    // prototype.
    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end())
      return FI->second->codegen(*this);
    // If no existing prototype exists, return null.
    return nullptr;
  }

  llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction,
                                           llvm::StringRef VarName) {
    llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                           TheFunction->getEntryBlock().begin());
    return TmpB.CreateAlloca(llvm::Type::getDoubleTy(*TheContext), nullptr,
                             VarName);
  }

  std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
  }

  llvm::Value *LogErrorV(const char *Str) {
    LogError(Str);
    return nullptr;
  }

  std::unique_ptr<llvm::LLVMContext> TheContext;
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::unique_ptr<llvm::Module> TheModule;
  std::map<std::string, llvm::Value *> NamedValues;
  std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
  std::map<char, int> BinopPrecedence;

  std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
};

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"

#include <iostream>

#include "astfunctions.h"
#include "strideparser.h"

class StrideEnvironment {
public:
  StrideEnvironment();

  bool compile(std::string path);

  JitState state;
  std::unique_ptr<llvm::orc::LLJIT> JIT;
  bool mVerbose{true};
};

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/EPCIndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

/// This will compile FnAST to IR, rename the function to add the given
/// suffix (needed to prevent a name-clash with the function's stub),
/// and then take ownership of the module that the function was compiled
/// into.
llvm::orc::ThreadSafeModule irgenAndTakeOwnership(FunctionAST &FnAST,
                                                  const std::string &Suffix,
                                                  JitState &state);

#include "strideparser.h"

struct GeneratedCode {
  std::unique_ptr<ExprAST> expr;
  std::vector<std::unique_ptr<FunctionAST>> functions;
  std::vector<llvm::Function *> externalFunctions;
};

std::unique_ptr<ExprAST> createExpr(ASTNode node);

GeneratedCode createStreamCode(std::shared_ptr<StreamNode> stream, ASTNode tree,
                               JitState &state);

std::unique_ptr<FunctionAST>
createFunctionDecl(std::shared_ptr<FunctionNode> func, ASTNode prev,
                   ASTNode next, ASTNode tree, JitState &state);

std::unique_ptr<FunctionAST>
createFunctionDeclaration(std::shared_ptr<FunctionNode> func, ASTNode prev,
                          ASTNode next, ASTNode tree, JitState &state);

// void createGlobals(ASTNode tree, JitState &state);

void generateCode(ASTNode tree, JitState &state);

namespace llvm {
namespace orc {

// class StrideASTLayer;

// class StrideASTMaterializationUnit : public MaterializationUnit {
// public:
//  StrideASTMaterializationUnit(StrideASTLayer &L,
//                               std::unique_ptr<FunctionAST> F);

//  StringRef getName() const override { return "StrideASTMaterializationUnit";
//  }

//  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;

// private:
//  void discard(const JITDylib &JD, const SymbolStringPtr &Sym) override {
//    llvm_unreachable("Stride functions are not overridable");
//  }

//  StrideASTLayer &L;
//  std::unique_ptr<FunctionAST> F;
//};

// class StrideASTLayer {
// public:
//  StrideASTLayer(IRLayer &BaseLayer, const DataLayout &DL);

//  Error add(ResourceTrackerSP RT, std::unique_ptr<FunctionAST> F);

//  void emit(std::unique_ptr<MaterializationResponsibility> MR,
//            std::unique_ptr<FunctionAST> F);

//  MaterializationUnit::Interface getInterface(FunctionAST &F);

// private:
//  IRLayer &BaseLayer;
//  const DataLayout &DL;
//};

// class StrideJIT {
// private:
//  std::unique_ptr<ExecutionSession> ES;
//  std::unique_ptr<EPCIndirectionUtils> EPCIU;

//  DataLayout DL;
//  MangleAndInterner Mangle;

//  RTDyldObjectLinkingLayer ObjectLayer;
//  IRCompileLayer CompileLayer;
//  IRTransformLayer OptimizeLayer;
//  StrideASTLayer ASTLayer;

//  JITDylib &MainJD;

//  static void handleLazyCallThroughError() {
//    errs() << "LazyCallThrough error: Could not find function body";
//    exit(1);
//  }

// public:
//  StrideJIT(std::unique_ptr<ExecutionSession> ES,
//            std::unique_ptr<EPCIndirectionUtils> EPCIU,
//            JITTargetMachineBuilder JTMB, DataLayout DL);

//  ~StrideJIT();

//  static Expected<std::unique_ptr<StrideJIT>> Create();

//  const DataLayout &getDataLayout() const { return DL; }

//  JITDylib &getMainJITDylib() { return MainJD; }

//  Error addModule(ThreadSafeModule TSM, ResourceTrackerSP RT = nullptr) {
//    if (!RT)
//      RT = MainJD.getDefaultResourceTracker();

//    return OptimizeLayer.add(RT, std::move(TSM));
//  }

//  Error addAST(std::unique_ptr<FunctionAST> F, ResourceTrackerSP RT =
//  nullptr);

//  Expected<JITEvaluatedSymbol> lookup(StringRef Name) {
//    return ES->lookup({&MainJD}, Mangle(Name.str()));
//  }

// private:
//  static Expected<ThreadSafeModule>
//  optimizeModule(ThreadSafeModule TSM, const MaterializationResponsibility &R)
//  {
//    TSM.withModuleDo([](Module &M) {
//      // Create a function pass manager.
//      auto FPM = std::make_unique<legacy::FunctionPassManager>(&M);

//      // Add some optimizations.
//      FPM->add(createInstructionCombiningPass());
//      FPM->add(createReassociatePass());
//      FPM->add(createGVNPass());
//      FPM->add(createCFGSimplificationPass());
//      FPM->doInitialization();

//      // Run the optimizations over all functions in the module being added to
//      // the JIT.
//      for (auto &F : M)
//        FPM->run(F);
//    });

//    return std::move(TSM);
//  }
//};

} // end namespace orc
} // end namespace llvm

#endif // STRIDEENVIRONMENT_HPP
