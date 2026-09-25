#ifndef STRIDE_STATEMACHINE_HPP
#define STRIDE_STATEMACHINE_HPP

#include <memory>
#include <string>
#include <vector>

#include "stride/stridejit/exprast.hpp"

namespace strd {

class DeclarationNode;
class AST;
using ASTNode = std::shared_ptr<AST>;
using ScopeStack = std::vector<std::pair<ASTNode, std::vector<ASTNode>>>;

struct FlattenedState {
  FlattenedState() = default;
  FlattenedState(const FlattenedState&) = delete;
  FlattenedState& operator=(const FlattenedState&) = delete;
  FlattenedState(FlattenedState&&) noexcept = default;
  FlattenedState& operator=(FlattenedState&&) noexcept = default;

  int id;
  std::shared_ptr<DeclarationNode> stateDecl;

  std::vector<std::unique_ptr<ExprAST>> onEntryCode;
  std::vector<std::unique_ptr<ExprAST>> onProcessCode;
  std::vector<std::unique_ptr<ExprAST>> onExitCode;
};

class StateMachine {
public:
  StateMachine() = default;
  StateMachine(const StateMachine&) = delete;
  StateMachine& operator=(const StateMachine&) = delete;
  StateMachine(StateMachine&&) noexcept = default;
  StateMachine& operator=(StateMachine&&) noexcept = default;

  std::string name;
  std::shared_ptr<DeclarationNode> smDecl;
  std::vector<FlattenedState> flattenedStates;
  int initialStateId;
  std::string activeStateVarName;

  static void flattenStateMachine(std::shared_ptr<DeclarationNode> stateNode,
                                  int &idCounter, StateMachine &sm,
                                  ScopeStack &scope, ASTNode tree);

  static std::vector<StateMachine>
  collectStateMachines(std::shared_ptr<DeclarationNode> domainDecl,
                       ScopeStack &scope, ASTNode tree);
};

} // namespace strd

#endif // STRIDE_STATEMACHINE_HPP
