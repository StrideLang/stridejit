#pragma once

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
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#include "exprast.hpp"
#include "numberexprast.hpp"

// From stride parser
#include "ast.h"
#include "codequery.hpp"
#include "declarationnode.h"
#include "strideenvironment.hpp"

class StrideCompiler;

class ListExprAST : public ExprAST {
public:
  enum class Type {
    IMMUTABLE_CONSISTENT, // Made of literals
    MUTABLE_CONSISTENT,   // Consistent types, but at least one mutable
    UNSUPPORTED
  };

  ListExprAST(std::vector<ASTNode> elements);

  llvm::Value *codegen(StrideCompiler &state) override;
  std::vector<std::unique_ptr<ExprAST>> &elements() { return members; }
  Type getType() { return mType; }

private:
  std::vector<std::unique_ptr<ExprAST>> members;
  std::vector<ASTNode> elementNodes;

  Type mType{Type::UNSUPPORTED};
};
