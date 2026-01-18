#ifndef EXPRAST_HPP
#define EXPRAST_HPP

#include <map>
#include <string>
#include <variant>

namespace llvm {
class Value;
}

namespace strd {
using DefaultVariant = std::variant<double, int32_t, bool>;

class StrideCompiler;

class ExprAST {
public:
  ExprAST();

  virtual ~ExprAST() = default;

  virtual llvm::Value *codegen(StrideCompiler &state) = 0;

  std::string typecast;
};
} // namespace strd

#endif // EXPRAST_HPP
