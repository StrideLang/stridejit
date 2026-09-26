#include "stride/stridejit/statemachine.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>

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
    StateMachine &sm, ScopeStack &scope, ASTNode tree, int parentId) {
  if (!stateNode)
    return;

  FlattenedState fs;
  fs.id = idCounter++;
  fs.parentId = parentId;
  fs.stateDecl = stateNode;

  auto isParProp = stateNode->getPropertyValue("isParallel");
  if (isParProp && isParProp->getNodeType() == AST::Switch) {
    fs.isParallel = std::static_pointer_cast<ValueNode>(isParProp)->getSwitchValue();
  }

  auto resProp = stateNode->getPropertyValue("resumeLastState");
  if (resProp && resProp->getNodeType() == AST::Switch) {
    fs.resumeLastState = std::static_pointer_cast<ValueNode>(resProp)->getSwitchValue();
  }

  auto isFinProp = stateNode->getPropertyValue("isFinal");
  if (isFinProp && isFinProp->getNodeType() == AST::Switch) {
    fs.isFinal = std::static_pointer_cast<ValueNode>(isFinProp)->getSwitchValue();
  }

  if (fs.resumeLastState) {
    fs.historyStateVarName = "__" + sm.name + "_" + stateNode->getName() + "_history_state_id";
  }

  int currentId = fs.id;
  sm.flattenedStates.push_back(std::move(fs));

  auto statesProp = stateNode->getPropertyValue("states");
  if (statesProp && statesProp->getNodeType() == AST::List) {
    for (const auto &child : statesProp->getChildren()) {
      if (child->getNodeType() == AST::Block) {
        auto childName = std::static_pointer_cast<BlockNode>(child)->getName();
        auto childDecl =
            ASTQuery::findDeclarationByName(childName, scope, tree);
        if (childDecl) {
          flattenStateMachine(childDecl, idCounter, sm, scope, tree, currentId);
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
          sm.transitionRequestVarName = "__" + domainDecl->getName() + "_" +
                                        smName + "_transition_request_id";

          int idCounter = 1; // 0 usually means uninitialized or inactive
          flattenStateMachine(smDecl, idCounter, sm, scope, tree);

          // Second pass: Load transitions
          int transitionIdCounter = 1;
          for (auto &fs : sm.flattenedStates) {
            auto transProp = fs.stateDecl->getPropertyValue("transitions");
            if (transProp && transProp->getNodeType() == AST::List) {
              for (const auto &child : transProp->getChildren()) {
                std::shared_ptr<DeclarationNode> tDecl = nullptr;

                if (child->getNodeType() == AST::Block) {
                  auto refName =
                      std::static_pointer_cast<BlockNode>(child)->getName();
                  tDecl = ASTQuery::findDeclarationByName(refName, scope, tree);
                } else if (child->getNodeType() == AST::Declaration) {
                  tDecl = std::static_pointer_cast<DeclarationNode>(child);
                }

                if (tDecl) {
                  auto targetProp = tDecl->getPropertyValue("targetState");
                  if (targetProp && targetProp->getNodeType() == AST::Block) {
                    auto targetName =
                        std::static_pointer_cast<BlockNode>(targetProp)
                            ->getName();
                    int targetId = -1;
                    for (const auto &targetFs : sm.flattenedStates) {
                      if (targetFs.stateDecl->getName() == targetName) {
                        targetId = targetFs.id;
                        break;
                      }
                    }

                    if (targetId != -1) {
                      Transition t;
                      t.id = transitionIdCounter++;
                      t.transitionDecl = tDecl;
                      t.targetStateId = targetId;

                      auto trigProp = tDecl->getPropertyValue("triggerOnGuard");
                      if (trigProp && trigProp->getNodeType() == AST::Switch) {
                        auto valNode =
                            std::static_pointer_cast<ValueNode>(trigProp);
                        if (valNode->getSwitchValue()) {
                          t.triggerOnGuard = true;
                        }
                      }

                      fs.transitions.push_back(std::move(t));
                    }
                  }
                }
              }
            }
          }

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

  llvm::Value *activeStatePtr = nullptr;
  std::cout << "DEBUG: Looking for " << smContext.activeStateVarName << " in NamedValues...\n";
  auto activeStateIt = state.NamedValues.find(smContext.activeStateVarName);

  if (activeStateIt != state.NamedValues.end()) {
    activeStatePtr = activeStateIt->second.first;
  } else {
    activeStatePtr = state.TheModule->getNamedGlobal(smContext.activeStateVarName);
  }
  assert(activeStatePtr && "Active state variable global not found!");
  auto *activeStateVal =
      state.Builder->CreateLoad(state.Builder->getInt32Ty(), activeStatePtr);

  // 2. Create the master switch
  auto *endBB = llvm::BasicBlock::Create(*state.TheContext, "sm_end", func);
  auto *switchInst = state.Builder->CreateSwitch(
      activeStateVal, endBB, smContext.flattenedStates.size());

  // 3. Generate Basic Blocks for each state
  for (auto &fs : smContext.flattenedStates) {
    auto *stateBB = llvm::BasicBlock::Create(
        *state.TheContext, "state_" + std::to_string(fs.id), func, endBB);
    switchInst->addCase(state.Builder->getInt32(fs.id), stateBB);

    state.Builder->SetInsertPoint(stateBB);

    // Evaluate Transitions (Phase 4 Step 3)
    llvm::Value *reqVarPtr = nullptr;
    auto reqVarIt = state.NamedValues.find(smContext.transitionRequestVarName);
    if (reqVarIt != state.NamedValues.end()) {
      reqVarPtr = reqVarIt->second.first;
    } else {
      reqVarPtr = state.TheModule->getNamedGlobal(smContext.transitionRequestVarName);
    }
    
    llvm::Value *reqVal = nullptr;
    if (reqVarPtr) {
      reqVal =
          state.Builder->CreateLoad(state.Builder->getInt32Ty(), reqVarPtr);
    }

    llvm::BasicBlock *processBB = llvm::BasicBlock::Create(
        *state.TheContext, "state_" + std::to_string(fs.id) + "_process", func,
        endBB);

    for (auto &t : fs.transitions) {
      llvm::BasicBlock *transCondBB = llvm::BasicBlock::Create(
          *state.TheContext, "trans_" + std::to_string(t.id) + "_cond", func,
          processBB);
      llvm::BasicBlock *transFireBB = llvm::BasicBlock::Create(
          *state.TheContext, "trans_" + std::to_string(t.id) + "_fire", func,
          processBB);

      state.Builder->CreateBr(transCondBB);
      state.Builder->SetInsertPoint(transCondBB);

      llvm::Value *guardVal = state.Builder->getInt1(true);
      if (reqVal && !t.triggerOnGuard) {
        auto *idVal = state.Builder->getInt32(t.id);
        guardVal = state.Builder->CreateICmpEQ(reqVal, idVal);
      }

      if (!t.guardCode.empty()) {
        auto result = t.guardCode.back()->codegen(state);
        llvm::Value *CondV = result.first;
        if (CondV->getType()->isDoubleTy()) {
          CondV = state.Builder->CreateFCmpONE(
              CondV,
              llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(0.0)),
              "ifcond");
        } else if (CondV->getType()->isIntegerTy(32)) {
          CondV = state.Builder->CreateICmpNE(CondV, state.Builder->getInt32(0),
                                              "ifcond");
        }
        guardVal = state.Builder->CreateAnd(guardVal, CondV);
      }

      llvm::BasicBlock *nextBB = llvm::BasicBlock::Create(
          *state.TheContext, "trans_" + std::to_string(t.id) + "_next", func,
          processBB);
      state.Builder->CreateCondBr(guardVal, transFireBB, nextBB);

      state.Builder->SetInsertPoint(transFireBB);

      for (auto &expr : fs.onExitCode) {
        expr->codegen(state);
      }

      // Update parent history state if parent state has resumeLastState
      if (fs.parentId != -1) {
        for (auto &pFs : smContext.flattenedStates) {
          if (pFs.id == fs.parentId && pFs.resumeLastState && !pFs.historyStateVarName.empty()) {
            auto histIt = state.NamedValues.find(pFs.historyStateVarName);
            if (histIt != state.NamedValues.end()) {
              state.Builder->CreateStore(state.Builder->getInt32(fs.id), histIt->second.first);
            }
          }
        }
      }

      for (auto &expr : t.onTransitionCode) {
        expr->codegen(state);
      }

      // Handle transition to target state (checking resumeLastState history)
      for (auto &targetFs : smContext.flattenedStates) {
        if (targetFs.id == t.targetStateId) {
          if (targetFs.resumeLastState && !targetFs.historyStateVarName.empty()) {
            auto histIt = state.NamedValues.find(targetFs.historyStateVarName);
            if (histIt != state.NamedValues.end()) {
              auto *histVal = state.Builder->CreateLoad(state.Builder->getInt32Ty(), histIt->second.first);
              auto *hasHist = state.Builder->CreateICmpNE(histVal, state.Builder->getInt32(0));
              auto *chosenId = state.Builder->CreateSelect(hasHist, histVal, state.Builder->getInt32(t.targetStateId));
              state.Builder->CreateStore(chosenId, activeStatePtr);
            } else {
              state.Builder->CreateStore(state.Builder->getInt32(t.targetStateId), activeStatePtr);
            }
          } else {
            state.Builder->CreateStore(state.Builder->getInt32(t.targetStateId), activeStatePtr);
          }

          if (reqVarPtr) {
            state.Builder->CreateStore(state.Builder->getInt32(0), reqVarPtr);
          }

          for (auto &expr : targetFs.onEntryCode) {
            expr->codegen(state);
          }
          break;
        }
      }

      state.Builder->CreateBr(endBB);
      state.Builder->SetInsertPoint(nextBB);
    }

    state.Builder->CreateBr(processBB);
    state.Builder->SetInsertPoint(processBB);

    // Run onProcessCode
    for (auto &expr : fs.onProcessCode) {
      expr->codegen(state);
    }

    // If parallel state, also execute process code of all child states
    if (fs.isParallel) {
      for (auto &childFs : smContext.flattenedStates) {
        if (childFs.parentId == fs.id) {
          for (auto &expr : childFs.onProcessCode) {
            expr->codegen(state);
          }
        }
      }
    }

    state.Builder->CreateBr(endBB);
  }

  // Restore insertion point to endBB
  state.Builder->SetInsertPoint(endBB);

  return {nullptr, std::nullopt};
}
