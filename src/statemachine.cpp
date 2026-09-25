#include "stride/stridejit/statemachine.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#include "stride/parser/ast.h"
#include "stride/parser/blocknode.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

namespace strd {

void StateMachine::flattenStateMachine(
    std::shared_ptr<DeclarationNode> stateNode, int &idCounter,
    StateMachine &sm, ScopeStack &scope, ASTNode tree) {
  if (!stateNode)
    return;

  FlattenedState fs;
  fs.id = idCounter++;
  fs.stateDecl = stateNode;
  sm.flattenedStates.push_back(std::move(fs));

  auto statesProp = stateNode->getPropertyValue("states");
  if (statesProp && statesProp->getNodeType() == AST::List) {
    for (const auto &child : statesProp->getChildren()) {
      if (child->getNodeType() == AST::Block) {
        auto childName = std::static_pointer_cast<BlockNode>(child)->getName();
        auto childDecl =
            ASTQuery::findDeclarationByName(childName, scope, tree);
        if (childDecl) {
          flattenStateMachine(childDecl, idCounter, sm, scope, tree);
        }
      }
    }
  }
}

std::vector<StateMachine>
StateMachine::collectStateMachines(std::shared_ptr<DeclarationNode> domainDecl,
                                   ScopeStack &scope, ASTNode tree) {
  std::vector<StateMachine> machines;
  if (!domainDecl)
    return machines;

  auto smProperty = domainDecl->getPropertyValue("stateMachines");
  if (smProperty && smProperty->getNodeType() == AST::List) {
    for (const auto &child : smProperty->getChildren()) {
      if (child->getNodeType() == AST::Block) {
        auto smName = std::static_pointer_cast<BlockNode>(child)->getName();
        auto smDecl = ASTQuery::findDeclarationByName(smName, scope, tree);
        if (smDecl && ASTQuery::isStateNode(smDecl, scope, tree)) {
          StateMachine sm;
          sm.name = smName;
          sm.smDecl = smDecl;
          sm.activeStateVarName =
              "__" + domainDecl->getName() + "_" + smName + "_active_state_id";

          int idCounter = 1; // 0 usually means uninitialized or inactive
          flattenStateMachine(smDecl, idCounter, sm, scope, tree);

          // Determine initial state
          sm.initialStateId = 1; // Default to the root state ID itself
          auto initProp = smDecl->getPropertyValue("initialState");
          if (initProp && initProp->getNodeType() == AST::Block) {
            auto initName =
                std::static_pointer_cast<BlockNode>(initProp)->getName();
            for (const auto &fs : sm.flattenedStates) {
              if (fs.stateDecl->getName() == initName) {
                sm.initialStateId = fs.id;
                break;
              }
            }
          }
          machines.push_back(std::move(sm));
        }
      }
    }
  }
  return machines;
}

} // namespace strd

std::pair<llvm::Value *, std::optional<llvm::Type *>> 
strd::StateMachineExprAST::codegen(strd::StrideCompiler &state) {
  auto *func = state.Builder->GetInsertBlock()->getParent();
  
  // 1. Load activeStateVar
  llvm::GlobalVariable *activeStatePtr = state.TheModule->getNamedGlobal(smContext.activeStateVarName);
  assert(activeStatePtr && "Active state variable global not found!");
  auto *activeStateVal = state.Builder->CreateLoad(state.Builder->getInt32Ty(), activeStatePtr);

  // 2. Create the master switch
  auto *endBB = llvm::BasicBlock::Create(*state.TheContext, "sm_end", func);
  auto *switchInst = state.Builder->CreateSwitch(activeStateVal, endBB, smContext.flattenedStates.size());

  // 3. Generate Basic Blocks for each state
  for (auto &fs : smContext.flattenedStates) {
    auto *stateBB = llvm::BasicBlock::Create(*state.TheContext, "state_" + std::to_string(fs.id), func, endBB);
    switchInst->addCase(state.Builder->getInt32(fs.id), stateBB);

    state.Builder->SetInsertPoint(stateBB);
    
    // Evaluate Transitions (Phase 4 Step 2)
    // TODO: Transition logic goes here
    
    // Run onProcessCode
    for (auto &expr : fs.onProcessCode) {
      expr->codegen(state);
    }
    
    state.Builder->CreateBr(endBB);
  }

  // Restore insertion point to endBB
  state.Builder->SetInsertPoint(endBB);
  
  return {nullptr, std::nullopt};
}
