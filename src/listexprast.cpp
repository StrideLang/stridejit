//#include "llvm/ADT/StringRef.h"
//#include "llvm/ExecutionEngine/JITSymbol.h"
//#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
//#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
//#include "llvm/ExecutionEngine/Orc/Core.h"
//#include "llvm/ExecutionEngine/Orc/EPCIndirectionUtils.h"
//#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
//#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
//#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
//#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
//#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
//#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
//#include "llvm/ExecutionEngine/SectionMemoryManager.h"
//#include "llvm/IR/DataLayout.h"
//#include "llvm/IR/LLVMContext.h"
//#include "llvm/IR/LegacyPassManager.h"
//#include "llvm/Transforms/InstCombine/InstCombine.h"
//#include "llvm/Transforms/Scalar.h"
//#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/IR/Constants.h"

#include "listexprast.hpp"
#include "stridegenerator.hpp"
//#include "numberexprast.hpp"
#include "stridecompiler.hpp"

#include "codequery.hpp"
//#include "stride/parser/declarationnode.h"
#include <stride/parser/astfunctions.h>

ListExprAST::ListExprAST(std::vector<ASTNode> elements)
    : elementNodes(elements) {
  bool consistent = true;
  bool isMutable = false;
  std::string previousType = "";
  for (const auto &e : elementNodes) {
    members.push_back(StrideGenerator::createExpr(e));

    if (e->getNodeType() != AST::Int && e->getNodeType() != AST::Real &&
        e->getNodeType() != AST::PortProperty) {
      isMutable = true;
    }
    // TODO use type cast metadata to determine consistency
    // Currently reporting inconsistent even if type cast has made consistent
    auto thisType = CodeQuery::resolveNodeOutDataType(e, {}, nullptr);
    if ((previousType != "") && previousType != previousType) {
      consistent = false;
    }
    previousType = thisType;
  }
  if (!isMutable && consistent) {
    mType = Type::IMMUTABLE_CONSISTENT;
  } else if (isMutable && consistent) {
    mType = Type::MUTABLE_CONSISTENT;
  }
}

llvm::Value *ListExprAST::codegen(StrideCompiler &state) {
  // All elements are constant
  std::vector<double> values;

  for (const auto &elem : elementNodes) {
    if (elem->getNodeType() == AST::Int || elem->getNodeType() == AST::Real) {
      // Literal number list.
      double val = std::static_pointer_cast<ValueNode>(elem)->toReal();
      values.push_back(val);

    } else {
      // Non-literal list
      return llvm::ConstantTokenNone::get(*state.TheContext);
    }
  }
  return llvm::ConstantDataArray::get(*state.TheContext, values);
}
