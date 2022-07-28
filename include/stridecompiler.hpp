#ifndef STRIDECOMPILER_HPP
#define STRIDECOMPILER_HPP

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
#include <optional>
#include <variant>

#include "blocknode.h"
#include "declarationnode.h"
#include "functionast.hpp"

enum class DataType { DOUBLE, BOOL };

struct ExternalFunction {
  std::string name;
  llvm::FunctionType *llvmFunctionType;
};

class StrideCompiler {
public:
  StrideCompiler();

  std::optional<ExternalFunction>
  getExternalFunction(std::string strideName, llvm::Type *returnType,
                      std::vector<llvm::Type *> argTypes,
                      bool allowConversion = false);

  llvm::Type *getLLVMType(std::shared_ptr<DeclarationNode> decl);

  llvm::Function *getFunctionInModule(std::string Name);

  llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction,
                                           llvm::StringRef VarName);

  std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
  }

  llvm::Value *LogErrorV(const char *Str) {
    LogError(Str);
    return nullptr;
  }

  std::unique_ptr<llvm::LLVMContext> TheContext;
  std::unique_ptr<llvm::Module> TheModule;
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::map<std::string, llvm::Value *> NamedValues;
  std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
  std::map<char, int> BinopPrecedence;
  std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;

  std::unordered_map<std::string, std::vector<ExternalFunction>> functionMap;
  std::unordered_map<std::string, llvm::Type *> typesMap;

  std::unordered_map<std::string, std::vector<DataType>> domainArgs;

private:
};

#endif // STRIDECOMPILER_HPP
