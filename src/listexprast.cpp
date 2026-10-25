// #include "llvm/ADT/StringRef.h"
#include "stride/utils/logger.h"
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

#include "stride/stridegen/codeanalysis.hpp"
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
        e->getNodeType() != strd::AST::MemberAccess) {
      isMutable = true;
    }
    std::string thisType;
    auto typecastNode = e->getCompilerProperty("typecast");
    if (typecastNode && typecastNode->getNodeType() == strd::AST::String) {
      thisType =
          std::static_pointer_cast<strd::ValueNode>(typecastNode)->getStringValue();
    } else {
      thisType = strd::CodeAnalysis::resolveNodeOutDataType(e, {}, nullptr);
    }
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
  // All elements are constant. Each one takes its own typecast, then the
  // typecast of the list, then the type of the literal.
  std::vector<llvm::Constant *> constants;
  std::optional<llvm::Type *> dataType;
  for (const auto &elem : elementNodes) {
    std::string elemType;
    if (auto typecastNode = elem->getCompilerProperty("typecast")) {
      if (typecastNode->getNodeType() == strd::AST::String) {
        elemType = std::static_pointer_cast<strd::ValueNode>(typecastNode)
                       ->getStringValue();
      }
    }
    if (elemType.empty()) {
      elemType = typecast;
    }
    if (elem->getNodeType() != strd::AST::Real &&
        elem->getNodeType() != strd::AST::Int) {
      // Non-literal list
      return {llvm::ConstantTokenNone::get(*state.TheContext), dataType};
    }
    auto value = std::static_pointer_cast<strd::ValueNode>(elem);
    auto info = state.getDataTypeInfo(elemType);
    if (!info.isNumeric()) {
      info = state.getDataTypeInfo(elem->getNodeType() == strd::AST::Real
                                       ? DataTypes::Real
                                       : DataTypes::Int);
    }
    llvm::Type *elemLLVMType = state.getLLVMTypeForDataType(info);
    llvm::Constant *constant = nullptr;
    if (info.isFloat()) {
      double val = elem->getNodeType() == strd::AST::Real
                       ? value->getRealValue()
                       : double(value->getIntValue());
      constant = llvm::ConstantFP::get(elemLLVMType, val);
    } else {
      int64_t val = elem->getNodeType() == strd::AST::Int
                        ? value->getIntValue()
                        : static_cast<int64_t>(value->getRealValue());
      constant = llvm::ConstantInt::get(elemLLVMType, static_cast<uint64_t>(val),
                                        info.isSigned);
    }
    if (dataType.has_value()) {
      if (dataType.value() != elemLLVMType) {
        LOG_ERROR() << "ERROR, inconsistent lists not supported" << std::endl;
      }
    } else {
      dataType = elemLLVMType;
    }
    constants.push_back(constant);
  }
  if (!dataType.has_value()) {
    dataType = state.typesMap[DataTypes::Real];
  }
  auto *arrayType = llvm::ArrayType::get(*dataType, constants.size());
  return {llvm::ConstantArray::get(arrayType, constants), dataType};
}
