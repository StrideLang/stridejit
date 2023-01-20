#ifndef EXPRAST_HPP
#define EXPRAST_HPP

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include <map>

class StrideCompiler;

class ExprAST {
public:
  ExprAST();

  virtual ~ExprAST() = default;

  virtual llvm::Value *codegen(StrideCompiler &state) = 0;

  std::string typecast;
};

#endif // EXPRAST_HPP
