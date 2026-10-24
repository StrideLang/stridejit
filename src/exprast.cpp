#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

#include "stride/parser/ast.h"

using namespace strd;

ExprAST::ExprAST() {}

void ExprAST::setLocationFrom(const std::shared_ptr<AST> &node) {
  if (node) {
    m_line = node->getLine();
    m_column = node->getColumn();
    m_relativeFilename = node->getRelativeFilename();
    m_absoluteFilename = node->getAbsoluteFilename();
  }
}

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
