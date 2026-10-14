#ifndef EXPRAST_HPP
#define EXPRAST_HPP

#include <map>
#include <optional>
#include <string>
#include <variant>

namespace llvm {
class Value;
class Type;
} // namespace llvm

namespace strd {
using DefaultVariant = std::variant<double, int32_t, bool, std::string>;

class StrideCompiler;

class ExprAST {
public:
  ExprAST();

  virtual ~ExprAST() = default;

  virtual std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) = 0;

  virtual bool writesTo(const std::string &varName) const { return false; }

  std::string typecast;
};

class StringExprAST : public ExprAST {
public:
  explicit StringExprAST(std::string val) : Val(std::move(val)) {}

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;

  const std::string &getValue() const { return Val; }

private:
  std::string Val;
};
} // namespace strd

#endif // EXPRAST_HPP
