
#include "llvm/IR/Module.h"
#include <iostream>

#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

using namespace strd;
// ----------------------

std::pair<llvm::Value *, std::optional<llvm::Type *>>
RealExprAST::codegen(StrideCompiler &state) {

  if (typecast.size() > 0) {
    if (typecast == "_IntType") {
      return {llvm::ConstantInt::get(*state.TheContext, llvm::APInt(32, Val)),
              llvm::Type::getInt32Ty(*state.TheContext)};
    }
  }
  return {llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(Val)),
          llvm::Type::getDoubleTy(*state.TheContext)};
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
IntExprAST::codegen(StrideCompiler &state) {
  if (typecast.size() > 0) {
    if (typecast == "_RealType") {
      return {
          llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(double(Val))),
          llvm::Type::getDoubleTy(*state.TheContext)};
    }
  }
  return {llvm::ConstantInt::get(*state.TheContext, llvm::APInt(NumBits, Val)),
          llvm::Type::getInt32Ty(*state.TheContext)};
}

std::vector<std::variant<size_t, std::string>>
VariableExprAST::getIndeces() const {
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
  if (typecast.size() > 0) {
    if (V->getType()->isIntegerTy() && typecast == "_RealType") {
      V = state.Builder->CreateSIToFP(
          V, llvm::Type::getDoubleTy(*state.TheContext));
    } else if (V->getType()->isDoubleTy() && typecast == "_IntType") {
      V = state.Builder->CreateFPToSI(
          V, llvm::Type::getInt32Ty(*state.TheContext));
    }
  }
  return {V, T};
}
