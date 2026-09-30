#include "stride/stridejit/statemachine.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

namespace strd {

static int resolveLeafInitialState(int stateId, const StateMachine &sm) {
  for (const auto &fs : sm.flattenedStates) {
    if (fs.id == stateId) {
      auto initProp = fs.stateDecl->getPropertyValue("initialState");
      if (initProp && initProp->getNodeType() == AST::Block) {
        auto initName =
            std::static_pointer_cast<BlockNode>(initProp)->getName();
        for (const auto &childFs : sm.flattenedStates) {
          if (childFs.stateDecl->getName() == initName) {
            return resolveLeafInitialState(childFs.id, sm);
          }
        }
      }
      break;
    }
  }
  return stateId;
}

static bool isTrueProp(ASTNode prop) {
  if (!prop)
    return false;
  if (prop->getNodeType() == AST::Switch) {
    return std::static_pointer_cast<ValueNode>(prop)->getSwitchValue();
  }
  if (prop->getNodeType() == AST::Block) {
    auto name = std::static_pointer_cast<BlockNode>(prop)->getName();
    return (name == "true" || name == "on" || name == "1" || name == "ON" ||
            name == "TRUE");
  }
  if (prop->getNodeType() == AST::Int) {
    return std::static_pointer_cast<ValueNode>(prop)->getIntValue() != 0;
  }
  if (prop->getNodeType() == AST::String) {
    auto str = std::static_pointer_cast<ValueNode>(prop)->getStringValue();
    return (str == "true" || str == "on" || str == "1");
  }
  return false;
}

void StateMachine::flattenStateMachine(
    std::shared_ptr<DeclarationNode> stateNode, int &idCounter,
    StateMachine &sm, const ScopeStack &scope, ASTNode tree, int parentId) {
  if (!stateNode)
    return;

  FlattenedState fs;
  fs.id = idCounter++;
  fs.parentId = parentId;
  fs.stateDecl = stateNode;

  auto isParProp = stateNode->getPropertyValue("isParallel");
  if (isParProp && isParProp->getNodeType() == AST::Switch) {
    fs.isParallel =
        std::static_pointer_cast<ValueNode>(isParProp)->getSwitchValue();
  }

  auto resProp = stateNode->getPropertyValue("resumeLastState");
  if (resProp && resProp->getNodeType() == AST::Switch) {
    fs.resumeLastState =
        std::static_pointer_cast<ValueNode>(resProp)->getSwitchValue();
  }

  auto isFinProp = stateNode->getPropertyValue("isFinal");
  if (isFinProp && isFinProp->getNodeType() == AST::Switch) {
    fs.isFinal =
        std::static_pointer_cast<ValueNode>(isFinProp)->getSwitchValue();
  }

  auto updGuardDomProp = stateNode->getPropertyValue("updateGuardOnDomain");
  if (updGuardDomProp && updGuardDomProp->getNodeType() == AST::Switch) {
    fs.updateGuardOnDomain =
        std::static_pointer_cast<ValueNode>(updGuardDomProp)->getSwitchValue();
  }

  if (fs.resumeLastState) {
    fs.historyStateVarName =
        "__" + sm.name + "_" + stateNode->getName() + "_history_state_id";
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

std::optional<StateMachine>
StateMachine::processStateMachine(std::shared_ptr<BlockNode> child,
                                  std::string prefix, const ScopeStack &scope,
                                  ASTNode tree) {
  auto smName = child->getName();
  auto smDecl = ASTQuery::findDeclarationByName(smName, scope, tree);
  if (smDecl && ASTQuery::isStateNode(smDecl, scope, tree)) {
    StateMachine sm;
    sm.name = smName;
    sm.smDecl = smDecl;
    // TODO encapsulate this name generation
    sm.activeStateVarName = "__" + prefix + "_" + smName + "_active_state_id";
    sm.transitionRequestVarName =
        "__" + prefix + "_" + smName + "_transition_request_id";

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
                  std::static_pointer_cast<BlockNode>(targetProp)->getName();
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
                  auto valNode = std::static_pointer_cast<ValueNode>(trigProp);
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
      auto initName = std::static_pointer_cast<BlockNode>(initProp)->getName();
      for (const auto &fs : sm.flattenedStates) {
        if (fs.stateDecl->getName() == initName) {
          sm.initialStateId = fs.id;
          break;
        }
      }
    }
    sm.initialStateId = resolveLeafInitialState(sm.initialStateId, sm);
    return sm;
  }
  return std::nullopt;
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
        auto stateMachine =
            processStateMachine(std::static_pointer_cast<BlockNode>(child),
                                domainDecl->getName(), scope, tree);
        if (stateMachine.has_value()) {
          machines.push_back(std::move(stateMachine.value()));
        } else {
          LOG_ERROR() << "Could not process state machine: " << child->toText()
                      << std::endl;
        }
      } else {
        LOG_ERROR() << "Could not process state machine: " << child->toText()
                    << std::endl;
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
  std::cout << "DEBUG: Looking for " << smContext.activeStateVarName
            << " in NamedValues...\n";
  auto activeStateIt = state.NamedValues.find(smContext.activeStateVarName);

  if (activeStateIt != state.NamedValues.end()) {
    activeStatePtr = activeStateIt->second.first;
  } else {
    activeStatePtr =
        state.TheModule->getNamedGlobal(smContext.activeStateVarName);
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

    // Execute updateGuard streams top-down (parent state first, then child
    // state)
    std::vector<const FlattenedState *> topDownAncestry;
    int ancId = fs.id;
    while (ancId != -1) {
      const FlattenedState *aFs = nullptr;
      for (const auto &s : smContext.flattenedStates) {
        if (s.id == ancId) {
          aFs = &s;
          break;
        }
      }
      if (!aFs)
        break;
      topDownAncestry.push_back(aFs);
      ancId = aFs->parentId;
    }
    std::reverse(topDownAncestry.begin(), topDownAncestry.end());

    for (const auto *aFs : topDownAncestry) {
      for (auto &expr : aFs->updateGuardCode) {
        expr->codegen(state);
      }
    }

    // Evaluate Transitions (Phase 4 Step 3)
    llvm::Value *reqVarPtr = nullptr;
    auto reqVarIt = state.NamedValues.find(smContext.transitionRequestVarName);
    if (reqVarIt != state.NamedValues.end()) {
      reqVarPtr = reqVarIt->second.first;
    } else {
      reqVarPtr =
          state.TheModule->getNamedGlobal(smContext.transitionRequestVarName);
    }

    llvm::Value *reqVal = nullptr;
    if (reqVarPtr) {
      reqVal =
          state.Builder->CreateLoad(state.Builder->getInt32Ty(), reqVarPtr);
    }

    llvm::BasicBlock *processBB = llvm::BasicBlock::Create(
        *state.TheContext, "state_" + std::to_string(fs.id) + "_process", func,
        endBB);

    struct ApplicableTransition {
      const Transition *trans;
      const FlattenedState *ownerState;
    };
    std::vector<ApplicableTransition> applicableTransitions;
    int currId = fs.id;
    while (currId != -1) {
      const FlattenedState *currFs = nullptr;
      for (const auto &s : smContext.flattenedStates) {
        if (s.id == currId) {
          currFs = &s;
          break;
        }
      }
      if (!currFs)
        break;
      for (const auto &t : currFs->transitions) {
        applicableTransitions.push_back({&t, currFs});
      }
      currId = currFs->parentId;
    }

    for (auto &appTrans : applicableTransitions) {
      auto &t = *appTrans.trans;
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
        if (CondV) {
          if (CondV->getType()->isPointerTy()) {
            CondV =
                state.Builder->CreateLoad(state.Builder->getInt32Ty(), CondV);
          }
          if (CondV->getType()->isIntegerTy(1)) {
            // Already i1 boolean
          } else if (CondV->getType()->isIntegerTy()) {
            CondV = state.Builder->CreateICmpNE(
                CondV, llvm::ConstantInt::get(CondV->getType(), 0), "ifcond");
          } else if (CondV->getType()->isDoubleTy()) {
            CondV = state.Builder->CreateFCmpONE(
                CondV,
                llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(0.0)),
                "ifcond");
          }
          guardVal = state.Builder->CreateAnd(guardVal, CondV);
        }
      }

      llvm::BasicBlock *nextBB = llvm::BasicBlock::Create(
          *state.TheContext, "trans_" + std::to_string(t.id) + "_next", func,
          processBB);
      state.Builder->CreateCondBr(guardVal, transFireBB, nextBB);

      state.Builder->SetInsertPoint(transFireBB);

      // Collect states being exited from active state fs up to
      // appTrans.ownerState
      std::vector<const FlattenedState *> exitedStates;
      int walkId = fs.id;
      while (walkId != -1) {
        const FlattenedState *wFs = nullptr;
        for (const auto &s : smContext.flattenedStates) {
          if (s.id == walkId) {
            wFs = &s;
            break;
          }
        }
        if (!wFs)
          break;
        exitedStates.push_back(wFs);
        if (wFs->id == appTrans.ownerState->id) {
          break;
        }
        walkId = wFs->parentId;
      }

      for (const auto *eFs : exitedStates) {
        for (auto &expr : eFs->onExitCode) {
          expr->codegen(state);
        }

        // Update parent history state if parent state has resumeLastState
        if (eFs->parentId != -1) {
          for (auto &pFs : smContext.flattenedStates) {
            if (pFs.id == eFs->parentId && pFs.resumeLastState &&
                !pFs.historyStateVarName.empty()) {
              auto histIt = state.NamedValues.find(pFs.historyStateVarName);
              if (histIt != state.NamedValues.end()) {
                state.Builder->CreateStore(state.Builder->getInt32(fs.id),
                                           histIt->second.first);
              }
            }
          }
        }
        if (eFs->resumeLastState && !eFs->historyStateVarName.empty()) {
          auto histIt = state.NamedValues.find(eFs->historyStateVarName);
          if (histIt != state.NamedValues.end()) {
            state.Builder->CreateStore(state.Builder->getInt32(fs.id),
                                       histIt->second.first);
          }
        }
      }

      for (auto &expr : t.onTransitionCode) {
        expr->codegen(state);
      }

      // Handle transition to target state (checking resumeLastState history)
      int resolvedTargetId =
          resolveLeafInitialState(t.targetStateId, smContext);
      const FlattenedState *targetFs = nullptr;
      for (const auto &s : smContext.flattenedStates) {
        if (s.id == t.targetStateId) {
          targetFs = &s;
          break;
        }
      }

      llvm::Value *finalTargetIdVal = state.Builder->getInt32(resolvedTargetId);
      if (targetFs && targetFs->resumeLastState &&
          !targetFs->historyStateVarName.empty()) {
        auto histIt = state.NamedValues.find(targetFs->historyStateVarName);
        if (histIt != state.NamedValues.end()) {
          auto *histVal = state.Builder->CreateLoad(state.Builder->getInt32Ty(),
                                                    histIt->second.first);
          auto *hasHist =
              state.Builder->CreateICmpNE(histVal, state.Builder->getInt32(0));
          finalTargetIdVal =
              state.Builder->CreateSelect(hasHist, histVal, finalTargetIdVal);
        }
      }

      state.Builder->CreateStore(finalTargetIdVal, activeStatePtr);
      if (reqVarPtr) {
        state.Builder->CreateStore(state.Builder->getInt32(0), reqVarPtr);
      }

      for (const auto &s : smContext.flattenedStates) {
        if (s.id == resolvedTargetId) {
          for (auto &expr : s.onEntryCode) {
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
