#ifndef STRIDECOMPILER_HPP
#define STRIDECOMPILER_HPP

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "stride/codegen/codeanalysis.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/functionnode.h"
#include "stride/stridejit/functionast.hpp"

// #include "llvm/Config/llvm-config.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

// forward declarations for llvm
namespace llvm {
class Value;
class Module;

} // namespace llvm

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
}

namespace strd {
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

  llvm::Type *getLLVMType(std::shared_ptr<strd::DeclarationNode> decl);
  llvm::Type *
  getLLVMTypeForCodegenBlock(std::shared_ptr<strd::DeclarationNode> decl,
                             std::shared_ptr<DeclarationNode> funcDecl,
                             std::shared_ptr<FunctionNode> functionInstance);

  llvm::Function *getFunctionInModule(std::string Name);

  llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction,
                                           llvm::StringRef VarName,
                                           llvm::Type *dataType);

  // Globals in current name prefix
  void createGlobal(std::shared_ptr<DeclarationNode> globalDecl);
  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  getGlobal(std::string globalName);
  bool globalExists(std::string globalName);

  llvm::Type *getElementType(llvm::Value *V);

  // Name prefix. Used to name instances and globals
  void pushName(std::string name);
  void popName();
  std::string getName();

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
  std::map<std::string, std::pair<llvm::Value *, std::optional<llvm::Type *>>>
      NamedValues;
  std::map<std::string, llvm::Value *> PortBlockMap;
  std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
  std::map<char, int> BinopPrecedence;

  CodeAnalysis::TypeTree m_intanceTree;

  std::unordered_map<std::string, std::vector<ExternalFunction>> functionMap;
  std::unordered_map<std::string, llvm::Type *> typesMap;

  std::unordered_map<std::string, std::vector<DomainArg>> domainArgs;

  // Track pointer element types for opaque pointers (LLVM >= 17)
  std::map<llvm::Value *, llvm::Type *> pointerElementTypes;

  std::vector<std::string> m_nameStack;
  uint32_t m_idCounter{0};

private:
  uint64_t m_configuration{NO_OPTIONS};
  std::map<std::string, std::pair<llvm::Value *, std::optional<llvm::Type *>>>
      m_globals;
};

} // namespace strd

#endif // STRIDECOMPILER_HPP
