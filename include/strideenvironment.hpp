#ifndef STRIDEENVIRONMENT_HPP
#define STRIDEENVIRONMENT_HPP

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"

//#include "astfunctions.h"
#include "strideparser.h"

#include "stridecompiler.hpp"

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

class StrideEnvironment {
public:
  StrideEnvironment(std::string strideroot = std::string());

  bool compile(std::string path);

  StrideCompiler state;
  std::unique_ptr<llvm::orc::LLJIT> JIT;
  bool mVerbose{true};

private:
  bool loadLibrary(const char *libName, std::string &err);

  std::string m_strideRoot;
  bool m_optimizeCode{true};
};

/// This will compile FnAST to IR, rename the function to add the given
/// suffix (needed to prevent a name-clash with the function's stub),
/// and then take ownership of the module that the function was compiled
/// into.
llvm::orc::ThreadSafeModule irgenAndTakeOwnership(FunctionAST &FnAST,
                                                  const std::string &Suffix,
                                                  StrideCompiler &state);

struct DomainCode {
  std::unique_ptr<ExprAST> expr;
  std::vector<std::unique_ptr<FunctionAST>> functions;
  std::vector<llvm::Function *> externalFunctions;
  std::vector<ASTNode> readVariables;
  std::vector<ASTNode> writeVariables;
};

using GeneratedCode = std::map<std::string, DomainCode>;

std::unique_ptr<ExprAST> createExpr(ASTNode node);

GeneratedCode createStreamCode(std::shared_ptr<StreamNode> stream, ASTNode tree,
                               ScopeStack *scope, StrideCompiler &state);
std::unique_ptr<FunctionAST>
createFunctionDeclaration(std::shared_ptr<FunctionNode> func, ASTNode prev,
                          ASTNode next, ASTNode tree, ScopeStack *scope,
                          StrideCompiler &state);

// void createGlobals(ASTNode tree, JitState &state);

void generateCode(ASTNode tree, ScopeStack *scope, StrideCompiler &state);

#endif // STRIDEENVIRONMENT_HPP
