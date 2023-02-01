#ifndef STRIDEENVIRONMENT_HPP
#define STRIDEENVIRONMENT_HPP

#include <string>

#include "stridecompiler.hpp"

// llvm forward declarations
namespace llvm {
class Value;
}; // namespace llvm

#include "llvm/ExecutionEngine/Orc/LLJIT.h"

struct StrideExternalVariable {
  std::string name;
  llvm::Type *type;
};

class StrideEnvironment {
public:
  StrideEnvironment(std::string strideroot = std::string());

  bool generateIr(std::string path);
  bool compileInMemory();
  bool compileObjectToDisk(std::string path);

  StrideCompiler state;
  std::unique_ptr<llvm::orc::LLJIT> JIT;
  bool mVerbose{true};

  llvm::Expected<llvm::JITEvaluatedSymbol> getFunction(std::string);

private:
  bool loadLibrary(const char *libName, std::string &err);

  std::string m_strideRoot;
  bool m_optimizeCode{true};
  bool generateCompiledObject(std::string path, std::string TargetTriple);
};

#endif // STRIDEENVIRONMENT_HPP
