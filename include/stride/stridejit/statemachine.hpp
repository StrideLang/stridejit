#ifndef STRIDE_STATEMACHINE_HPP
#define STRIDE_STATEMACHINE_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "stride/parser/blocknode.h"
#include "stride/stridejit/exprast.hpp"

namespace strd {

class DeclarationNode;
class AST;
using ASTNode = std::shared_ptr<AST>;
using ScopeStack = std::vector<std::pair<ASTNode, std::vector<ASTNode>>>;

struct Transition {
  Transition() = default;
  Transition(const Transition &) = delete;
  Transition &operator=(const Transition &) = delete;
  Transition(Transition &&) noexcept = default;
  Transition &operator=(Transition &&) noexcept = default;

  int id;
  std::shared_ptr<DeclarationNode> transitionDecl;
  int targetStateId;
  bool triggerOnGuard = false;

  std::vector<std::unique_ptr<ExprAST>> guardCode;
  std::vector<std::unique_ptr<ExprAST>> onTransitionCode;
};

struct FlattenedState {
  FlattenedState() = default;
  FlattenedState(const FlattenedState &) = delete;
  FlattenedState &operator=(const FlattenedState &) = delete;
  FlattenedState(FlattenedState &&) noexcept = default;
  FlattenedState &operator=(FlattenedState &&) noexcept = default;

  int id;
  int parentId = -1;
  std::shared_ptr<DeclarationNode> stateDecl;

  bool isParallel = false;
  bool resumeLastState = false;
  bool isFinal = false;
  bool updateGuardOnDomain = false;
  std::string historyStateVarName;

  std::vector<std::unique_ptr<ExprAST>> updateGuardCode;
  std::vector<std::unique_ptr<ExprAST>> onEntryCode;
  std::vector<std::unique_ptr<ExprAST>> onProcessCode;
  std::vector<std::unique_ptr<ExprAST>> onExitCode;
  std::vector<Transition> transitions;
};

enum class StateMachineField {
  ActiveStateId,
  TransitionRequestId,
  IsEntered,
  HistoryStateId
};

class StateMachine {
public:
  using Field = StateMachineField;

  StateMachine() = default;
  StateMachine(const StateMachine &) = delete;
  StateMachine &operator=(const StateMachine &) = delete;
  StateMachine(StateMachine &&) noexcept = default;
  StateMachine &operator=(StateMachine &&) noexcept = default;

  std::string name;
  std::shared_ptr<DeclarationNode> smDecl;
  std::vector<FlattenedState> flattenedStates;
  int initialStateId;
  std::string activeStateVarName;
  std::string transitionRequestVarName;
  std::string isEnteredVarName;

  static std::string getFieldSuffix(StateMachineField field);
  static std::string getVariableName(StateMachineField field,
                                     const std::string &prefix,
                                     const std::string &name);

  static void flattenStateMachine(std::shared_ptr<DeclarationNode> stateNode,
                                  int &idCounter, StateMachine &sm,
                                  const ScopeStack &scope, ASTNode tree,
                                  int parentId = -1);

  static std::optional<StateMachine>
  processStateMachine(std::shared_ptr<strd::BlockNode> child,
                      std::string prefix, const ScopeStack &scope,
                      ASTNode tree);
  static std::vector<StateMachine>
  collectStateMachines(std::shared_ptr<DeclarationNode> domainDecl,
                       ScopeStack &scope, ASTNode tree);

  const FlattenedState *findState(const std::string &stateName) const;
};

class StateMachineExprAST : public ExprAST {
public:
  StateMachineExprAST(StateMachine sm) : smContext(std::move(sm)) {}
  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;

private:
  StateMachine smContext;
};

} // namespace strd

#endif // STRIDE_STATEMACHINE_HPP
