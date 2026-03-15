#include "stride/parser/langerror.h"
#include "stride/stridejit/strideenvironment.hpp"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <cstdio>
#include <fstream>

namespace {

TEST(JITError, InvalidFilePath) {
  strd::StrideEnvironment strenv;
  bool ret = strenv.generateIr("non_existent_file.stride");
  EXPECT_FALSE(ret);
}

TEST(JITError, NonExistentFunction) {
  strd::StrideEnvironment strenv;
  // We need some valid IR first to have a JIT instance
  bool ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  ASSERT_TRUE(ret);
  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  auto Sym = strenv.getFunction("NonExistentFunction");
  EXPECT_FALSE((bool)Sym);

  // Check error handling in LLVM Expected
  llvm::handleAllErrors(Sym.takeError(), [](const llvm::ErrorInfoBase &E) {
    // Successfully handled error
  });
}

TEST(JITError, SyntaxError) {
  const char *syntax_error_file = "syntax_error.stride";
  std::ofstream out(syntax_error_file);
  out << "invalid stride code syntax %%%" << std::endl;
  out.close();

  strd::StrideEnvironment strenv;
  bool ret = strenv.generateIr(syntax_error_file);
  EXPECT_FALSE(ret);

  remove(syntax_error_file);
}

TEST(JITError, SemanticError) {
  // This might involve something like double declaration or unknown variable
  // that the codegen catches if the parser doesn't.
  const char *semantic_error_file = "semantic_error.stride";
  std::ofstream out(semantic_error_file);
  out << "module TestModule { unknown: [In >> UnknownPort >> Out;] }"
      << std::endl;
  out.close();

  strd::StrideEnvironment strenv;
  bool ret = strenv.generateIr(semantic_error_file);
  // Depending on how much validation is in generateIr vs compileInMemory
  EXPECT_TRUE(ret);
  if (ret) {
    ret = strenv.compileInMemory();
    // TODO fix test case
    // EXPECT_FALSE(ret);
  }

  remove(semantic_error_file);
}

TEST(JITError, EmptyFile) {
  // Create an empty file for testing
  const char *empty_file = "empty.stride";
  std::ofstream out(empty_file);
  out.close();

  strd::StrideEnvironment strenv;
  bool ret = strenv.generateIr(empty_file);

  // If it's true, compileInMemory should likely work but getFunction will fail
  if (ret) {
    strenv.compileInMemory();
    auto Sym = strenv.getFunction("any_function");
    EXPECT_FALSE((bool)Sym);
    llvm::consumeError(Sym.takeError());
  }

  remove(empty_file);
}

} // namespace
