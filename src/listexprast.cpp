// #include "llvm/ADT/StringRef.h"
// #include "llvm/ExecutionEngine/JITSymbol.h"
// #include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
// #include "llvm/ExecutionEngine/Orc/CompileUtils.h"
// #include "llvm/ExecutionEngine/Orc/Core.h"
// #include "llvm/ExecutionEngine/Orc/EPCIndirectionUtils.h"
// #include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
// #include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
// #include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
// #include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
// #include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
// #include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
// #include "llvm/ExecutionEngine/SectionMemoryManager.h"
// #include "llvm/IR/DataLayout.h"
// #include "llvm/IR/LLVMContext.h"
// #include "llvm/IR/LegacyPassManager.h"
// #include "llvm/Transforms/InstCombine/InstCombine.h"
// #include "llvm/Transforms/Scalar.h"
// #include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/IR/Constants.h"
#include <iostream>

#include "stride/stridejit/listexprast.hpp"
#include "stride/stridejit/stridegenerator.hpp"
// #include "numberstride/stridejit/exprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include "stride/codegen/codeanalysis.hpp"
// #include "stride/parser/declarationnode.h"
//  #include "stride/utils/astfunctions.h"

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
    if ((previousType != "") && previousType != thisType) {
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

std::pair<llvm::Value *, std::optional<llvm::Type *>>
ListExprAST::codegen(StrideCompiler &state) {
  // All elements are constant
  std::vector<double> values;
  std::vector<int32_t> intValues;
  std::optional<llvm::Type *> dataType;
  for (const auto &elem : elementNodes) {
    if (elem->getNodeType() == strd::AST::Real) {
      // Literal number list.
      double val =
          std::static_pointer_cast<strd::ValueNode>(elem)->getRealValue();
      values.push_back(val);
      if (dataType.has_value()) {
        if (dataType.value() != llvm::Type::getDoubleTy(*state.TheContext)) {
          std::cerr << "ERROR, inconsistent lists not supported" << std::endl;
        }
      } else {
        dataType = llvm::Type::getDoubleTy(*state.TheContext);
      }
    } else if (elem->getNodeType() == strd::AST::Int) {
      int32_t val =
          std::static_pointer_cast<strd::ValueNode>(elem)->getIntValue();
      intValues.push_back(val);
      if (dataType.has_value()) {
        if (dataType.value() != llvm::Type::getInt32Ty(*state.TheContext)) {
          std::cerr << "ERROR, inconsistent lists not supported" << std::endl;
        }
      } else {
        dataType = llvm::Type::getInt32Ty(*state.TheContext);
      }
    } else {
      // Non-literal list
      return {llvm::ConstantTokenNone::get(*state.TheContext), dataType};
    }
  }

  if (values.size() > 0) {
    assert(intValues.size() == 0);
    return {llvm::ConstantDataArray::get(*state.TheContext, values), dataType};
  } else {
    return {llvm::ConstantDataArray::get(*state.TheContext, intValues),
            dataType};
  }
}
