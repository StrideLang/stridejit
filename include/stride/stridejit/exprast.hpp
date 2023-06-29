#ifndef EXPRAST_HPP
#define EXPRAST_HPP

#include <map>
#include <string>
#include <variant>

using DefaultVariant = std::variant<double, int32_t, bool>;

namespace llvm {
class Value;
}
class StrideCompiler;

class ExprAST {
public:
  ExprAST();

  virtual ~ExprAST() = default;

  virtual llvm::Value *codegen(StrideCompiler &state) = 0;

  std::string typecast;
};

#endif // EXPRAST_HPP
