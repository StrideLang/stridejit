#ifndef EXPRAST_HPP
#define EXPRAST_HPP

#include "llvm/IR/Value.h"

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
