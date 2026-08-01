#ifndef FUNCTIONAST_HPP
#define FUNCTIONAST_HPP

#include "stride/stridejit/exprast.hpp"

// From stride parser
#include "stride/parser/declarationnode.h"

namespace llvm {
class Type;
class Function;
} // namespace llvm

namespace strd {
class StrideCompiler;

struct PrototypeArg {
  std::string name;
  llvm::Type *llvmType;
  std::string property; // If port property, this is not empty
  DefaultVariant defaultValue;
};

enum class CallableType {
  Module,
  Reaction,
  Loop,
  External,
  DomainFunction,
  LLVMCommand
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
///
class PrototypeAST {
  std::string Name;
  std::vector<PrototypeArg> InArgs;
  std::vector<PrototypeArg> OutArgs;
  std::vector<PrototypeArg> InternalPersistentArgs; // For internal scope variables.
  std::vector<PrototypeArg>
      ExternalArgs; // For upper scope in reactions and loops
  std::vector<PrototypeArg> UsedPortProperties;
  bool IsOperator;
  unsigned Precedence; // Precedence if a binary op.

public:
  PrototypeAST(const std::string &Name, std::vector<PrototypeArg> OutArgs,
               std::vector<PrototypeArg> InArgs,
               std::vector<PrototypeArg> InternalArgs,
               std::vector<PrototypeArg> ExternalArgs,
               std::vector<PrototypeArg> UsedPortProperties,
               bool IsOperator = false, unsigned Prec = 0)
      : Name(Name), InArgs(InArgs), OutArgs(OutArgs),
        InternalPersistentArgs(InternalArgs), ExternalArgs(ExternalArgs),
        UsedPortProperties(UsedPortProperties) {}

  llvm::Function *codegen(StrideCompiler &state);
  const std::string &getName() const { return Name; }
  bool isUnaryOp() const { return IsOperator && InArgs.size() == 1; }
  bool isBinaryOp() const { return IsOperator && InArgs.size() == 2; }
  char getOperatorName() const;
  unsigned getBinaryPrecedence() const { return Precedence; }

  std::vector<llvm::Type *> getUsedArgsTypes() const;
  std::vector<PrototypeArg> getExternalArgs() const;
  std::vector<PrototypeArg> getInternalArgs() const;

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
  std::vector<std::shared_ptr<strd::DeclarationNode>> internalVariables;
  std::string terminateWhenName;

  CallableType callType;

private:
  void allocateInternalVariables(StrideCompiler &state,
                              llvm::Function *TheFunction);
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
protected:
  std::string Callee;

public:
  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> OutArgs,
              std::vector<std::unique_ptr<ExprAST>> InArgs,
              std::vector<std::unique_ptr<ExprAST>> InternalArgs,
              std::vector<std::unique_ptr<ExprAST>> ExternalArgs,
              std::vector<std::unique_ptr<ExprAST>> PortPropArgs,
              std::vector<llvm::Type *> OutArgsDataType,
              std::vector<llvm::Type *> InArgsDataType,
              std::string instanceName)
      : Callee(Callee), InArgs(std::move(InArgs)), OutArgs(std::move(OutArgs)),
        InternalArgs(std::move(InternalArgs)), InArgsDataType(InArgsDataType),
        OutArgsDataType(OutArgsDataType), ExternalArgs(std::move(ExternalArgs)),
        PortPropArgs(std::move(PortPropArgs)), instanceName(instanceName) {}

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;

  CallableType callType;

  std::vector<std::unique_ptr<ExprAST>> InArgs;
  std::vector<std::unique_ptr<ExprAST>> OutArgs;
  std::vector<std::unique_ptr<ExprAST>> InternalArgs;
  std::vector<llvm::Type *> InArgsDataType;
  std::vector<llvm::Type *> OutArgsDataType;
  std::vector<std::unique_ptr<ExprAST>> ExternalArgs;
  std::vector<std::unique_ptr<ExprAST>> PortPropArgs;
  std::string instanceName;
  std::unique_ptr<ExprAST> ret;
};

class LLVMCommandAST : public ExprAST {
protected:
public:
  LLVMCommandAST(const std::string &command,
                 std::vector<std::unique_ptr<ExprAST>> OutArgs,
                 std::vector<std::unique_ptr<ExprAST>> InArgs,
                 std::vector<std::unique_ptr<ExprAST>> ExternalArgs,
                 std::vector<std::unique_ptr<ExprAST>> PortPropArgs)
      : command(command), InArgs(std::move(InArgs)),
        OutArgs(std::move(OutArgs)), ExternalArgs(std::move(ExternalArgs)),
        PortPropArgs(std::move(PortPropArgs)) {}

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;

  std::string command;
  std::vector<std::unique_ptr<ExprAST>> InArgs;
  std::vector<std::unique_ptr<ExprAST>> OutArgs;
  std::vector<std::unique_ptr<ExprAST>> ExternalArgs;
  std::vector<std::unique_ptr<ExprAST>> PortPropArgs;
  std::unique_ptr<ExprAST> ret;
};
} // namespace strd

#endif // FUNCTIONAST_HPP
