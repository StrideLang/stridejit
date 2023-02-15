#ifndef FUNCTIONAST_HPP
#define FUNCTIONAST_HPP

#include "exprast.hpp"
#include "numberexprast.hpp"

// From stride parser
#include "ast.h"
#include "declarationnode.h"

class StrideCompiler;

struct PrototypeArg {
  std::string name;
  llvm::Type *llvmType;
  std::string property; // If port property, this is not empty
};

enum class CallableType { Module, Reaction, Loop, External, DomainFunction };

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
///
class PrototypeAST {
  std::string Name;
  std::vector<PrototypeArg> Args;
  std::vector<PrototypeArg> OutArgs;
  std::vector<PrototypeArg>
      ExternalArgs; // For upper scope in reactions and loops
  std::vector<PrototypeArg> UsedPortProperties;
  bool IsOperator;
  unsigned Precedence; // Precedence if a binary op.

public:
  PrototypeAST(const std::string &Name, std::vector<PrototypeArg> OutArgs,
               std::vector<PrototypeArg> Args,
               std::vector<PrototypeArg> ExternalArgs,
               std::vector<PrototypeArg> UsedPortProperties,
               bool IsOperator = false, unsigned Prec = 0)
      : Name(Name), Args(Args), OutArgs(OutArgs), ExternalArgs(ExternalArgs),
        UsedPortProperties(UsedPortProperties) {}

  llvm::Function *codegen(StrideCompiler &state);
  const std::string &getName() const { return Name; }
  bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }
  char getOperatorName() const {
    assert(isUnaryOp() || isBinaryOp());
    return Name[Name.size() - 1];
  }
  unsigned getBinaryPrecedence() const { return Precedence; }
  std::vector<PrototypeArg> getExternalArgs() const;

  CallableType callType; // Set by parent FunctionAST before codegen()
  std::vector<PrototypeArg> getUsedPortProperties() const;
};

class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::vector<std::unique_ptr<ExprAST>> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body);
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::vector<std::unique_ptr<ExprAST>> Body);

  const PrototypeAST &getProto() const;

  const std::string &getName() const;
  llvm::Function *codegen(StrideCompiler &state);

  std::vector<llvm::Function *> externalFunctions;
  std::vector<std::shared_ptr<DeclarationNode>> internalVariables;
  std::string terminateWhenName;

  CallableType callType;
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
protected:
  std::string Callee;

public:
  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> OutArgs,
              std::vector<std::unique_ptr<ExprAST>> InArgs,
              std::vector<std::unique_ptr<ExprAST>> ExternalArgs,
              std::vector<std::unique_ptr<ExprAST>> PortPropArgs)
      : Callee(Callee), InArgs(std::move(InArgs)), OutArgs(std::move(OutArgs)),
        ExternalArgs(std::move(ExternalArgs)),
        PortPropArgs(std::move(PortPropArgs)) {}

  llvm::Value *codegen(StrideCompiler &state) override;

  CallableType callType;

  std::vector<std::unique_ptr<ExprAST>> InArgs;
  std::vector<std::unique_ptr<ExprAST>> OutArgs;
  std::vector<std::unique_ptr<ExprAST>> ExternalArgs;
  std::vector<std::unique_ptr<ExprAST>> PortPropArgs;
  std::unique_ptr<ExprAST> ret;
};

#endif // FUNCTIONAST_HPP
