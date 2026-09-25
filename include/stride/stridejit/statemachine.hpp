#ifndef STRIDE_STATEMACHINE_HPP
#define STRIDE_STATEMACHINE_HPP

#include <memory>
#include <string>
#include <vector>

namespace strd {

class DeclarationNode;
class AST;
using ASTNode = std::shared_ptr<AST>;
using ScopeStack = std::vector<std::pair<ASTNode, std::vector<ASTNode>>>;

struct FlattenedState {
  int id;
  std::shared_ptr<DeclarationNode> stateDecl;
};

class StateMachine {
public:
  StateMachine() = default;

  std::string name;
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
