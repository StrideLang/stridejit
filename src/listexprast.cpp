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

#include "stride/stridejit/listexprast.hpp"
#include "stride/stridejit/stridegenerator.hpp"
//#include "numberstride/stridejit/exprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include "stride/codegen/codeanalysis.hpp"
//#include "stride/parser/declarationnode.h"
// #include "stride/utils/astfunctions.h"

using namespace strd;

ListExprAST::ListExprAST(std::vector<strd::ASTNode> elements)
    : elementNodes(elements) {
  bool consistent = true;
  bool isMutable = false;
  std::string previousType = "";
  for (auto &e : elementNodes) {
    members.push_back(StrideGenerator::createExpr(e));

    if (e->getNodeType() != strd::AST::Int &&
        e->getNodeType() != strd::AST::Real &&
        e->getNodeType() != strd::AST::PortProperty) {
      isMutable = true;
    }
    // TODO use type cast metadata to determine consistency
    // Currently reporting inconsistent even if type cast has made consistent
    auto thisType = strd::CodeAnalysis::resolveNodeOutDataType(e, {}, nullptr);
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
    if (elem->getNodeType() == strd::AST::Int ||
        elem->getNodeType() == strd::AST::Real) {
      // Literal number list.
      double val = std::static_pointer_cast<strd::ValueNode>(elem)->toReal();
      values.push_back(val);

    } else {
      // Non-literal list
      return llvm::ConstantTokenNone::get(*state.TheContext);
    }
  }
  return llvm::ConstantDataArray::get(*state.TheContext, values);
}
