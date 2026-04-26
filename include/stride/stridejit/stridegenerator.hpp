#ifndef STRIDEGENERATOR_HPP
#define STRIDEGENERATOR_HPP

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "stride/stridejit/exprast.hpp"

#include "stride/stridejit/stridecompiler.hpp"

// llvm
namespace llvm {
class Function;
}

namespace strd {
// Stride
class StreamNode;
class FunctionNode;
class AST;
using ASTNode = std::shared_ptr<AST>;
class DeclarationNode;
using ScopeStack = std::vector<std::pair<ASTNode, std::vector<ASTNode>>>;

// StrideJIT
class FunctionAST;
class StrideCompiler;

class StrideGenerator {
public:
  struct DomainCode {
    std::vector<std::unique_ptr<ExprAST>> expr;
    std::vector<std::unique_ptr<FunctionAST>> functions;
    std::vector<llvm::Function *> externalFunctions;
    std::vector<ASTNode> readVariables;
    std::vector<ASTNode> writeVariables;
  };

  struct GeneratedIRCode {
    std::map<std::string, std::vector<std::unique_ptr<ExprAST>>>
        domainGeneratedCode;
    std::vector<DomainArg> domainArgs;
    std::vector<std::shared_ptr<DeclarationNode>> GlobalSignals;
  };

  using GeneratedCode = std::map<std::string, DomainCode>;

  static void generateCode(ASTNode tree, ScopeStack &scope,
                           StrideCompiler &state);

  static GeneratedIRCode generateCodeForTree(ASTNode tree, ScopeStack &scope,
                                             StrideCompiler &state);

  static std::unique_ptr<ExprAST> createExpr(ASTNode node);

  static std::unique_ptr<FunctionAST>
  createFunctionDeclaration(std::shared_ptr<DeclarationNode> funcDecl,
                            std::shared_ptr<FunctionNode> funcInstance,
                            ASTNode tree, ScopeStack *scope,
                            StrideCompiler &state);

  static void
  generatePlatformFunctionSignature(std::shared_ptr<DeclarationNode> decl,
                                    std::vector<ASTNode> &frameworkScope,
                                    StrideCompiler &state);

private:
  static GeneratedCode createStreamCode(std::shared_ptr<StreamNode> stream,
                                        ASTNode tree, ScopeStack &scope,
                                        StrideCompiler &state);

  static void setTypeCastMetadata(ASTNode node, ExprAST *V);

  // helper functions
  static DefaultVariant getDefaultValue(std::shared_ptr<DeclarationNode> decl,
                                        StrideCompiler &state);
};
} // namespace strd

#endif // STRIDEGENERATOR_HPP
