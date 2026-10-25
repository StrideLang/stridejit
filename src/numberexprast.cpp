
#include "llvm/IR/Module.h"
#include "stride/utils/logger.h"
#include <iostream>

#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

using namespace strd;
// ----------------------

std::pair<llvm::Value *, std::optional<llvm::Type *>>
RealExprAST::codegen(StrideCompiler &state) {
  llvm::Type *type = state.typesMap[DataTypes::Real];
  if (typecast.size() > 0) {
    auto info = state.getDataTypeInfo(typecast);
    if (info.isInteger()) {
      auto *intType = state.getLLVMTypeForDataType(info);
      return {llvm::ConstantInt::get(intType, static_cast<int64_t>(Val),
                                     info.isSigned),
              intType};
    } else if (info.isFloat()) {
      type = state.getLLVMTypeForDataType(info);
    }
  }
  return {llvm::ConstantFP::get(type, Val), type};
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
IntExprAST::codegen(StrideCompiler &state) {
  if (typecast.size() > 0) {
    auto info = state.getDataTypeInfo(typecast);
    if (info.isFloat()) {
      auto *floatType = state.getLLVMTypeForDataType(info);
      return {llvm::ConstantFP::get(floatType, double(Val)), floatType};
    } else if (info.isInteger()) {
      auto *intType = state.getLLVMTypeForDataType(info);
      return {llvm::ConstantInt::get(intType, static_cast<uint64_t>(Val),
                                     info.isSigned),
              intType};
    }
  }
  if (NumBits == 64) {
    return {llvm::ConstantInt::get(*state.TheContext,
                                   llvm::APInt(NumBits, Val, Signed)),
            llvm::Type::getInt64Ty(*state.TheContext)};
  } else if (NumBits == 32) {
    return {llvm::ConstantInt::get(
                *state.TheContext,
                llvm::APInt(NumBits, static_cast<int32_t>(Val), Signed)),
            llvm::Type::getInt32Ty(*state.TheContext)};
  }
  LOG_INFO() << "IntExprAST::codegen Unsupported number of bits" << std::endl;
  return {nullptr, nullptr};
}


std::vector<std::variant<size_t, std::string>>
VariableExprAST::getIndices() const {
  return Indeces;
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
VariableExprAST::codegen(StrideCompiler &state) {
  // Look this variable up in the function.
  llvm::Value *V{nullptr};
  std::optional<llvm::Type *> T;
  if (state.NamedValues.find(Name) != state.NamedValues.end()) {
    V = state.NamedValues[Name].first;
    assert(state.NamedValues[Name].second.has_value());
    T = state.NamedValues[Name].second;
  } else if (state.globalExists(Name)) {
    auto global = state.getGlobal(Name);
    V = global.first;
    T = global.second;
  }
  if (!V)
    return {state.LogErrorV(("Unknown variable name: " + Name).c_str()),
            std::nullopt};
  //  return
  //  state.Builder->CreateLoad(llvm::Type::getDoubleTy(*state.TheContext),
  //                                   V, Name.c_str());
  if (typecast.size() > 0 && !V->getType()->isPointerTy()) {
    V = state.convertToDataType(V, dataType, typecast);
  }
  return {V, T};
}
