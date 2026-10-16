#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

using namespace strd;

ExprAST::ExprAST() {}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
StringExprAST::codegen(StrideCompiler &state) {
  llvm::Value *strGlobal =
      state.Builder->CreateGlobalStringPtr(Val, "str_lit", 0, state.TheModule.get());
  llvm::Type *stringTy = state.typesMap["_StringType"];
  llvm::Value *lenVal = llvm::ConstantInt::get(
      llvm::Type::getInt32Ty(*state.TheContext), Val.size());
  llvm::Value *strStruct = llvm::UndefValue::get(stringTy);
  strStruct = state.Builder->CreateInsertValue(strStruct, strGlobal, 0);
  strStruct = state.Builder->CreateInsertValue(strStruct, lenVal, 1);
  return {strStruct, stringTy};
}
