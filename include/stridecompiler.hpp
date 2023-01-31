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

#if defined(_MSC_VER)
//  Microsoft
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
//  GCC
#define EXPORT __attribute__((visibility("default")))
#define IMPORT
#else
//  do nothing and hope for the best?
#define EXPORT
#define IMPORT
#pragma warning Unknown dynamic link import / export semantics.
#endif

extern "C" {
EXPORT double __stride_Greater_d_dd(double a, double b);
EXPORT bool __stride_Greater_b_dd(double a, double b);
EXPORT bool __stride_Greater_b_ii(int32_t a, int32_t b);

EXPORT bool __stride_Equal_b_dd(double a, double b);
}

enum class DataType { DOUBLE, BOOL, INT32, INT64 };

struct ExternalFunction {
  std::string name;
  llvm::FunctionType *llvmFunctionType;
};

struct DomainArg {
  std::string name;
  DataType type;
};

enum StrideConfig {
  NO_OPTIONS = 0x0,
  PACK_DOMAIN_FUNCTION_EXTERNAL = 0x1,
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
                                           llvm::StringRef VarName,
                                           llvm::Type *dataType);

  std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
  }

  llvm::Value *LogErrorV(const char *Str) {
    LogError(Str);
    return nullptr;
  }

  void setConfiguration(StrideConfig option, bool enable = true);
  bool hasConfiguration(StrideConfig option);

  std::unique_ptr<llvm::LLVMContext> TheContext;
  std::unique_ptr<llvm::Module> TheModule;
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::map<std::string, llvm::Value *> NamedValues;
  std::map<std::string, llvm::Value *> PortBlockMap;
  std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
  std::map<char, int> BinopPrecedence;
  std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;

  std::unordered_map<std::string, std::vector<ExternalFunction>> functionMap;
  std::unordered_map<std::string, llvm::Type *> typesMap;

  std::unordered_map<std::string, std::vector<DomainArg>> domainArgs;

private:
  uint64_t m_configuration{NO_OPTIONS};
};

#endif // STRIDECOMPILER_HPP
