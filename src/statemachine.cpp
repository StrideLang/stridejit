#include "stride/stridejit/statemachine.hpp"
#include "stride/stridejit/stridedebugemitter.hpp"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/stridejit/numberexprast.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/generatorrangenode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/rangenode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

namespace strd {

static int resolveLeafInitialState(int stateId, const StateMachine &sm,
                                   std::vector<int> &visited) {
  for (int v : visited) {
    if (v == stateId) {
      LOG_ERROR() << "Cycle detected in initialState hierarchy for state ID: "
                  << stateId << std::endl;
      return stateId;
    }
  }
  visited.push_back(stateId);
  for (const auto &fs : sm.flattenedStates) {
    if (fs.id == stateId) {
      auto initProp = fs.stateDecl->getPropertyValue("initialState");
      if (initProp && initProp->getNodeType() == AST::Entity) {
        auto initName =
            std::static_pointer_cast<EntityNode>(initProp)->getName();
        for (const auto &childFs : sm.flattenedStates) {
          if (childFs.stateDecl->getName() == initName) {
            return resolveLeafInitialState(childFs.id, sm, visited);
          }
        }
      }
      break;
    }
  }
  return stateId;
}

static int resolveLeafInitialState(int stateId, const StateMachine &sm) {
  std::vector<int> visited;
  return resolveLeafInitialState(stateId, sm, visited);
}

static bool isTrueProp(ASTNode prop) {
  if (!prop)
    return false;
  if (prop->getNodeType() == AST::Switch) {
    return std::static_pointer_cast<ValueNode>(prop)->getSwitchValue();
  }
  if (prop->getNodeType() == AST::Entity) {
    auto name = std::static_pointer_cast<EntityNode>(prop)->getName();
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

std::string StateMachine::getFieldSuffix(StateMachineField field) {
  switch (field) {
  case StateMachineField::ActiveStateId:
    return "_active_state_id";
  case StateMachineField::TransitionRequestId:
    return "_transition_request_id";
  case StateMachineField::TransitionRequestArg:
    return "_transition_request_arg";
  case StateMachineField::IsEntered:
    return "_is_entered";
  case StateMachineField::HistoryStateId:
    return "_history_state_id";
  }
  return "";
}

std::string StateMachine::getVariableName(StateMachineField field,
                                          const std::string &prefix,
                                          const std::string &name) {
  return "__" + prefix + "_" + name + getFieldSuffix(field);
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
    fs.historyStateVarName = StateMachine::getVariableName(
        StateMachineField::HistoryStateId, sm.name, stateNode->getName());
  }

  int currentId = fs.id;
  sm.flattenedStates.push_back(std::move(fs));

  auto statesProp = stateNode->getPropertyValue("states");
  if (statesProp && statesProp->getNodeType() == AST::List) {
    for (const auto &child : statesProp->getChildren()) {
      if (child->getNodeType() == AST::Entity) {
        auto childName = std::static_pointer_cast<EntityNode>(child)->getName();
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
StateMachine::processStateMachine(std::shared_ptr<EntityNode> child,
                                  std::string prefix, const ScopeStack &scope,
                                  ASTNode tree) {
  auto smName = child->getName();
  auto smDecl = ASTQuery::findDeclarationByName(smName, scope, tree);
  if (smDecl && ASTQuery::isStateNode(smDecl, scope, tree)) {
    StateMachine sm;
    sm.name = smName;
    sm.smDecl = smDecl;
    sm.activeStateVarName = StateMachine::getVariableName(
        StateMachineField::ActiveStateId, prefix, smName);
    sm.transitionRequestVarName = StateMachine::getVariableName(
        StateMachineField::TransitionRequestId, prefix, smName);
    sm.transitionRequestArgVarName = StateMachine::getVariableName(
        StateMachineField::TransitionRequestArg, prefix, smName);
    sm.isEnteredVarName = StateMachine::getVariableName(
        StateMachineField::IsEntered, prefix, smName);

    int idCounter = 1; // 0 usually means uninitialized or inactive
    flattenStateMachine(smDecl, idCounter, sm, scope, tree);

    // Second pass: Load transitions
    int transitionIdCounter = 1;
    for (auto &fs : sm.flattenedStates) {
      auto transProp = fs.stateDecl->getPropertyValue("transitions");
      if (transProp && transProp->getNodeType() == AST::List) {
        for (const auto &child : transProp->getChildren()) {
          std::shared_ptr<DeclarationNode> tDecl = nullptr;

          if (child->getNodeType() == AST::Entity) {
            auto refName =
                std::static_pointer_cast<EntityNode>(child)->getName();
            tDecl = ASTQuery::findDeclarationByName(refName, scope, tree);
          } else if (child->getNodeType() == AST::Declaration) {
            tDecl = std::static_pointer_cast<DeclarationNode>(child);
          }

          if (tDecl) {
            auto targetProp = tDecl->getPropertyValue("targetState");
            if (targetProp && targetProp->getNodeType() == AST::Entity) {
              auto targetName =
                  std::static_pointer_cast<EntityNode>(targetProp)->getName();
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

                auto isGenProp = tDecl->getCompilerProperty("_isGenerator");
                if (isGenProp && isGenProp->getNodeType() == AST::Switch &&
                    std::static_pointer_cast<ValueNode>(isGenProp)->getSwitchValue()) {
                  t.isGenerator = true;
                  auto varProp = tDecl->getCompilerProperty("_generatorVar");
                  if (varProp && varProp->getNodeType() == AST::String) {
                    t.generatorVar =
                        std::static_pointer_cast<ValueNode>(varProp)->getStringValue();
                  }
                  auto rangeProp = tDecl->getCompilerProperty("_generatorRange");
                  if (rangeProp &&
                      rangeProp->getNodeType() == AST::GeneratorRange) {
                    auto genNode =
                        std::static_pointer_cast<GeneratorRangeNode>(rangeProp);
                    if (genNode->startIndex() &&
                        genNode->startIndex()->getNodeType() == AST::Int) {
                      t.rangeStart = std::static_pointer_cast<ValueNode>(
                                         genNode->startIndex())
                                         ->getIntValue();
                    }
                    if (genNode->endIndex() &&
                        genNode->endIndex()->getNodeType() == AST::Int) {
                      t.rangeEnd = std::static_pointer_cast<ValueNode>(
                                       genNode->endIndex())
                                       ->getIntValue();
                    }
                    if (genNode->stepIndex() &&
                        genNode->stepIndex()->getNodeType() == AST::Int) {
                      t.rangeStep = std::static_pointer_cast<ValueNode>(
                                        genNode->stepIndex())
                                        ->getIntValue();
                    }
                  } else if (rangeProp &&
                             rangeProp->getNodeType() == AST::Range) {
                    auto rNode = std::static_pointer_cast<RangeNode>(rangeProp);
                    if (rNode->startIndex()->getNodeType() == AST::Int) {
                      t.rangeStart = std::static_pointer_cast<ValueNode>(
                                         rNode->startIndex())
                                         ->getIntValue();
                    }
                    if (rNode->endIndex()->getNodeType() == AST::Int) {
                      t.rangeEnd = std::static_pointer_cast<ValueNode>(
                                       rNode->endIndex())
                                       ->getIntValue();
                    }
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
    if (initProp && initProp->getNodeType() == AST::Entity) {
      auto initName = std::static_pointer_cast<EntityNode>(initProp)->getName();
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
      if (child->getNodeType() == AST::Entity) {
        auto stateMachine =
            processStateMachine(std::static_pointer_cast<EntityNode>(child),
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

const FlattenedState *
StateMachine::findState(const std::string &stateName) const {
  for (const auto &fs : flattenedStates) {
    if (fs.stateDecl && fs.stateDecl->getName() == stateName) {
      return &fs;
    }
  }
  return nullptr;
}

} // namespace strd

std::pair<llvm::Value *, std::optional<llvm::Type *>>
strd::StateMachineExprAST::codegen(strd::StrideCompiler &state) {
  auto *func = state.Builder->GetInsertBlock()->getParent();

  // 1. Load activeStateVar
  llvm::Value *activeStatePtr = nullptr;
  auto activeStateIt = state.NamedValues.find(smContext.activeStateVarName);

  if (activeStateIt != state.NamedValues.end()) {
    activeStatePtr = activeStateIt->second.first;
  } else {
    activeStatePtr =
        state.TheModule->getNamedGlobal(smContext.activeStateVarName);
  }
  assert(activeStatePtr && "Active state variable global not found!");

  // 2. Check and handle initial entry
  llvm::Value *isEnteredPtr = nullptr;
  if (!smContext.isEnteredVarName.empty()) {
    auto isEnteredIt = state.NamedValues.find(smContext.isEnteredVarName);
    if (isEnteredIt != state.NamedValues.end()) {
      isEnteredPtr = isEnteredIt->second.first;
    } else {
      isEnteredPtr =
          state.TheModule->getNamedGlobal(smContext.isEnteredVarName);
    }
  }

  if (isEnteredPtr) {
    auto *initialEntryBB =
        llvm::BasicBlock::Create(*state.TheContext, "sm_initial_entry", func);
    auto *switchEntryBB =
        llvm::BasicBlock::Create(*state.TheContext, "sm_switch_entry", func);

    auto *isEnteredVal =
        state.Builder->CreateLoad(state.Builder->getInt1Ty(), isEnteredPtr);
    state.Builder->CreateCondBr(isEnteredVal, switchEntryBB, initialEntryBB);

    state.Builder->SetInsertPoint(initialEntryBB);

    // Run onEntryCode for initial state ancestry top-down
    std::vector<const FlattenedState *> initAncestry;
    int ancId = smContext.initialStateId;
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
      initAncestry.push_back(aFs);
      ancId = aFs->parentId;
    }
    std::reverse(initAncestry.begin(), initAncestry.end());

    for (const auto *aFs : initAncestry) {
      for (auto &expr : aFs->onEntryCode) {
        if (state.debugEmitter && expr->getLine() > 0) {
          state.debugEmitter->emitLocation(*state.Builder, expr->getLine(),
                                           expr->getColumn());
        }
        expr->codegen(state);
      }
    }

    state.Builder->CreateStore(state.Builder->getInt1(true), isEnteredPtr);
    state.Builder->CreateBr(switchEntryBB);

    state.Builder->SetInsertPoint(switchEntryBB);
  }

  auto *activeStateVal =
      state.Builder->CreateLoad(state.Builder->getInt32Ty(), activeStatePtr);

  // 3. Create the master switch
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

    llvm::Value *reqArgVarPtr = nullptr;
    if (!smContext.transitionRequestArgVarName.empty()) {
      auto reqArgVarIt =
          state.NamedValues.find(smContext.transitionRequestArgVarName);
      if (reqArgVarIt != state.NamedValues.end()) {
        reqArgVarPtr = reqArgVarIt->second.first;
      } else {
        reqArgVarPtr = state.TheModule->getNamedGlobal(
            smContext.transitionRequestArgVarName);
      }
    }
    llvm::Value *reqArgVal = nullptr;
    if (reqArgVarPtr) {
      reqArgVal =
          state.Builder->CreateLoad(state.Builder->getInt32Ty(), reqArgVarPtr);
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

      // If transition is a generator, also bound-check reqArgVal in range [rangeStart, rangeEnd]
      if (t.isGenerator && reqArgVal) {
        auto *geStart = state.Builder->CreateICmpSGE(
            reqArgVal, state.Builder->getInt32(t.rangeStart));
        auto *leEnd = state.Builder->CreateICmpSLE(
            reqArgVal, state.Builder->getInt32(t.rangeEnd));
        auto *inRange = state.Builder->CreateAnd(geStart, leEnd);
        if (std::abs(t.rangeStep) > 1) {
          auto *diff = state.Builder->CreateSub(
              reqArgVal, state.Builder->getInt32(t.rangeStart));
          auto *rem = state.Builder->CreateSRem(
              diff, state.Builder->getInt32(t.rangeStep));
          auto *stepMatch = state.Builder->CreateICmpEQ(
              rem, state.Builder->getInt32(0));
          inRange = state.Builder->CreateAnd(inRange, stepMatch);
        }
        guardVal = state.Builder->CreateAnd(guardVal, inRange);

        // Temporarily bind generator variable in state.NamedValues so guardCode can evaluate with reqArgVal
        if (!t.generatorVar.empty()) {
          state.NamedValues[t.generatorVar] = {reqArgVal,
                                               state.Builder->getInt32Ty()};
        }
      }

      if (!t.guardCode.empty()) {
        auto *varExpr =
            dynamic_cast<VariableExprAST *>(t.guardCode.back().get());
        auto result = t.guardCode.back()->codegen(state);
        llvm::Value *CondV = result.first;
        llvm::Type *valType = result.second.value_or(nullptr);
        if (CondV) {
          if (varExpr && !varExpr->getIndices().empty() &&
              CondV->getType()->isPointerTy()) {
            std::vector<llvm::Value *> idxList;
            for (const auto &idx : varExpr->getIndices()) {
              const size_t *intIdx = std::get_if<size_t>(&idx);
              if (intIdx) {
                idxList.push_back(llvm::ConstantInt::get(
                    *state.TheContext, llvm::APInt(64, *intIdx)));
              }
              const std::string *strIdx = std::get_if<std::string>(&idx);
              if (strIdx) {
                if (t.isGenerator && *strIdx == t.generatorVar && reqArgVal) {
                  idxList.push_back(reqArgVal);
                } else {
                  auto it = state.NamedValues.find(*strIdx);
                  if (it != state.NamedValues.end()) {
                    llvm::Value *indexVal = it->second.first;
                    if (indexVal->getType()->isPointerTy()) {
                      indexVal = state.Builder->CreateLoad(
                          it->second.second.value(), indexVal, *strIdx);
                    }
                    idxList.push_back(indexVal);
                  }
                }
              }
            }
            if (!idxList.empty()) {
              llvm::Type *gepElemType =
                  valType ? valType : state.Builder->getInt32Ty();
              if (gepElemType->isArrayTy()) {
                gepElemType = static_cast<llvm::ArrayType *>(gepElemType)
                                  ->getElementType();
              }
              CondV = state.Builder->CreateGEP(gepElemType, CondV, idxList);
              CondV = state.Builder->CreateLoad(gepElemType, CondV,
                                                varExpr->getName());
            }
          } else if (CondV->getType()->isPointerTy()) {
            llvm::Type *loadType =
                valType ? valType : state.Builder->getInt32Ty();
            CondV = state.Builder->CreateLoad(loadType, CondV);
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
          if (state.debugEmitter && expr->getLine() > 0) {
            state.debugEmitter->emitLocation(*state.Builder, expr->getLine(),
                                             expr->getColumn());
          }
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

      bool hasElementContext = false;
      if (t.isGenerator && !t.generatorVar.empty() && reqArgVal) {
        state.NamedValues[t.generatorVar] = {reqArgVal,
                                             state.Builder->getInt32Ty()};
        if (state.isDebugInfoEnabled() && state.debugEmitter) {
          ElementContext ctx;
          ctx.kind = ElementContext::Kind::Generator;
          ctx.varName = t.generatorVar;
          ctx.currentVal = reqArgVal;
          ctx.totalCount =
              (t.rangeEnd >= t.rangeStart) ? (t.rangeEnd - t.rangeStart + 1) : 0;
          state.debugEmitter->pushElementContext(ctx);
          std::string relFile =
              t.transitionDecl ? t.transitionDecl->getRelativeFilename() : "";
          std::string absFile =
              t.transitionDecl ? t.transitionDecl->getAbsoluteFilename() : "";
          int line = t.transitionDecl ? t.transitionDecl->getLine() : -1;
          int col = t.transitionDecl ? t.transitionDecl->getColumn() : -1;
          state.debugEmitter->emitElementContextVariable(
              *state.Builder, relFile, absFile, line, col);
          hasElementContext = true;
        }
      }

      for (auto &expr : t.onTransitionCode) {
        if (state.debugEmitter && expr->getLine() > 0) {
          state.debugEmitter->emitLocation(*state.Builder, expr->getLine(),
                                           expr->getColumn());
        }
        expr->codegen(state);
      }

      if (hasElementContext) {
        state.debugEmitter->popElementContext();
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
      if (reqArgVarPtr) {
        state.Builder->CreateStore(state.Builder->getInt32(0), reqArgVarPtr);
      }

      for (const auto &s : smContext.flattenedStates) {
        if (s.id == resolvedTargetId) {
          std::vector<const FlattenedState *> targetAncestry;
          int ancId = s.id;
          while (ancId != -1) {
            const FlattenedState *aFs = nullptr;
            for (const auto &cand : smContext.flattenedStates) {
              if (cand.id == ancId) {
                aFs = &cand;
                break;
              }
            }
            if (!aFs)
              break;
            targetAncestry.push_back(aFs);
            ancId = aFs->parentId;
          }
          std::reverse(targetAncestry.begin(), targetAncestry.end());

          for (const auto *aFs : targetAncestry) {
            for (auto &expr : aFs->updateGuardCode) {
              if (state.debugEmitter && expr->getLine() > 0) {
                state.debugEmitter->emitLocation(*state.Builder, expr->getLine(),
                                                 expr->getColumn());
              }
              expr->codegen(state);
            }
          }

          for (const auto *aFs : targetAncestry) {
            for (auto &expr : aFs->onEntryCode) {
              if (state.debugEmitter && expr->getLine() > 0) {
                state.debugEmitter->emitLocation(*state.Builder, expr->getLine(),
                                                 expr->getColumn());
              }
              expr->codegen(state);
            }
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
      if (state.debugEmitter && expr->getLine() > 0) {
        state.debugEmitter->emitLocation(*state.Builder, expr->getLine(),
                                         expr->getColumn());
      }
      expr->codegen(state);
    }

    // If parallel state, also execute process code of all child states
    if (fs.isParallel) {
      for (auto &childFs : smContext.flattenedStates) {
        if (childFs.parentId == fs.id) {
          for (auto &expr : childFs.onProcessCode) {
            if (state.debugEmitter && expr->getLine() > 0) {
              state.debugEmitter->emitLocation(*state.Builder, expr->getLine(),
                                               expr->getColumn());
            }
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
