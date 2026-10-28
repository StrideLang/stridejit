#include "stride/utils/logger.h"
#include <algorithm>
#include <iostream>
#include <optional>
#include <set>

#include "stride/stridejit/binaryexprast.hpp"
#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/functionast.hpp"
#include "stride/stridejit/listexprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// stride
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astquery.h"

// llvm
#include "llvm/IR/Module.h"
// #include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

using namespace strd;

namespace {

struct ExternalCallTemplate {
  std::string symbol;
  // Input token index for each operand listed after the symbol, in call
  // order. Empty when the template lists no operands (e.g. "@fn").
  std::vector<size_t> inTokenOrder;
  bool operandsParsed{true};
};

// Parses a platformModule processing string that names an external C symbol
// with the '@' prefix. Accepted forms:
//   "@fn"
//   "@fn %%intokens:0%%, %%intokens:1%%"
//   "%%outtokens:0%% = @fn %%intokens:0%%, %%intokens:1%%"
// Native instructions ("llvm::...") are not external calls.
std::optional<ExternalCallTemplate>
parseExternalCallTemplate(const std::string &processing) {
  if (processing.find("llvm::") != std::string::npos) {
    return std::nullopt;
  }
  size_t atPos = processing.find('@');
  if (atPos == std::string::npos) {
    return std::nullopt;
  }
  size_t nameEnd = processing.find_first_of(" \t\r\n(,", atPos + 1);
  ExternalCallTemplate result;
  result.symbol = processing.substr(atPos + 1, nameEnd == std::string::npos
                                                   ? std::string::npos
                                                   : nameEnd - (atPos + 1));
  if (result.symbol.empty()) {
    return std::nullopt;
  }
  if (nameEnd == std::string::npos) {
    return result;
  }

  std::string operands = processing.substr(nameEnd);
  operands.erase(std::remove(operands.begin(), operands.end(), '('),
                 operands.end());
  operands.erase(std::remove(operands.begin(), operands.end(), ')'),
                 operands.end());
  const std::string prefix = "%%intokens:";
  const std::string suffix = "%%";
  size_t start = 0;
  while (start <= operands.size()) {
    size_t comma = operands.find(',', start);
    std::string op = operands.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    op.erase(0, op.find_first_not_of(" \t\r\n"));
    size_t last = op.find_last_not_of(" \t\r\n");
    op.erase(last == std::string::npos ? 0 : last + 1);
    if (!op.empty()) {
      if (op.size() > prefix.size() + suffix.size() &&
          op.compare(0, prefix.size(), prefix) == 0 &&
          op.compare(op.size() - suffix.size(), suffix.size(), suffix) == 0) {
        std::string idxStr = op.substr(
            prefix.size(), op.size() - prefix.size() - suffix.size());
        try {
          result.inTokenOrder.push_back(
              static_cast<size_t>(std::stoul(idxStr)));
        } catch (...) {
          result.operandsParsed = false;
        }
      } else {
        result.operandsParsed = false;
      }
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return result;
}

} // namespace

void StrideGenerator::compile(ASTNode tree, ScopeStack &scope,
                              StrideCompiler &state) {
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration ||
        node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "platformModule") {
        StrideGenerator::generatePlatformFunctionSignature(
            decl, scope.back().second, state);
      }
    }
  }

  state.stateMachineInfos.clear();
  GeneratedIRCode generatedIRCode = generateCodeForTree(tree, scope, state);

  // Domain member variables (globals)
  // TODO set global linkage according to debug mode
  for (const auto &globalDecl : generatedIRCode.GlobalSignals) {
    state.createGlobal(globalDecl);
  }

  for (auto it = generatedIRCode.domainGeneratedCode.begin();
       it != generatedIRCode.domainGeneratedCode.end(); it++) {
    std::vector<PrototypeArg> ExternalArgs; // Args to domain functions
    const std::string &domainName = it->first;
    // Find domain and insert inputs and output to tree.
    auto domainDecl = ASTQuery::findDeclarationByName(domainName, scope, tree);
    if (domainDecl) {
      LOG_DEBUG() << " Found domain declaration for " << domainName
                  << std::endl;
      auto smContexts =
          StateMachine::collectStateMachines(domainDecl, scope, tree);

      for (auto &smCtx : smContexts) {
        StrideCompiler::StateMachineInfo smInfo;
        smInfo.domainName = domainName;
        smInfo.smName = smCtx.name;
        for (const auto &fs : smCtx.flattenedStates) {
          std::string stateName = fs.stateDecl ? fs.stateDecl->getName() : "";
          if (fs.stateDecl) {
            smInfo.stateIdsByName[fs.stateDecl->getName()] = fs.id;
          }
          for (const auto &t : fs.transitions) {
            if (t.transitionDecl) {
              if (!t.transitionDecl->getName().empty()) {
                smInfo.transitionIdsByName[t.transitionDecl->getName()] = t.id;
                if (!stateName.empty()) {
                  smInfo.transitionIdsByStateAndName[{
                      stateName, t.transitionDecl->getName()}] = t.id;
                }
              }
              auto labelProp = t.transitionDecl->getPropertyValue("label");
              if (labelProp && labelProp->getNodeType() == AST::String) {
                std::string labelStr =
                    std::static_pointer_cast<ValueNode>(labelProp)
                        ->getStringValue();
                if (!labelStr.empty()) {
                  smInfo.transitionIdsByName[labelStr] = t.id;
                  if (!stateName.empty()) {
                    smInfo.transitionIdsByStateAndName[{stateName, labelStr}] =
                        t.id;
                  }
                }
              }
            }
          }
        }
        state.stateMachineInfos.push_back(std::move(smInfo));

        processStateStreams(smCtx, smCtx.smDecl, scope, tree, state,
                            domainName);

        // Save init info before moving smCtx
        generatedIRCode.stateMachinesByDomain[domainName].push_back(
            {smCtx.activeStateVarName, smCtx.isEnteredVarName,
             smCtx.initialStateId});

        // Inject the master switch block into the domain's process execution
        // stream
        auto smExpr = std::make_unique<StateMachineExprAST>(std::move(smCtx));
        generatedIRCode.domainGeneratedCode[domainName].push_back(
            std::move(smExpr));
      }
      auto domainExternalInputNode = domainDecl->getPropertyValue("inputs");
      if (domainExternalInputNode) {
        for (const auto &externalInput :
             domainExternalInputNode->getChildren()) {
          if (externalInput->getNodeType() == AST::Declaration ||
              externalInput->getNodeType() == AST::ArrayDeclaration) {
            auto decl =
                std::static_pointer_cast<DeclarationNode>(externalInput);
            ExternalArgs.push_back({decl->getName(), state.getLLVMType(decl),
                                    std::string(),
                                    getDefaultValue(decl, state)});
            // Remove external from globals list
            for (auto global = generatedIRCode.GlobalSignals.begin();
                 global != generatedIRCode.GlobalSignals.end(); global++) {
              if ((*global)->getName() == decl->getName()) {
                generatedIRCode.GlobalSignals.erase(global);
                break;
              }
            }
          }
        }
      }
      auto domainExternalOutputNode = domainDecl->getPropertyValue("outputs");
      if (domainExternalOutputNode) {
        for (const auto &externalOutput :
             domainExternalOutputNode->getChildren()) {
          if (externalOutput->getNodeType() == AST::Declaration ||
              externalOutput->getNodeType() == AST::ArrayDeclaration) {
            auto decl =
                std::static_pointer_cast<DeclarationNode>(externalOutput);
            ExternalArgs.push_back({decl->getName(), state.getLLVMType(decl),
                                    std::string(),
                                    getDefaultValue(decl, state)});
            // Remove external from globals list
            for (auto global = generatedIRCode.GlobalSignals.begin();
                 global != generatedIRCode.GlobalSignals.end(); global++) {
              if ((*global)->getName() == decl->getName()) {
                generatedIRCode.GlobalSignals.erase(global);
                break;
              }
            }
          }
        }
      }
    }

    // A trigger is a switch that turns off at the end of its domain's
    // processing, so it is on for exactly the tick that set it. Switches
    // written into triggers just pass their value.
    std::vector<std::string> domainTriggers;
    for (const auto &node : tree->getChildren()) {
      if (node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() != "trigger") {
        continue;
      }
      auto domainProp = decl->getPropertyValue("domain");
      if (!domainProp || (domainProp->getNodeType() == AST::Entity &&
                          ASTQuery::getNodeName(domainProp) == domainName)) {
        domainTriggers.push_back(decl->getName());
      }
    }
    for (const auto &bufferDomain : state.bufferDomains) {
      if (bufferDomain.second == domainName) {
        auto bufferDecl =
            ASTQuery::findDeclarationByName(bufferDomain.first, scope, tree);
        domainTriggers.push_back(
            BufferSpec::fromDeclaration(bufferDecl, tree).readyName());
      }
    }
    for (const auto &triggerName : domainTriggers) {
      it->second.push_back(std::make_unique<BinaryExprAST>(
          '=', std::make_unique<VariableExprAST>(triggerName),
          std::make_unique<BoolExprAST>(false)));
    }

    bool domainNeedsState = state.doesDomainNeedState(domainDecl);

    std::vector<PrototypeArg> internalArgs;
    if (domainNeedsState) {
      internalArgs.push_back(PrototypeArg{
          "__state", llvm::PointerType::get(*state.TheContext, 0)});
    }

    auto processProto = std::make_unique<PrototypeAST>(
        std::string(domainName + "_process"),
        /* InArgs */ std::vector<PrototypeArg>{},
        /* OutArgs */ std::vector<PrototypeArg>{},
        /* InternalPersistentArgs */ internalArgs,
        /* PropertyArgs */ std::vector<PrototypeArg>{}, ExternalArgs,
        std::vector<PrototypeArg>{});
    auto processFunc = std::make_unique<FunctionAST>(std::move(processProto),
                                                     std::move(it->second));
    processFunc->callType = CallableType::DomainFunction;
    processFunc->hasState = domainNeedsState;
    processFunc->funcInstance = domainDecl;
    if (domainDecl) {
      processFunc->line = domainDecl->getLine();
      processFunc->column = domainDecl->getColumn();
      processFunc->relativeFilename = domainDecl->getRelativeFilename();
      processFunc->absoluteFilename = domainDecl->getAbsoluteFilename();
    }
    processFunc->displayName = domainName + "::process";

    if (auto *processLlvm = processFunc->codegen(state)) {
      generateInvoker(processLlvm, state);
    }

    auto initProto = std::make_unique<PrototypeAST>(
        std::string(domainName + "_init"),
        /* InArgs */ std::vector<PrototypeArg>{},
        /* OutArgs */ std::vector<PrototypeArg>{},
        /* InternalPersistentArgs */ internalArgs,
        /* PropertyArgs */ std::vector<PrototypeArg>{}, ExternalArgs,
        std::vector<PrototypeArg>{});
    std::vector<std::unique_ptr<ExprAST>> resetBody;
    for (auto &extArg : ExternalArgs) {
      if (std::holds_alternative<int32_t>(extArg.defaultValue)) {
        auto varInit = std::make_unique<BinaryExprAST>(
            '=', std::make_unique<VariableExprAST>(extArg.name),
            std::make_unique<IntExprAST>(
                std::get<int32_t>(extArg.defaultValue)));
        resetBody.push_back(std::move(varInit));
      } else if (std::holds_alternative<int64_t>(extArg.defaultValue)) {
        auto varInit = std::make_unique<BinaryExprAST>(
            '=', std::make_unique<VariableExprAST>(extArg.name),
            std::make_unique<IntExprAST>(
                std::get<int64_t>(extArg.defaultValue), 64));
        resetBody.push_back(std::move(varInit));
      } else if (std::holds_alternative<double>(extArg.defaultValue)) {
        auto varInit = std::make_unique<BinaryExprAST>(
            '=', std::make_unique<VariableExprAST>(extArg.name),
            std::make_unique<RealExprAST>(
                std::get<double>(extArg.defaultValue)));
        resetBody.push_back(std::move(varInit));
      } else if (std::holds_alternative<bool>(extArg.defaultValue)) {
        auto varInit = std::make_unique<BinaryExprAST>(
            '=', std::make_unique<VariableExprAST>(extArg.name),
            std::make_unique<BoolExprAST>(std::get<bool>(extArg.defaultValue)));
        resetBody.push_back(std::move(varInit));
      } else if (std::holds_alternative<std::string>(extArg.defaultValue)) {
        auto varInit = std::make_unique<BinaryExprAST>(
            '=', std::make_unique<VariableExprAST>(extArg.name),
            std::make_unique<StringExprAST>(
                std::get<std::string>(extArg.defaultValue)));
        resetBody.push_back(std::move(varInit));
      } else {
        LOG_ERROR() << "ERROR unexpected type for default." << std::endl;
      }
    }

    // TODO Use information on generated code to initialize instead of going
    // through tree again.
    for (const auto &smCtx :
         generatedIRCode.stateMachinesByDomain[domainName]) {
      auto varInit = std::make_unique<BinaryExprAST>(
          '=', std::make_unique<VariableExprAST>(smCtx.activeStateVarName),
          std::make_unique<IntExprAST>(smCtx.initialStateId));
      resetBody.push_back(std::move(varInit));
      if (!smCtx.isEnteredVarName.empty()) {
        auto enteredInit = std::make_unique<BinaryExprAST>(
            '=', std::make_unique<VariableExprAST>(smCtx.isEnteredVarName),
            std::make_unique<BoolExprAST>(false));
        resetBody.push_back(std::move(enteredInit));
      }
    }

    // Buffers restart empty. Stored samples are left in place: a frame view
    // only covers samples pushed since the reset.
    for (const auto &bufferDomain : state.bufferDomains) {
      if (bufferDomain.second != domainName) {
        continue;
      }
      auto bufferDecl =
          ASTQuery::findDeclarationByName(bufferDomain.first, scope, tree);
      auto spec = BufferSpec::fromDeclaration(bufferDecl, tree);
      for (const auto &name :
           {spec.indexName(), spec.countName(), spec.sinceName()}) {
        resetBody.push_back(std::make_unique<BinaryExprAST>(
            '=', std::make_unique<VariableExprAST>(name),
            std::make_unique<IntExprAST>(0, 32)));
      }
      resetBody.push_back(std::make_unique<BinaryExprAST>(
          '=', std::make_unique<VariableExprAST>(spec.readyName()),
          std::make_unique<BoolExprAST>(false)));
      if (spec.hasTriggerBlock()) {
        resetBody.push_back(std::make_unique<BinaryExprAST>(
            '=', std::make_unique<VariableExprAST>(spec.flushName()),
            std::make_unique<BoolExprAST>(false)));
      }
    }

    if (const auto *dynFields = state.findDynamicDomainFields(domainDecl)) {
      for (const auto &field : *dynFields) {
        if (field.name.rfind("__rate_phase_", 0) == 0 && field.type) {
          std::unique_ptr<ExprAST> zeroExpr;
          if (field.type->isIntegerTy(32)) {
            zeroExpr = std::make_unique<IntExprAST>(0, 32);
          } else if (field.type->isIntegerTy(64)) {
            zeroExpr = std::make_unique<IntExprAST>(0, 64);
          } else if (field.type->isDoubleTy()) {
            zeroExpr = std::make_unique<RealExprAST>(0.0);
          }
          if (zeroExpr) {
            resetBody.push_back(std::make_unique<BinaryExprAST>(
                '=', std::make_unique<VariableExprAST>(field.name),
                std::move(zeroExpr)));
          }
        }
      }
    }

    auto addDeclToResetBody =
        [&](const std::shared_ptr<DeclarationNode> &decl) {
          auto defaultValueNode = decl->getPropertyValue("default");
          if (!defaultValueNode)
            return;
          if (decl->getEntityType() == "switch" ||
              decl->getEntityType() == "trigger") {
            if (defaultValueNode->getNodeType() == AST::Switch) {
              auto varInit = std::make_unique<BinaryExprAST>(
                  '=', std::make_unique<VariableExprAST>(decl->getName()),
                  std::make_unique<BoolExprAST>(
                      std::static_pointer_cast<ValueNode>(defaultValueNode)
                          ->getSwitchValue()));
              resetBody.push_back(std::move(varInit));
            } else {
              LOG_ERROR() << "Unsupported type for switch default: "
                          << defaultValueNode->toText() << std::endl;
            }
          } else if (decl->getEntityType() == "signal" ||
                     decl->getEntityType() == "string") {
            llvm::Type *llvmType = state.getLLVMType(decl);
            std::unique_ptr<ExprAST> valExpr;
            if (defaultValueNode->getNodeType() == AST::Real) {
              double val = std::static_pointer_cast<ValueNode>(defaultValueNode)
                               ->getRealValue();
              if (llvmType->isFloatingPointTy())
                valExpr = std::make_unique<RealExprAST>(val);
              else if (llvmType->isIntegerTy() && !llvmType->isIntegerTy(1))
                valExpr =
                    std::make_unique<IntExprAST>(static_cast<int64_t>(val), 64);
              else
                valExpr = std::make_unique<BoolExprAST>(val != 0.0);
            } else if (defaultValueNode->getNodeType() == AST::Int) {
              int64_t val =
                  std::static_pointer_cast<ValueNode>(defaultValueNode)
                      ->getIntValue();
              if (llvmType->isFloatingPointTy())
                valExpr =
                    std::make_unique<RealExprAST>(static_cast<double>(val));
              else if (llvmType->isIntegerTy() && !llvmType->isIntegerTy(1))
                valExpr =
                    std::make_unique<IntExprAST>(static_cast<int64_t>(val), 64);
              else
                valExpr = std::make_unique<BoolExprAST>(val != 0);
            } else if (defaultValueNode->getNodeType() == AST::Switch) {
              bool val = std::static_pointer_cast<ValueNode>(defaultValueNode)
                             ->getSwitchValue();
              if (llvmType->isFloatingPointTy())
                valExpr = std::make_unique<RealExprAST>(val ? 1.0 : 0.0);
              else if (llvmType->isIntegerTy() && !llvmType->isIntegerTy(1))
                valExpr = std::make_unique<IntExprAST>(val ? 1 : 0);
              else
                valExpr = std::make_unique<BoolExprAST>(val);
            } else if (defaultValueNode->getNodeType() == AST::String) {
              std::string val =
                  std::static_pointer_cast<ValueNode>(defaultValueNode)
                      ->getStringValue();
              valExpr = std::make_unique<StringExprAST>(val);
            }
            if (valExpr) {
              auto varInit = std::make_unique<BinaryExprAST>(
                  '=', std::make_unique<VariableExprAST>(decl->getName()),
                  std::move(valExpr));
              resetBody.push_back(std::move(varInit));
            }
          }
        };

    if (domainDecl) {
      auto smContexts =
          StateMachine::collectStateMachines(domainDecl, scope, tree);
      for (const auto &smCtx : smContexts) {
        for (const auto &fs : smCtx.flattenedStates) {
          if (fs.stateDecl) {
            auto entities = ASTQuery::getStateMachineEntities(fs.stateDecl);
            for (const auto &entityNode : entities) {
              if (entityNode->getNodeType() == AST::Declaration) {
                addDeclToResetBody(
                    std::static_pointer_cast<DeclarationNode>(entityNode));
              }
            }
          }
        }
      }
    }

    for (const auto &node : tree->getChildren()) {
      if (node->getNodeType() == AST::Declaration ||
          node->getNodeType() == AST::ArrayDeclaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(node);
        auto domainNode = decl->getPropertyValue("domain");
        auto defaultValueNode = decl->getPropertyValue("default");
        if (defaultValueNode && domainNode &&
            domainNode->getNodeType() == AST::Entity) {
          if (ASTQuery::getNodeName(domainNode) == domainName) {
            if (node->getNodeType() == AST::Declaration) {
              addDeclToResetBody(decl);
            } else if (node->getNodeType() == AST::ArrayDeclaration) {
              std::vector<LangError> errors;
              int size = ASTQuery::getEntityDeclaredSize(
                  std::static_pointer_cast<DeclarationNode>(node), scope, tree,
                  &errors);
              std::vector<ASTNode> defaultValues;
              if (defaultValueNode->getNodeType() != AST::List) {
                defaultValues.resize(size);
                std::fill(defaultValues.begin(), defaultValues.end(),
                          defaultValueNode);
              } else {
                defaultValues = defaultValueNode->getChildren();

                if (defaultValues.size() != size) {
                  LOG_ERROR() << "ERROR default values size does not match "
                                 "declared size"
                              << std::endl;
                  continue;
                }
              }
              size_t index = 0;
              llvm::Type *llvmType = state.getLLVMType(decl);
              for (const auto &defaultValue : defaultValues) {
                std::unique_ptr<ExprAST> valExpr;
                if (defaultValue->getNodeType() == AST::Real) {
                  double val = std::static_pointer_cast<ValueNode>(defaultValue)
                                   ->getRealValue();
                  if (llvmType->isFloatingPointTy())
                    valExpr = std::make_unique<RealExprAST>(val);
                  else if (llvmType->isIntegerTy() && !llvmType->isIntegerTy(1))
                    valExpr =
                        std::make_unique<IntExprAST>(static_cast<int64_t>(val), 64);
                  else
                    valExpr = std::make_unique<BoolExprAST>(val != 0.0);
                } else if (defaultValue->getNodeType() == AST::Int) {
                  int64_t val =
                      std::static_pointer_cast<ValueNode>(defaultValue)
                          ->getIntValue();
                  if (llvmType->isFloatingPointTy())
                    valExpr =
                        std::make_unique<RealExprAST>(static_cast<double>(val));
                  else if (llvmType->isIntegerTy() && !llvmType->isIntegerTy(1))
                    valExpr =
                        std::make_unique<IntExprAST>(static_cast<int64_t>(val), 64);
                  else
                    valExpr = std::make_unique<BoolExprAST>(val != 0);
                } else if (defaultValue->getNodeType() == AST::Switch) {
                  bool val = std::static_pointer_cast<ValueNode>(defaultValue)
                                 ->getSwitchValue();
                  if (llvmType->isFloatingPointTy())
                    valExpr = std::make_unique<RealExprAST>(val ? 1.0 : 0.0);
                  else if (llvmType->isIntegerTy() && !llvmType->isIntegerTy(1))
                    valExpr = std::make_unique<IntExprAST>(val ? 1 : 0);
                  else
                    valExpr = std::make_unique<BoolExprAST>(val);
                }
                if (valExpr) {
                  auto varInit = std::make_unique<BinaryExprAST>(
                      '=',
                      std::make_unique<VariableExprAST>(
                          decl->getName(),
                          std::vector<std::variant<size_t, std::string>>{
                              index}),
                      std::move(valExpr));
                  resetBody.push_back(std::move(varInit));
                }
                index++;
              }
            }
          }
        }
      }
    }

    auto initFunc = std::make_unique<FunctionAST>(std::move(initProto),
                                                  std::move(resetBody));
    initFunc->callType = CallableType::DomainFunction;
    initFunc->hasState = domainNeedsState;
    initFunc->funcInstance = domainDecl;
    if (domainDecl) {
      initFunc->line = domainDecl->getLine();
      initFunc->column = domainDecl->getColumn();
      initFunc->relativeFilename = domainDecl->getRelativeFilename();
      initFunc->absoluteFilename = domainDecl->getAbsoluteFilename();
    }
    initFunc->displayName = domainName + "::init";
    // Add domain member variables (globals)
    // for (const auto &global : generatedIRCode.GlobalSignals) {
    //   initFunc->internalVariables.push_back(global);
    // }
    if (auto *initLlvm = initFunc->codegen(state)) {
      generateInvoker(initLlvm, state);
    }

    state.domainArgs[domainName] = generatedIRCode.domainArgs;
  }
}

std::unique_ptr<ExprAST> StrideGenerator::createExpr(ASTNode node) {
  if (!node) {
    return nullptr;
  }
  std::unique_ptr<ExprAST> V = nullptr;
  if (node->getNodeType() == AST::Entity) {
    V = std::make_unique<VariableExprAST>(
        std::static_pointer_cast<EntityNode>(node)->getName());
    setTypeCastMetadata(node, V.get());
  } else if (node->getNodeType() == AST::Array) {
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);

    std::vector<std::variant<size_t, std::string>> indeces;
    for (const auto &idx : arrayNode->index()->getChildren()) {
      if (idx->getNodeType() == AST::Entity) {
        indeces.push_back(std::static_pointer_cast<EntityNode>(idx)->getName());
      } else if (idx->getNodeType() == AST::Int) {
        indeces.push_back(
            (size_t)std::static_pointer_cast<ValueNode>(idx)->getIntValue());
      } else {
        LOG_ERROR() << "Unsupported node type for array index.";
      }
    }
    V = std::make_unique<VariableExprAST>(arrayNode->getName(), indeces);
    setTypeCastMetadata(node, V.get());
  } else if (node->getNodeType() == AST::Real) {
    // TODO separate float types
    V = std::make_unique<RealExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getRealValue());
    setTypeCastMetadata(node, V.get());
  } else if (node->getNodeType() == AST::Int) {
    // TODO separate int types
    V = std::make_unique<IntExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getIntValue());
    setTypeCastMetadata(node, V.get());
  } else if (node->getNodeType() == AST::Switch) {
    // TODO separate int types
    V = std::make_unique<BoolExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getSwitchValue());
    setTypeCastMetadata(node, V.get());
  } else if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->isUnary()) {
      if (expr->getExpressionType() == ExpressionNode::UnaryMinus) {
        // Lower -x to x * -1. BinaryExprAST promotes the integer constant to
        // double when x is a real, so this works for both int and real signals.
        auto operand = createExpr(expr->getValue());
        if (!operand) {
          return nullptr;
        }
        V = std::make_unique<BinaryExprAST>(
            '*', std::move(operand),
            std::make_unique<IntExprAST>(static_cast<int32_t>(-1)));
        setTypeCastMetadata(node, V.get());
      } else {
        LOG_ERROR() << " Unsupported unary expression type: "
                    << expr->getExpressionTypeString() << std::endl;
        return nullptr;
      }
    } else {
      auto l = createExpr(expr->getLeft());
      auto r = createExpr(expr->getRight());
      char op;
      if (expr->getExpressionType() == ExpressionNode::Multiply) {
        op = '*';
      } else if (expr->getExpressionType() == ExpressionNode::Divide) {
        op = '/';
      } else if (expr->getExpressionType() == ExpressionNode::Add) {
        op = '+';
      } else if (expr->getExpressionType() == ExpressionNode::Subtract) {
        op = '-';
      } else {
        assert(0 == 1);
        return nullptr;
      }

      V = std::make_unique<BinaryExprAST>(op, std::move(l), std::move(r));
      setTypeCastMetadata(node, V.get());
    }
  } else if (node->getNodeType() == AST::List) {
    V = std::make_unique<ListExprAST>(node->getChildren());
  } else if (node->getNodeType() == AST::String) {
    auto val = std::static_pointer_cast<ValueNode>(node)->getStringValue();
    V = std::make_unique<StringExprAST>(val);
    setTypeCastMetadata(node, V.get());
  } else if (node->getNodeType() == AST::MemberAccess) {
    auto pp = std::static_pointer_cast<MemberAccessNode>(node);
    V = std::make_unique<MemberAccessAST>(pp->getEntity(),
                                          pp->getPropertyName());
    setTypeCastMetadata(node, V.get());
  } else {
    LOG_ERROR() << " Node type not supported " << std::endl;
    return nullptr;
  }

  if (V && node) {
    V->setLocationFrom(node);
  }
  return V;
}

std::vector<std::unique_ptr<ExprAST>> StrideGenerator::generateStreamsForNodes(
    const std::vector<ASTNode> &nodes, ScopeStack scope, StrideCompiler &state,
    const std::string &targetDomain) {
  std::vector<std::unique_ptr<ExprAST>> result;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);
      auto code = createStreamCode(stream, state.m_tree, scope, state);
      for (auto &domainCode : code) {
        if (targetDomain.empty() || domainCode.first == targetDomain ||
            domainCode.first == "RootDomain") {
          for (const auto &f : domainCode.second.functions) {
            f->codegen(state);
          }
          while (domainCode.second.expr.size() > 0) {
            result.emplace_back(std::move(domainCode.second.expr.front()));
            domainCode.second.expr.erase(domainCode.second.expr.begin());
          }
        }
      }
    }
  }
  return result;
}

void StrideGenerator::processStateStreams(
    StateMachine &sm, std::shared_ptr<DeclarationNode> stateNode,
    ScopeStack scope, ASTNode tree, StrideCompiler &state,
    const std::string &domainName) {
  // 1. Get state's local entities and push to scope
  std::vector<ASTNode> stateEntities = ASTQuery::getStateMachineEntities(stateNode);
  scope.push_back({stateNode, stateEntities});

  // 2. Evaluate streams and track them in the state machine structs
  for (auto &fs : sm.flattenedStates) {
    if (fs.stateDecl == stateNode) {
      auto streamBlocks =
          ASTQuery::getContainedStreamBlocks(stateNode, scope, tree);
      for (const auto &streamBlock : streamBlocks) {
        std::vector<ASTNode> streamNodes(streamBlock.streams.begin(),
                                         streamBlock.streams.end());
        if (streamBlock.executionStage == "updateGuard") {
          auto code = generateStreamsForNodes(streamNodes, scope, state,
                                              domainName);
          fs.updateGuardCode.insert(fs.updateGuardCode.end(),
                                    std::make_move_iterator(code.begin()),
                                    std::make_move_iterator(code.end()));
        } else if (streamBlock.executionStage == "onEntry") {
          auto code =
              generateStreamsForNodes(streamNodes, scope, state, domainName);
          fs.onEntryCode.insert(fs.onEntryCode.end(),
                                std::make_move_iterator(code.begin()),
                                std::make_move_iterator(code.end()));
        } else if (streamBlock.executionStage == "onProcess") {
          auto code =
              generateStreamsForNodes(streamNodes, scope, state, domainName);
          fs.onProcessCode.insert(fs.onProcessCode.end(),
                                  std::make_move_iterator(code.begin()),
                                  std::make_move_iterator(code.end()));
        } else if (streamBlock.executionStage == "onExit") {
          auto code =
              generateStreamsForNodes(streamNodes, scope, state, domainName);
          fs.onExitCode.insert(fs.onExitCode.end(),
                               std::make_move_iterator(code.begin()),
                               std::make_move_iterator(code.end()));
        }
      }

      // Codegenerate transition streams
      for (auto &t : fs.transitions) {
        if (t.transitionDecl) {
          auto guardProp = t.transitionDecl->getPropertyValue("guard");
          if (guardProp && guardProp->getNodeType() != AST::None) {
            if (guardProp->getNodeType() == AST::Entity ||
                guardProp->getNodeType() == AST::Array) {
              t.guardCode.push_back(createExpr(guardProp));
            } else if (guardProp->getNodeType() != AST::List) {
              std::cerr << "Unsupported guard: " << guardProp->toText()
                        << std::endl;
            }
          }
          auto tBlocks =
              ASTQuery::getContainedStreamBlocks(t.transitionDecl, scope, tree);
          for (const auto &tb : tBlocks) {
            if (tb.executionStage == "onTransition" ||
                tb.propertyName == "onTransition") {
              std::vector<ASTNode> streamNodes(tb.streams.begin(),
                                               tb.streams.end());
              auto code = generateStreamsForNodes(streamNodes, scope, state,
                                                  domainName);
              t.onTransitionCode.insert(t.onTransitionCode.end(),
                                        std::make_move_iterator(code.begin()),
                                        std::make_move_iterator(code.end()));
            }
          }
        }
      }
      break;
    }
  }

  // 3. Recurse into child states
  auto statesProp = stateNode->getPropertyValue("states");
  if (statesProp && statesProp->getNodeType() == AST::List) {
    for (const auto &child : statesProp->getChildren()) {
      if (child->getNodeType() == AST::Entity) {
        auto childName = std::static_pointer_cast<EntityNode>(child)->getName();
        auto childDecl =
            ASTQuery::findDeclarationByName(childName, scope, tree);
        if (childDecl) {
          processStateStreams(sm, childDecl, scope, tree, state, domainName);
        }
      }
    }
  }
}

StrideGenerator::GeneratedIRCode
StrideGenerator::generateCodeForTree(ASTNode tree, ScopeStack &scope,
                                     StrideCompiler &state) {

  state.m_tree = tree;
  state.registerDataTypes(tree);
  state.m_intanceTree = CodeAnalysis::getStateStructInformation({}, tree);

  // PRE-PASS: Find all domain variables, signals, switches, and state machine
  // variables, and inject them into dynamic fields for each domain.
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (ASTQuery::isDomainDefinition(
              ASTQuery::findTypeDeclaration(decl, scope, tree), scope, tree)) {
        std::string domainName = decl->getName();

        auto registerVarDeclAsDynamicField =
            [&](const std::shared_ptr<DeclarationNode> &varDecl) {
              llvm::Type *varType = state.getLLVMType(varDecl);
              if (!varType || varType->isVoidTy() || !varType->isSized()) {
                varType = llvm::Type::getDoubleTy(*state.TheContext);
              }
              if (varDecl->getNodeType() == AST::ArrayDeclaration) {
                int size =
                    ASTQuery::getEntityDeclaredSize(varDecl, scope, tree);
                if (size > 0) {
                  varType = llvm::ArrayType::get(varType, size);
                }
              }
              auto defaultNode = varDecl->getPropertyValue("default");
              if (!defaultNode) {
                defaultNode = varDecl->getPropertyValue("value");
              }
              llvm::Constant *defaultVal =
                  state.makeDefaultConstant(defaultNode, varType);
              if (!defaultVal) {
                defaultVal = llvm::Constant::getNullValue(varType);
              }

              // Check if already added to avoid duplicates
              bool alreadyAdded = false;
              auto dynIt = state.dynamicDomainFields.find(node);
              if (dynIt != state.dynamicDomainFields.end()) {
                for (const auto &field : dynIt->second) {
                  if (field.name == varDecl->getName()) {
                    alreadyAdded = true;
                    break;
                  }
                }
              }
              if (!alreadyAdded) {
                state.addDynamicDomainField(node, varDecl->getName(), varType,
                                            defaultVal);
              }
            };

        // 1. Add state machine internal variables
        auto smContexts = StateMachine::collectStateMachines(decl, scope, tree);
        for (const auto &smCtx : smContexts) {
          state.addDynamicDomainField(
              node, smCtx.activeStateVarName,
              llvm::Type::getInt32Ty(*state.TheContext),
              llvm::ConstantInt::get(llvm::Type::getInt32Ty(*state.TheContext),
                                     smCtx.initialStateId));
          state.addDynamicDomainField(
              node, smCtx.transitionRequestVarName,
              llvm::Type::getInt32Ty(*state.TheContext),
              llvm::ConstantInt::get(llvm::Type::getInt32Ty(*state.TheContext),
                                     0));
          if (!smCtx.transitionRequestArgVarName.empty()) {
            state.addDynamicDomainField(
                node, smCtx.transitionRequestArgVarName,
                llvm::Type::getInt32Ty(*state.TheContext),
                llvm::ConstantInt::get(
                    llvm::Type::getInt32Ty(*state.TheContext), 0));
          }
          state.addDynamicDomainField(
              node, smCtx.isEnteredVarName,
              llvm::Type::getInt1Ty(*state.TheContext),
              llvm::ConstantInt::get(llvm::Type::getInt1Ty(*state.TheContext),
                                     0));
          for (const auto &fs : smCtx.flattenedStates) {
            if (fs.resumeLastState && !fs.historyStateVarName.empty()) {
              state.addDynamicDomainField(
                  node, fs.historyStateVarName,
                  llvm::Type::getInt32Ty(*state.TheContext),
                  llvm::ConstantInt::get(
                      llvm::Type::getInt32Ty(*state.TheContext), 0));
            }
            if (fs.stateDecl) {
              auto entities = ASTQuery::getStateMachineEntities(fs.stateDecl);
              for (const auto &entityNode : entities) {
                if (entityNode->getNodeType() == AST::Declaration ||
                    entityNode->getNodeType() == AST::ArrayDeclaration) {
                  auto varDecl =
                      std::static_pointer_cast<DeclarationNode>(entityNode);
                  std::string objType = varDecl->getEntityType();
                  if (objType == "switch" || objType == "signal" ||
                      objType == "trigger" || objType == "constant" ||
                      objType == "string") {
                    registerVarDeclAsDynamicField(varDecl);
                  }
                }
              }
            }
          }
        }

        // 2. Add domain-level signals, switches, triggers, and guards
        for (const auto &childNode : tree->getChildren()) {
          if (childNode->getNodeType() == AST::Declaration ||
              childNode->getNodeType() == AST::ArrayDeclaration) {
            auto varDecl = std::static_pointer_cast<DeclarationNode>(childNode);
            std::string objType = varDecl->getEntityType();
            if (objType == "switch" || objType == "signal" ||
                objType == "trigger" || objType == "constant" ||
                objType == "string") {
              auto domainProp = varDecl->getPropertyValue("domain");
              bool belongsToDomain = false;
              if (domainProp && domainProp->getNodeType() == AST::Entity) {
                belongsToDomain =
                    (ASTQuery::getNodeName(domainProp) == domainName);
              } else if (!domainProp && objType != "constant") {
                belongsToDomain = true;
              }

              if (belongsToDomain) {
                registerVarDeclAsDynamicField(varDecl);
              }
            }
          }
        }
      }
    }
  }

  std::set<ASTNode> stagedStreams;
  std::set<ASTNode> uniformStagedStreams;
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration ||
        node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "_domainDefinition" ||
          decl->getEntityType() == "gameDefinition") {
        auto stagesProp = decl->getCompilerProperty("streamStages");
        if (stagesProp && stagesProp->getNodeType() == AST::List) {
          double domRate = CodeAnalysis::getDomainDefaultRate(decl);
          std::string domName = decl->getName();
          for (const auto &stageNode : stagesProp->getChildren()) {
            double stageRate = -1.0;
            auto rateProp = stageNode->getCompilerProperty("rate");
            if (rateProp && (rateProp->getNodeType() == AST::Real ||
                             rateProp->getNodeType() == AST::Int)) {
              stageRate = static_cast<ValueNode *>(rateProp.get())->toReal();
            }
            if (stageRate <= 0.0)
              stageRate = domRate;

            bool stageHasUniformStreams = false;
            for (const auto &sNode : stageNode->getChildren()) {
              stagedStreams.insert(sNode);
              auto s = std::static_pointer_cast<StreamNode>(sNode);
              StreamNodeIterator it(s);
              double firstRate = -1.0;
              bool internalTransitions = false;
              while (auto n = it.next()) {
                double r = CodeAnalysis::getNodeRate(n, scope, tree);
                if (r > 0.0) {
                  if (firstRate <= 0.0) {
                    firstRate = r;
                  } else if (std::abs(r - firstRate) > 1e-9) {
                    internalTransitions = true;
                    break;
                  }
                }
              }
              if (!internalTransitions) {
                uniformStagedStreams.insert(sNode);
                stageHasUniformStreams = true;
              }
            }

            if (stageHasUniformStreams && domRate > 0.0 && stageRate > 0.0 &&
                std::abs(stageRate - domRate) > 1e-9) {
              std::string fieldName = "__rate_phase_" + domName + "_" +
                                      std::to_string(state.m_idCounter++);
              auto spec = RateScheduleSpec::build(domRate, stageRate, fieldName);
              if (spec.needsPersistentPhaseState()) {
                llvm::Type *phaseTy =
                    spec.isExactInteger
                        ? llvm::Type::getInt32Ty(*state.TheContext)
                        : (spec.useInt64Phase
                               ? llvm::Type::getInt64Ty(*state.TheContext)
                               : llvm::Type::getDoubleTy(*state.TheContext));
                llvm::Constant *zeroConst =
                    llvm::Constant::getNullValue(phaseTy);
                state.addDynamicDomainField(decl, fieldName, phaseTy,
                                            zeroConst);
                stageNode->setCompilerProperty(
                    "__phase_field",
                    std::make_shared<ValueNode>(fieldName, __FILE__, __LINE__));
              }
            }
          }
        }
      }
    }
  }

  // Pre-scan top-level streams to register dynamic domain fields for multi-rate phase state
  // before state.buildStateStructTypes is invoked.
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream && uniformStagedStreams.count(node) == 0) {
      auto stream = std::static_pointer_cast<StreamNode>(node);
      StreamNodeIterator streamIt(stream);
      ASTNode current;
      ASTNode next = streamIt.next();
      ASTNode prev = nullptr;

      struct PreScanSegState {
        double activeRate{-1.0};
        double domainRate{-1.0};
        ASTNode domainDecl{nullptr};
        bool domainResolved{false};
        bool hasActiveExprs{false};
      };
      std::map<std::string, PreScanSegState> segStates;

      auto resolveDomain = [&](const std::string &domName) -> PreScanSegState & {
        auto &seg = segStates[domName];
        if (!seg.domainResolved) {
          seg.domainResolved = true;
          auto domDecl = CodeAnalysis::findDomainDeclaration(domName, tree);
          if (!domDecl) {
            domDecl = ASTQuery::findDeclarationByName(domName, scope, tree);
          }
          seg.domainDecl = domDecl;
          if (domDecl) {
            seg.domainRate = CodeAnalysis::getDomainDefaultRate(domDecl);
          }
        }
        return seg;
      };

      auto phaseFieldsList = std::make_shared<ListNode>(__FILE__, __LINE__);

      auto flushPreScanSegment = [&](const std::string &domName) {
        auto &seg = resolveDomain(domName);
        if (!seg.hasActiveExprs) return;
        seg.hasActiveExprs = false;
        if (seg.domainRate <= 0.0 || seg.activeRate <= 0.0 ||
            std::abs(seg.activeRate - seg.domainRate) < 1e-9) {
          return;
        }
        std::string fieldName =
            "__rate_phase_" + domName + "_" + std::to_string(state.m_idCounter++);
        auto spec = RateScheduleSpec::build(seg.domainRate, seg.activeRate, fieldName);
        if (spec.needsPersistentPhaseState() && seg.domainDecl) {
          llvm::Type *phaseTy =
              spec.isExactInteger
                  ? llvm::Type::getInt32Ty(*state.TheContext)
                  : (spec.useInt64Phase
                         ? llvm::Type::getInt64Ty(*state.TheContext)
                         : llvm::Type::getDoubleTy(*state.TheContext));
          llvm::Constant *zeroConst = llvm::Constant::getNullValue(phaseTy);
          state.addDynamicDomainField(seg.domainDecl, fieldName, phaseTy, zeroConst);
          phaseFieldsList->addChild(std::make_shared<ValueNode>(fieldName, __FILE__, __LINE__));
        }
      };

      while (next) {
        prev = current;
        current = next;
        next = streamIt.next();
        auto domainName = CodeAnalysis::getNodeDomainName(current, scope, tree);
        if (domainName.empty()) {
          StreamNodeIterator lookaheadIt(stream);
          while (auto n = lookaheadIt.next()) {
            if (n == current) break;
          }
          while (auto n = lookaheadIt.next()) {
            auto d = CodeAnalysis::getNodeDomainName(n, scope, tree);
            if (!d.empty()) {
              domainName = d;
              break;
            }
          }
          if (domainName.empty()) domainName = "RootDomain";
        }
        auto &segState = resolveDomain(domainName);
        double currentRate = CodeAnalysis::getNodeRate(current, scope, tree);
        if (currentRate > 0.0 && segState.activeRate <= 0.0) {
          segState.activeRate = currentRate;
        } else if (currentRate > 0.0 && segState.activeRate > 0.0 &&
                   std::abs(currentRate - segState.activeRate) > 1e-9) {
          flushPreScanSegment(domainName);
          segState.activeRate = currentRate;
        }
        segState.hasActiveExprs = true;
      }
      for (const auto &pair : segStates) {
        flushPreScanSegment(pair.first);
      }
      stream->setCompilerProperty("__rate_phase_fields", phaseFieldsList);
    }
  }

  state.buildStateStructTypes(state.m_intanceTree, scope, tree);
  StrideGenerator::GeneratedIRCode generatedIRCode;

  // Emit streams grouped into topological rate stages where available
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration ||
        node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "_domainDefinition" ||
          decl->getEntityType() == "gameDefinition") {
        auto stagesProp = decl->getCompilerProperty("streamStages");
        if (stagesProp && stagesProp->getNodeType() == AST::List) {
          double domRate = CodeAnalysis::getDomainDefaultRate(decl);
          std::string domName = decl->getName();
          for (const auto &stageNode : stagesProp->getChildren()) {
            double stageRate = -1.0;
            auto rateProp = stageNode->getCompilerProperty("rate");
            if (rateProp && (rateProp->getNodeType() == AST::Real ||
                             rateProp->getNodeType() == AST::Int)) {
              stageRate = static_cast<ValueNode *>(rateProp.get())->toReal();
            }
            if (stageRate <= 0.0)
              stageRate = domRate;

            std::vector<std::unique_ptr<ExprAST>> stageExprs;
            for (const auto &sNode : stageNode->getChildren()) {
              auto stream = std::static_pointer_cast<StreamNode>(sNode);
              bool internalTransitions = false;
              StreamNodeIterator it(stream);
              double firstRate = -1.0;
              while (auto n = it.next()) {
                double r = CodeAnalysis::getNodeRate(n, scope, tree);
                if (r > 0.0) {
                  if (firstRate <= 0.0) {
                    firstRate = r;
                  } else if (std::abs(r - firstRate) > 1e-9) {
                    internalTransitions = true;
                    break;
                  }
                }
              }

              if (internalTransitions) {
                auto code = createStreamCode(stream, tree, scope, state, true);
                for (auto &domainCode : code) {
                  for (const auto &f : domainCode.second.functions) {
                    f->codegen(state);
                  }
                  while (!domainCode.second.expr.empty()) {
                    generatedIRCode.domainGeneratedCode[domainCode.first].push_back(
                        std::move(domainCode.second.expr.front()));
                    domainCode.second.expr.erase(domainCode.second.expr.begin());
                  }
                }
              } else {
                auto code = createStreamCode(stream, tree, scope, state, false);
                for (auto &domainCode : code) {
                  for (const auto &f : domainCode.second.functions) {
                    f->codegen(state);
                  }
                  while (!domainCode.second.expr.empty()) {
                    stageExprs.push_back(
                        std::move(domainCode.second.expr.front()));
                    domainCode.second.expr.erase(
                        domainCode.second.expr.begin());
                  }
                }
              }
            }

            if (!stageExprs.empty()) {
              if (domRate > 0.0 && stageRate > 0.0 &&
                  std::abs(stageRate - domRate) > 1e-9) {
                std::string phaseFieldName;
                auto phaseNode = stageNode->getCompilerProperty("__phase_field");
                if (phaseNode && phaseNode->getNodeType() == AST::String) {
                  phaseFieldName = static_cast<ValueNode *>(phaseNode.get())
                                       ->getStringValue();
                }
                auto spec =
                    RateScheduleSpec::build(domRate, stageRate, phaseFieldName);
                if (stageRate < domRate) {
                  auto downsampleExpr = std::make_unique<DownsampleExprAST>(
                      std::move(spec), std::move(stageExprs));
                  generatedIRCode.domainGeneratedCode[domName].push_back(
                      std::move(downsampleExpr));
                } else {
                  auto upsampleExpr = std::make_unique<UpsampleExprAST>(
                      std::move(spec), std::move(stageExprs));
                  generatedIRCode.domainGeneratedCode[domName].push_back(
                      std::move(upsampleExpr));
                }
              } else {
                for (auto &e : stageExprs) {
                  generatedIRCode.domainGeneratedCode[domName].push_back(
                      std::move(e));
                }
              }
            }
          }
        }
      }
    }
  }

  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream && stagedStreams.count(node) > 0) {
      continue;
    }
    if (node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);

      auto code = createStreamCode(stream, tree, scope, state);
      for (auto &domainCode : code) {
        for (const auto &f : domainCode.second.functions) {
          f->codegen(state);
        }

        while (domainCode.second.expr.size() > 0) {
          generatedIRCode.domainGeneratedCode[domainCode.first].emplace_back(
              std::move(domainCode.second.expr.front()));
          domainCode.second.expr.erase(domainCode.second.expr.begin());
        }
      }

      //      auto *RetVal =
      //          llvm::ConstantInt::get(state.Builder->getInt32Ty(), 0,
      //          true);
      //      state.Builder->CreateRet(RetVal);
    } else if (node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "_domainDefinition" ||
          decl->getEntityType() == "gameDefinition") {
        generatedIRCode.domainGeneratedCode[decl->getName()];
      }
      if (decl->getEntityType() == "signal" ||
          decl->getEntityType() == "string" ||
          ASTQuery::isConstant(decl, scope, tree)) {
        auto *type = state.getLLVMType(decl);

        bool inDynamicDomain = false;
        for (const auto &pair : state.dynamicDomainFields) {
          for (const auto &field : pair.second) {
            if (field.name == decl->getName()) {
              inDynamicDomain = true;
              break;
            }
          }
          if (inDynamicDomain)
            break;
        }
        if (!inDynamicDomain) {
          generatedIRCode.GlobalSignals.push_back(decl);
        }

        // TODO remove domain args?
        if (type->isDoubleTy()) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::DOUBLE});
        } else if (type->isFloatTy()) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::FLOAT32});
        } else if (type->isIntegerTy(1)) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::BOOL});
        } else if (type->isIntegerTy(8)) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::INT8});
        } else if (type->isIntegerTy(16)) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::INT16});
        } else if (type->isIntegerTy(32)) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::INT32});
        } else if (type->isIntegerTy(64)) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::INT64});
        } else if (type->isStructTy() || decl->getEntityType() == "string") {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::STRING});
        } else if (type->isArrayTy()) {
          // Arrays of signals or strings
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::CUSTOM});
          //        } else if (type->isIntegerTy(64)) {
          //          domainArgs.push_back({decl->getName(), DataType::INT64});
        } else {
          assert(0 == 1);
        }
      } else if (decl->getEntityType() == "buffer") {
        // Storage plus the private state BufferPushExprAST and
        // BufferViewExprAST work on. See BufferSpec for the names.
        auto spec = BufferSpec::fromDeclaration(decl, tree);
        generatedIRCode.GlobalSignals.push_back(decl);

        const std::string file = decl->getFilename();
        auto makeState = [&](const std::string &name,
                             const std::string &entityType,
                             ASTNode defaultValue) {
          auto stateDecl = std::make_shared<DeclarationNode>(
              name, entityType, nullptr, file.c_str(), decl->getLine(),
              decl->getColumn());
          if (entityType == "signal") {
            stateDecl->setPropertyValue(
                "type", std::make_shared<EntityNode>("Int", file.c_str(),
                                                     decl->getLine(),
                                                     decl->getColumn()));
          }
          stateDecl->setPropertyValue(
              entityType == "constant" ? "value" : "default", defaultValue);
          generatedIRCode.GlobalSignals.push_back(stateDecl);
        };
        auto intValue = [&](int64_t v) {
          return std::make_shared<ValueNode>(v, file.c_str(), decl->getLine(),
                                             decl->getColumn());
        };
        auto offValue = [&]() {
          return std::make_shared<ValueNode>(false, file.c_str(),
                                             decl->getLine(), decl->getColumn());
        };
        makeState(spec.indexName(), "signal", intValue(0));
        makeState(spec.countName(), "signal", intValue(0));
        makeState(spec.sinceName(), "signal", intValue(0));
        makeState(spec.readyName(), "trigger", offValue());
        if (spec.hasTriggerBlock()) {
          makeState(spec.flushName(), "switch", offValue());
        }
        makeState(spec.frameSizeName(), "constant", intValue(spec.frameSize));
      } else if (decl->getEntityType() == "switch" ||
                 decl->getEntityType() == "trigger") {
        bool inDynamicDomain = false;
        for (const auto &pair : state.dynamicDomainFields) {
          for (const auto &field : pair.second) {
            if (field.name == decl->getName()) {
              inDynamicDomain = true;
              break;
            }
          }
          if (inDynamicDomain)
            break;
        }
        if (!inDynamicDomain) {
          generatedIRCode.GlobalSignals.push_back(decl);
        }
        generatedIRCode.domainArgs.push_back({decl->getName(), DataType::BOOL});
      } else {
        continue;
      }
    }
  }
  return generatedIRCode;
}

bool StrideGenerator::processPreviousFunction(
    std::shared_ptr<FunctionNode> prevFuncCall,
    std::shared_ptr<DeclarationNode> decl,
    std::unique_ptr<ExprAST> currentExpr,
    StrideGenerator::GeneratedCode &generated, std::string domainName,
    const ScopeStack &scope, ASTNode tree, StrideCompiler &state) {
  auto funcDecl = ASTQuery::findDeclarationByName(
      ASTQuery::getNodeName(prevFuncCall), scope, tree);
  if (!funcDecl) {
    LOG_ERROR() << "ERROR: no function declaration for "
                << ASTQuery::getNodeName(prevFuncCall) << std::endl;
    return false;
  }
  auto retType = state.getLLVMType(funcDecl);
  if (auto callexpr = dynamic_cast<CallExprAST *>(
          generated[domainName].expr.back().get())) {

    if (!callexpr->OutArgs.empty()) {
      return true;
    }

    std::vector<llvm::Type *> argTypes;
    for (auto it = callexpr->InArgsDataType.begin();
         it != callexpr->InArgsDataType.end(); it++) {
      argTypes.push_back(*it);
    }

    // TODO do we also test that the function is not declared in Stride
    // code? Or is this check enough?
    auto externFunc =
        state.getExternalFunction(prevFuncCall->getName(), retType, argTypes);
    if (externFunc && retType && !retType->isVoidTy()) {
      generated[domainName].expr.back() = std::make_unique<BinaryExprAST>(
          '=', std::move(currentExpr),
          std::move(generated[domainName].expr.back()));
    } else {
      // Stride Functions pass the output as arguments, so no need to
      // assign
    }
  } else if (dynamic_cast<LLVMCommandAST *>(
                 generated[domainName].expr.back().get())) {
    auto outputsNode = funcDecl->getPropertyValue("outputs");
    llvm::Type *outType = nullptr;
    if (outputsNode && outputsNode->getChildren().size() > 0) {
      auto outputTypeNode = outputsNode->getChildren()[0];
      if (outputTypeNode && outputTypeNode->getNodeType() == AST::Entity) {
        auto outputType = std::static_pointer_cast<EntityNode>(outputTypeNode);
        if (state.typesMap.find(outputType->getName()) !=
            state.typesMap.end()) {
          outType = state.typesMap[outputType->getName()];
        }
      }
    }

    if (outType && retType != outType) {
      if (retType->isIntegerTy() && outType->isDoubleTy()) {
        // FIXME: convert if needed
      }
    }

    if (decl->getEntityType() == "trigger") {
      std::vector<std::unique_ptr<ExprAST>> Expressions;
      auto triggerResetsNode = decl->getCompilerProperty("triggerResets");
      if (triggerResetsNode) {
        for (const auto &node : triggerResetsNode->getChildren()) {
          if (node->getNodeType() == AST::Declaration ||
              node->getNodeType() == AST::ArrayDeclaration) {
            auto resetNodeDecl =
                std::static_pointer_cast<DeclarationNode>(node);
            // TODO is this code duplicated?
            if (resetNodeDecl->getEntityType() == "signal") {
              auto typeNode = resetNodeDecl->getPropertyValue("type");
              if (typeNode && typeNode->getNodeType() == AST::Entity) {
                auto typeName =
                    std::static_pointer_cast<EntityNode>(typeNode)->getName();
                auto defaultNode = resetNodeDecl->getPropertyValue("default");
                if (!defaultNode) {
                  continue;
                }
                auto typeInfo = state.getDataTypeInfo(typeName);
                if (typeInfo.isFloat() &&
                    (defaultNode->getNodeType() == AST::Real ||
                     defaultNode->getNodeType() == AST::Int)) {
                  double defaultValue =
                      std::static_pointer_cast<ValueNode>(defaultNode)
                          ->toReal();
                  auto varReal = std::make_unique<BinaryExprAST>(
                      '=',
                      std::make_unique<VariableExprAST>(
                          resetNodeDecl->getName()),
                      std::make_unique<RealExprAST>(defaultValue));
                  Expressions.push_back(std::move(varReal));
                } else if (typeInfo.isInteger() &&
                           defaultNode->getNodeType() == AST::Int) {
                  int64_t defaultValue =
                      std::static_pointer_cast<ValueNode>(defaultNode)
                          ->getIntValue();
                  auto varInit = std::make_unique<BinaryExprAST>(
                      '=',
                      std::make_unique<VariableExprAST>(
                          resetNodeDecl->getName()),
                      std::make_unique<IntExprAST>(defaultValue));
                  Expressions.push_back(std::move(varInit));
                } else {
                  assert(0 == 1);
                }
              } else {
                assert(0 == 1);
              }

            } else if (resetNodeDecl->getEntityType() == "switch" ||
                       resetNodeDecl->getEntityType() == "trigger") {

            } else {
              assert(0 == 1);
            }
          }
        }
      }
      generated[domainName].expr.back() = std::make_unique<ResetExprAST>(
          "ResetName", std::move(generated[domainName].expr.back()),
          std::move(Expressions));
    } else {
      generated[domainName].expr.back() = std::make_unique<BinaryExprAST>(
          '=', std::move(currentExpr),
          std::move(generated[domainName].expr.back()));
    }
  } else {
    assert(0 == 1);
    return false;
  }
  return true;
}

void StrideGenerator::collectInputArgs(
    FunctionArgs &args, CodeAnalysis::TypeTree *typeTree, StrideCompiler &state,
    std::vector<std::unique_ptr<ExprAST>> &exprs, ScopeStack &scope,
    ASTNode tree, std::shared_ptr<DeclarationNode> funcDecl,
    std::shared_ptr<FunctionNode> func, ASTNode prev) {
  // TODO collect function port arguments
  ASTNode input = nullptr;
  if (typeTree && typeTree->instance) {
    input = typeTree->instance->getCompilerProperty("mainInput");
  }
  if (!input && func) {
    input = func->getCompilerProperty("mainInput");
  }
  if (!input && prev) {
    input = prev;
  }
  if (input) {
    if (auto prevFunc = std::dynamic_pointer_cast<FunctionNode>(input)) {
      if (auto intermediateNode =
              prevFunc->getCompilerProperty("intermediateOutput")) {
        // Verify that the intermediate symbol was produced by the previously
        // connected function
        std::string intermediateSymbol =
            ASTQuery::getNodeName(intermediateNode);
        args.MainIn.args.emplace_back(
            std::make_unique<VariableExprAST>(intermediateSymbol));
        auto prevDecls = ASTQuery::findAllDeclarations(
            ASTQuery::getNodeName(prevFunc), scope, tree);
        auto prevDecl = CodeAnalysis::matchDefinitionToTypes(
            prevDecls, prevFunc, scope, tree);
        if (prevDecl) {
          auto protoIt =
              state.FunctionProtos.find(ASTQuery::getNodeName(prevDecl));
          if (protoIt != state.FunctionProtos.end() &&
              !protoIt->second->getOutArgs().empty()) {
            args.MainIn.argTypes.push_back(
                protoIt->second->getOutArgs()[0].llvmType);
          } else {
            args.MainIn.argTypes.push_back(
                llvm::Type::getInt32Ty(*state.TheContext));
          }
        } else {
          args.MainIn.argTypes.push_back(
              llvm::Type::getInt32Ty(*state.TheContext));
        }
      } else {
        if (!exprs.empty()) {
          args.MainIn.args.emplace_back(std::move(exprs.back()));
          exprs.pop_back();
          // FIXME do automatic type casting int ->float
          args.MainIn.argTypes.push_back(
              llvm::Type::getDoubleTy(*state.TheContext));
        } else {
          LOG_ERROR() << "ERROR: exprs is empty when collecting function input argument"
                      << std::endl;
        }
      }
    } else if (auto prevList = std::dynamic_pointer_cast<ListNode>(input)) {
      if (exprs.empty()) {
        LOG_ERROR() << "ERROR: exprs is empty when collecting list input arguments"
                    << std::endl;
        return;
      }
      auto *v = dynamic_cast<ListExprAST *>(exprs.back().get());
      if (!v) {
        LOG_ERROR() << "ERROR: exprs.back() is not a ListExprAST"
                    << std::endl;
        return;
      }
      auto listNodes = prevList->getChildren();
      auto nodeIt = listNodes.begin();
      for (auto elemExpr = v->elements().begin();
           elemExpr != v->elements().end(); elemExpr++) {
        args.MainIn.args.emplace_back(std::move(*elemExpr));
        ASTNode currentChildNode =
            (nodeIt != listNodes.end()) ? *nodeIt : nullptr;
        if (nodeIt != listNodes.end()) {
          nodeIt++;
        }
        std::string typeStr;
        if (currentChildNode) {
          setTypeCastMetadata(currentChildNode, args.MainIn.args.back().get());
          auto typecastNode =
              currentChildNode->getCompilerProperty("typecast");
          if (typecastNode && typecastNode->getNodeType() == AST::String) {
            typeStr = std::static_pointer_cast<ValueNode>(typecastNode)
                          ->getStringValue();
          } else {
            typeStr = CodeAnalysis::resolveNodeOutDataType(currentChildNode,
                                                           scope, tree);
          }
        }
        if (!typeStr.empty()) {
          auto typeIt = state.typesMap.find(typeStr);
          if (typeIt != state.typesMap.end() && typeIt->second &&
              !typeIt->second->isVoidTy()) {
            args.MainIn.argTypes.push_back(typeIt->second);
          } else {
            args.MainIn.argTypes.push_back(state.typesMap[DataTypes::Real]);
          }
        } else if (currentChildNode) {
          auto elemDecl = ASTQuery::findDeclarationByName(
              ASTQuery::getNodeName(currentChildNode), scope, tree);

          if (elemDecl) {
            args.MainIn.argTypes.push_back(
                state.getLLVMTypeForCodegenEntity(elemDecl, funcDecl, func));
          } else if (currentChildNode->getNodeType() == AST::Int) {
            // TODO get types from framework
            args.MainIn.argTypes.push_back(
                llvm::Type::getInt32Ty(*state.TheContext));
          } else if (currentChildNode->getNodeType() == AST::Real) {
            args.MainIn.argTypes.push_back(
                llvm::Type::getDoubleTy(*state.TheContext));
          } else if (currentChildNode->getNodeType() == AST::String) {
            args.MainIn.argTypes.push_back(
                state.typesMap[DataTypes::String]);
          } else if (currentChildNode->getNodeType() == AST::MemberAccess) {
            auto pp =
                std::static_pointer_cast<MemberAccessNode>(currentChildNode);
            if (pp->getPropertyName() == "size") {
              args.MainIn.argTypes.push_back(
                  llvm::Type::getInt32Ty(*state.TheContext));
            } else if (pp->getPropertyName() == "rate") {
              args.MainIn.argTypes.push_back(
                  llvm::Type::getDoubleTy(*state.TheContext));
            } else {
              LOG_ERROR()
                  << ":Port property not supported: " << pp->getPropertyName()
                  << std::endl;
              args.MainIn.argTypes.push_back(
                  llvm::Type::getDoubleTy(*state.TheContext));
            }
          } else {
            // Fallback. We shouldn't get here when things are fully
            // implemented
            args.MainIn.argTypes.push_back(
                state.getLLVMTypeForCodegenEntity(elemDecl, funcDecl, func));
            LOG_ERROR() << "Unsupported type for: "
                        << currentChildNode->toText() << std::endl;
          }
        } else {
          args.MainIn.argTypes.push_back(
              llvm::Type::getDoubleTy(*state.TheContext));
        }
      }
      exprs.pop_back();

    } else {
      if (exprs.size() > 0) {
        args.MainIn.args.emplace_back(std::move(exprs.back()));
        exprs.pop_back();
        LOG_DEBUG() << "Calling getOutputDataTypes for input: "
                    << input->toText() << std::endl;
        auto prevTypes = CodeAnalysis::getOutputDataTypes(input, scope, tree);
        LOG_DEBUG() << "getOutputDataTypes returned " << prevTypes.size()
                    << " types" << std::endl;
        for (const auto &prevType : prevTypes) {
          auto typeName = ASTQuery::getNodeName(prevType);
          LOG_DEBUG() << "prevType name is: " << typeName << std::endl;
          if (!typeName.empty() &&
              state.typesMap.find(typeName) != state.typesMap.end() &&
              !state.typesMap[typeName]->isVoidTy()) {
            args.MainIn.argTypes.push_back(state.typesMap[typeName]);
            LOG_DEBUG() << "added type to argTypes, size is now "
                        << args.MainIn.argTypes.size() << std::endl;
          } else if (prevType && prevType->getNodeType() == AST::MemberAccess) {
            args.MainIn.argTypes.push_back(state.typesMap[DataTypes::Real]);
          } else {
            LOG_DEBUG() << "typeName " << typeName << " not found in typesMap!"
                        << std::endl;
            args.MainIn.argTypes.push_back(state.typesMap[DataTypes::Real]);
          }
        }
      } else {
        LOG_ERROR() << "ERROR: No code generated for domain." << std::endl;
      }
    }
  }
}

void StrideGenerator::collectPropertyArgs(
    // TODO avoid code duplication with collectInputArgs
    FunctionArgs &args, CodeAnalysis::TypeTree *typeTree, StrideCompiler &state,
    std::vector<std::unique_ptr<ExprAST>> &exprs, ScopeStack &scope,
    ASTNode tree, std::shared_ptr<DeclarationNode> funcDecl,
    std::shared_ptr<FunctionNode> func) {
  // TODO collect function port arguments
  auto funcInstance =
      (typeTree && typeTree->instance &&
       typeTree->instance->getNodeType() == AST::Function)
          ? std::static_pointer_cast<FunctionNode>(typeTree->instance)
          : func;
  if (funcInstance) {
    auto props = funcInstance->getProperties();
    for (const auto &propNode : props) {
      auto value = propNode->getValue();
      if (value && value->getNodeType() == AST::Entity) {
        auto valueDecl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(value), scope, tree);
        if (valueDecl && valueDecl->getEntityType() == "buffer") {
          // Zero-copy: the reaction reads the current frame in place
          auto spec = BufferSpec::fromDeclaration(valueDecl, tree);
          args.Properties.args.emplace_back(
              std::make_unique<BufferViewExprAST>(spec));
          args.Properties.argTypes.push_back(
              state.getLLVMType(valueDecl)->isArrayTy()
                  ? state.getLLVMType(valueDecl)->getArrayElementType()
                  : state.getLLVMType(valueDecl));
          continue;
        }
      }
      args.Properties.args.emplace_back(createExpr(value));
      // FIXME set correct type
      args.Properties.argTypes.push_back(
          llvm::Type::getDoubleTy(*state.TheContext));
    }
  }
}

StrideGenerator::GeneratedCode
StrideGenerator::createStreamCode(std::shared_ptr<StreamNode> stream,
                                  ASTNode tree, ScopeStack &scope,
                                  StrideCompiler &state,
                                  bool wrapMultiRate) {
  StrideGenerator::GeneratedCode generated;
  ASTNode prev = nullptr;
  bool isTriggeredReactionStream = false;
  auto leftNode = stream->getLeft();
  std::shared_ptr<DeclarationNode> leftDecl = nullptr;
  bool isLeftSwitch = false;
  if (leftNode) {
    if (leftNode->getNodeType() == AST::Switch) {
      isLeftSwitch = true;
    } else if (leftNode->getNodeType() == AST::MemberAccess) {
      auto pp = std::static_pointer_cast<MemberAccessNode>(leftNode);
      auto entityDecl =
          ASTQuery::findDeclarationByName(pp->getEntity(), scope, tree);
      if (entityDecl && entityDecl->getEntityType() == "buffer" &&
          pp->getPropertyName() == "ready") {
        isLeftSwitch = true;
      }
    } else {
      leftDecl = ASTQuery::findDeclarationByName(
          ASTQuery::getNodeName(leftNode), scope, tree);
      if (leftDecl && (leftDecl->getEntityType() == "switch" ||
                       leftDecl->getEntityType() == "trigger")) {
        isLeftSwitch = true;
      }
    }
  }
  if (isLeftSwitch) {
    StreamNodeIterator checkIt(stream);
    while (auto n = checkIt.next()) {
      if (n->getNodeType() == AST::Function) {
        auto funcNode = std::static_pointer_cast<FunctionNode>(n);
        auto funcDecls = ASTQuery::findAllDeclarations(
            ASTQuery::getNodeName(funcNode), scope, tree);
        decltype(funcDecls) callableDecls;
        for (const auto &d : funcDecls) {
          if (!d)
            continue;
          const auto &t = d->getEntityType();
          if (t == "module" || t == "reaction" || t == "action" ||
              t == "loop" || t == "platformModule" || t == "function") {
            callableDecls.push_back(d);
          }
        }
        if (!callableDecls.empty()) {
          funcDecls = std::move(callableDecls);
        }
        auto firstFuncDecl = CodeAnalysis::matchDefinitionToTypes(
            funcDecls, funcNode, scope, tree);
        if (firstFuncDecl) {
          std::string entType = firstFuncDecl->getEntityType();
          if (entType == "reaction" || entType == "action") {
            isTriggeredReactionStream = true;
          }
        }
        break;
      }
    }
  }

  StreamNodeIterator streamIt(stream);
  ASTNode current;
  ASTNode next = streamIt.next();

  // Track active rate segment per domain for on-the-fly multi-rate wrapping.
  // Only top-level domain streams (scope.size() <= 1) wrap segments in
  // DownsampleExprAST / UpsampleExprAST; streams inside modules/loops/reactions
  // inherit their caller's execution rate.
  const bool isTopLevelDomainStream = (scope.size() <= 1);
  size_t nextPhaseFieldIdx = 0;
  struct DomainRateSegmentState {
    double activeRate{-1.0};
    double domainRate{-1.0};
    ASTNode domainDecl{nullptr};
    bool domainResolved{false};
    std::vector<std::unique_ptr<ExprAST>> committedExprs;
  };
  std::map<std::string, DomainRateSegmentState> rateSegments;

  auto resolveDomainRateInfo = [&](const std::string &domName) -> DomainRateSegmentState & {
    auto &seg = rateSegments[domName];
    if (!seg.domainResolved) {
      seg.domainResolved = true;
      auto domDecl = CodeAnalysis::findDomainDeclaration(domName, tree);
      if (!domDecl) {
        domDecl = ASTQuery::findDeclarationByName(domName, scope, tree);
      }
      seg.domainDecl = domDecl;
      if (domDecl) {
        seg.domainRate = CodeAnalysis::getDomainDefaultRate(domDecl);
      }
    }
    return seg;
  };

  auto flushRateSegment = [&](const std::string &domName,
                              const std::string &boundarySignal = "") {
    auto &seg = resolveDomainRateInfo(domName);
    auto &activeExprs = generated[domName].expr;
    if (activeExprs.empty()) {
      return;
    }
    if (!isTopLevelDomainStream || seg.domainRate <= 0.0 ||
        seg.activeRate <= 0.0 ||
        std::abs(seg.activeRate - seg.domainRate) < 1e-9 ||
        !wrapMultiRate) {
      for (auto &e : activeExprs) {
        seg.committedExprs.push_back(std::move(e));
      }
      activeExprs.clear();
      return;
    }

    auto spec = RateScheduleSpec::build(seg.domainRate, seg.activeRate,
                                        "", boundarySignal);
    if (spec.needsPersistentPhaseState()) {
      ASTNode phaseProp =
          stream ? stream->getCompilerProperty("__rate_phase_fields") : nullptr;
      if (phaseProp && phaseProp->getNodeType() == AST::List) {
        if (nextPhaseFieldIdx < phaseProp->getChildren().size()) {
          auto valNode = std::static_pointer_cast<ValueNode>(
              phaseProp->getChildren().at(nextPhaseFieldIdx++));
          if (valNode) {
            spec.phaseStateFieldName = valNode->getStringValue();
          }
        }
      }
      if (spec.phaseStateFieldName.empty()) {
        spec.phaseStateFieldName =
            "__rate_phase_" + domName + "_" + std::to_string(state.m_idCounter++);
      }
    }

    std::vector<std::unique_ptr<ExprAST>> bodyExprs = std::move(activeExprs);
    activeExprs.clear();
    if (spec.isDownsample()) {
      seg.committedExprs.push_back(
          std::make_unique<DownsampleExprAST>(std::move(spec), std::move(bodyExprs)));
    } else if (spec.isUpsample()) {
      seg.committedExprs.push_back(
          std::make_unique<UpsampleExprAST>(std::move(spec), std::move(bodyExprs)));
    } else {
      for (auto &e : bodyExprs) {
        seg.committedExprs.push_back(std::move(e));
      }
    }
  };

  while (next) {
    prev = current;
    current = next;
    next = streamIt.next();
    auto domainName = CodeAnalysis::getNodeDomainName(current, scope, tree);
    if (domainName.empty()) {
      // Look downstream along the stream to find the target domain
      StreamNodeIterator lookaheadIt(stream);
      while (auto n = lookaheadIt.next()) {
        if (n == current) {
          break;
        }
      }
      while (auto downstream = lookaheadIt.next()) {
        auto d = CodeAnalysis::getNodeDomainName(downstream, scope, tree);
        if (d.empty()) {
          auto conn =
              CodeAnalysis::resolveConnectionEntity(downstream, scope, tree, true);
          if (conn) {
            d = CodeAnalysis::getNodeDomainName(conn, scope, tree);
          }
        }
        if (!d.empty()) {
          domainName = d;
          break;
        }
      }
      if (domainName.empty()) {
        for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
          if (it->first) {
            auto d = CodeAnalysis::getNodeDomainName(it->first, scope, tree);
            if (!d.empty()) {
              domainName = d;
              break;
            }
          }
        }
      }
      if (domainName.empty()) {
        domainName = "RootDomain";
      }
    }
    auto &segState = resolveDomainRateInfo(domainName);
    double currentRate = CodeAnalysis::getNodeRate(current, scope, tree);
    if (currentRate > 0.0 && segState.activeRate <= 0.0) {
      segState.activeRate = currentRate;
    }
    if (current->getNodeType() == AST::Expression) {
      // -------------------------------------------------------------
      // Expression
      assert(prev == nullptr);
      generated[domainName].expr.push_back(createExpr(current));
      // FIXME look recursively inside elements
      for (const auto &elem : current->getChildren()) {
        auto decl = elem->getCompilerProperty("declaration");
        if (decl && decl->getChildren().size() > 0) {
          if (decl->getChildren()[0]->getCompilerProperty("reads")) {
            generated[domainName].readVariables.push_back(elem);
          }
        }
      }
    } else if (current->getNodeType() == AST::Entity ||
               current->getNodeType() == AST::Array) {
      // -------------------------------------------------------------
      // Entity / Array
      auto entityExpr = createExpr(current);
      auto decl = ASTQuery::findDeclarationByName(
          ASTQuery::getNodeName(current), scope, tree);
      if (current->getNodeType() == AST::Array) {
        auto indexNode = std::static_pointer_cast<ArrayNode>(current)->index();
        if (indexNode->getNodeType() == AST::List) {
          for (const auto &elem : indexNode->getChildren()) {
            if (elem->getNodeType() == AST::Entity) {
              auto indexDecl = ASTQuery::findDeclarationByName(
                  ASTQuery::getNodeName(elem), scope, tree);
              if (indexDecl) {
                generated[domainName].readVariables.push_back(elem);
              } else {
                assert(0 == 1);
              }
            } else if (elem->getNodeType() == AST::Int) {
              // Nothing needed in this case
            } else {
              assert(0 == 1); // FIXME complete support for processing index
                              // contents
            }
          }
        } else {
          assert(0 == 1);
        }
      }
      if (!decl) {
        LOG_ERROR() << "No declaration for: " << AST::toText(current) << " in "
                    << AST::toText(stream) << " in " << stream->getFilename()
                    << ":" << stream->getLine() << std::endl;
        return generated;
      }
      // TODO accumulate read and write variables for switch, expressions,
      // lists and function arguments if on root namespace
      if (current == stream->getLeft()) {
        generated[domainName].readVariables.push_back(current);
        if (isTriggeredReactionStream) {
          continue;
        }
      } else {
        generated[domainName].writeVariables.push_back(current);
      }
      if (prev && prev->getNodeType() == AST::Function) {
        // TODO do we need to process again here?
        // Connect to previous function
        if (!processPreviousFunction(
                std::static_pointer_cast<FunctionNode>(prev), decl,
                std::move(entityExpr), generated, domainName, scope, tree,
                state)) {
          continue;
        }
        if (next) {
          bool rateTransition =
              (currentRate > 0.0 && segState.activeRate > 0.0 &&
               std::abs(currentRate - segState.activeRate) > 1e-9);
          auto *backCall =
              generated[domainName].expr.empty()
                  ? nullptr
                  : dynamic_cast<CallExprAST *>(
                        generated[domainName].expr.back().get());
          bool needsMidStreamReload =
              rateTransition || (backCall && !backCall->OutArgs.empty());
          if (rateTransition) {
            flushRateSegment(domainName, ASTQuery::getNodeName(current));
            segState.activeRate = currentRate;
          }
          if (needsMidStreamReload) {
            generated[domainName].expr.push_back(createExpr(current));
          }
        } else if (currentRate > 0.0 && segState.activeRate <= 0.0) {
          segState.activeRate = currentRate;
        }
      } else if (generated[domainName].expr.size() > 0) {
        bool rateTransition =
            (currentRate > 0.0 && segState.activeRate > 0.0 &&
             std::abs(currentRate - segState.activeRate) > 1e-9);
        if (rateTransition) {
          // If the previous expression is already a completed assignment to an
          // intermediate variable (e.g. FastA >> FastB >> SlowC), flush the
          // upstream rate segment first, switch to currentRate, and read the
          // upstream variable as the RHS of the new assignment.
          auto *prevBin = dynamic_cast<BinaryExprAST *>(
              generated[domainName].expr.back().get());
          if (prevBin && prev &&
              (prev->getNodeType() == AST::Entity ||
               prev->getNodeType() == AST::Array)) {
            flushRateSegment(domainName, ASTQuery::getNodeName(prev));
            segState.activeRate = currentRate;
            generated[domainName].expr.push_back(createExpr(prev));
          } else {
            segState.activeRate = currentRate;
          }
        }
        // Connect to previous stream node
        if (decl && decl->getEntityType() == "buffer") {
          state.bufferDomains[decl->getName()] = domainName;
          generated[domainName].expr.back() = std::make_unique<BufferPushExprAST>(
              BufferSpec::fromDeclaration(decl, tree),
              std::move(generated[domainName].expr.back()));
        } else {
          generated[domainName].expr.back() = std::make_unique<BinaryExprAST>(
              '=', std::move(entityExpr),
              std::move(generated[domainName].expr.back()));
        }
        if (stream) {
          generated[domainName].expr.back()->setLocationFrom(stream);
        }
        if (next && next->getNodeType() == AST::Function) {
          double nextRate = CodeAnalysis::getNodeRate(next, scope, tree);
          if (nextRate > 0.0 && segState.activeRate > 0.0 &&
              std::abs(nextRate - segState.activeRate) > 1e-9) {
            flushRateSegment(domainName, ASTQuery::getNodeName(current));
            segState.activeRate = nextRate;
            generated[domainName].expr.push_back(createExpr(current));
          }
        }
      } else {
        generated[domainName].expr.push_back(std::move(entityExpr));
      }
    } else if (current->getNodeType() == AST::Function) {
      // -------------------------------------------------------------
      // Function
      if (currentRate > 0.0 && segState.activeRate > 0.0 &&
          std::abs(currentRate - segState.activeRate) > 1e-9) {
        if (prev && prev->getNodeType() == AST::Function &&
            !generated[domainName].expr.empty()) {
          flushRateSegment(domainName, ASTQuery::getNodeName(prev));
        }
        segState.activeRate = currentRate;
      }
      auto func = std::static_pointer_cast<FunctionNode>(current);
      auto decls =
          ASTQuery::findAllDeclarations(ASTQuery::getNodeName(func), scope, tree);
      {
        // A function call can only resolve to a callable declaration. Other
        // declarations may share the name (e.g. a named generator transition
        // `transition Place_X[Index in 0:8]` calling `action Place_X`).
        auto isCallableDecl = [](const std::shared_ptr<DeclarationNode> &d) {
          if (!d)
            return false;
          const auto &t = d->getEntityType();
          if (t == "module" || t == "reaction" || t == "action" ||
              t == "loop" || t == "platformModule" || t == "function") {
            return true;
          }
          return false;
        };
        decltype(decls) callableDecls;
        for (const auto &d : decls) {
          if (isCallableDecl(d)) {
            callableDecls.push_back(d);
          }
        }
        if (!callableDecls.empty()) {
          decls = std::move(callableDecls);
        }
      }
      auto funcDecl =
          CodeAnalysis::matchDefinitionToTypes(decls, func, scope, tree);
      if (!funcDecl) {
        LOG_ERROR() << "ERROR can't find/match declaration for "
                    << func->getName() << std::endl;
        continue;
      }

      state.pushName(funcDecl->getName());
      // Collect arguments for function call ------------------------------

      auto *domainTypeTree = state.m_intanceTree.getDomainRootTree(domainName);
      auto *typeTree = domainTypeTree->find(func);
      if (!typeTree) {
        LOG_DEBUG() << "typeTree is null for function " << func->getName()
                    << " in domain " << domainName << std::endl;
      } else {
        LOG_DEBUG() << "typeTree is NOT null for function " << func->getName()
                    << std::endl;
      }
      FunctionArgs args;

      if (!(isTriggeredReactionStream && prev == stream->getLeft())) {
        collectInputArgs(args, typeTree, state, generated[domainName].expr,
                         scope, tree, funcDecl, func, prev);
      }

      // ASTExpr for properties are processed in collectPropertyArgs
      collectPropertyArgs(args, typeTree, state, generated[domainName].expr,
                          scope, tree, funcDecl, func);

      if (funcDecl->getEntityType() == "platformModule" && func) {
        if (auto fmtProp = func->getPropertyValue("format")) {
          auto fmtExpr = createExpr(fmtProp);
          if (fmtExpr) {
            args.MainIn.args.insert(args.MainIn.args.begin(),
                                    std::move(fmtExpr));
            args.MainIn.argTypes.insert(args.MainIn.argTypes.begin(),
                                        state.typesMap[DataTypes::String]);
          }
        }
      }

      // find if function is external
      std::optional<ExternalFunction> externFunc;

      // FIXME: ensure next node is processed correctly. This will not
      // work for many cases, e.g. if next is a function
      std::shared_ptr<DeclarationNode> nextDecl =
          ASTQuery::findDeclarationByName(ASTQuery::getNodeName(next), scope,
                                          tree);

      llvm::Type *retType = state.getLLVMType(nextDecl);
      if (retType->isVoidTy() &&
          funcDecl->getEntityType() == "platformModule") {
        auto outputList = funcDecl->getPropertyValue("outputs");
        if (outputList && outputList->getNodeType() == AST::List &&
            outputList->getChildren().size() > 0) {
          auto outputBlock = outputList->getChildren()[0];
          if (outputBlock->getNodeType() == AST::Entity) {
            auto outputType =
                std::static_pointer_cast<EntityNode>(outputBlock)->getName();
            if (state.typesMap.find(outputType) != state.typesMap.end()) {
              retType = state.typesMap[outputType];
            }
          }
        }
      }

      if (funcDecl->getEntityType() == "platformModule") {
        externFunc = state.getExternalFunction(func->getName(), retType,
                                               args.MainIn.argTypes);
      }

      std::unique_ptr<ExprAST> nextExpr = nullptr;
      if (next && next->getNodeType() != AST::Function) {
        nextExpr = createExpr(next);
      }
      if (nextExpr) {
        ASTNode outputNode = nullptr;
        if (typeTree && typeTree->instance) {
          outputNode = typeTree->instance->getCompilerProperty("mainOutput");
        }
        if (!outputNode && func) {
          outputNode = func->getCompilerProperty("mainOutput");
        }
        args.MainOut.args.emplace_back(std::move(nextExpr));
        if (outputNode) {
          auto entityName = ASTQuery::getNodeName(outputNode);
          ScopeStack scope;
          if (auto blocksNode = funcDecl->getPropertyValue("blocks")) {
            scope = {std::pair<ASTNode, std::vector<ASTNode>>(
                nullptr, blocksNode->getChildren())};
          }
          auto mainOutputDecl =
              ASTQuery::findDeclarationByName(entityName, scope, nullptr);

          args.MainOut.argTypes.push_back(state.getLLVMType(mainOutputDecl));
        }
      }

      // Create function call expr
      if (externFunc) {
        auto externCall = parseExternalCallTemplate(externFunc->name);
        if (externCall) {
          std::string fnName = externCall->symbol;
          // The external ABI path passes inputs in declaration order, so the
          // template may only list %%intokens:N%% in ascending order.
          bool orderOk = externCall->operandsParsed;
          for (size_t i = 0; orderOk && i < externCall->inTokenOrder.size();
               ++i) {
            orderOk = externCall->inTokenOrder[i] == i;
          }
          if (!orderOk) {
            LOG_ERROR() << "External call template '" << externFunc->name
                        << "' must list %%intokens:N%% operands in "
                           "declaration order; ignoring template operands."
                        << std::endl;
          }
          if (!state.TheModule->getFunction(fnName)) {
            // Aggregates (e.g. String {ptr, i32}) cannot be passed by
            // value across the JIT/host-C++ boundary: MSVC passes 16-byte
            // structs by hidden reference and returns them via sret, while
            // LLVM splits them into registers. Lower to pointers instead.
            llvm::FunctionType *logicalFT = externFunc->llvmFunctionType;
            llvm::LLVMContext &ctx = *state.TheContext;
            llvm::Type *ptrTy = llvm::PointerType::get(ctx, 0);
            std::vector<llvm::Type *> loweredParams;
            for (auto *p : logicalFT->params()) {
              loweredParams.push_back(p->isStructTy() ? ptrTy : p);
            }
            llvm::Type *loweredRet = logicalFT->getReturnType();
            bool stringOut = loweredRet->isStructTy();
            if (stringOut) {
              loweredParams.push_back(ptrTy);
              loweredRet = llvm::Type::getVoidTy(ctx);
            }
            auto *loweredFT =
                llvm::FunctionType::get(loweredRet, loweredParams, false);
            auto *newFunc = llvm::Function::Create(
                loweredFT, llvm::Function::ExternalLinkage,
                fnName, *state.TheModule);
            if (stringOut) {
              newFunc->addFnAttr("stride.struct_out");
            }
            if (externFunc->isConstrainedList) {
              newFunc->addFnAttr("stride.constrained_list");
              newFunc->addFnAttr("stride.variadic_prefix",
                                 std::to_string(externFunc->prefixTypes.size()));
              if (externFunc->isHeterogeneousVariadic) {
                newFunc->addFnAttr("stride.variadic_heterogeneous");
              }
            }
            generated[domainName].externalFunctions.push_back(newFunc);
          }
          auto newExternCall = std::make_unique<CallExprAST>(
              fnName, std::move(args.MainOut.args),
              std::move(args.MainIn.args),
              std::vector<std::unique_ptr<ExprAST>>{},
              std::vector<std::unique_ptr<ExprAST>>{},
              std::vector<std::unique_ptr<ExprAST>>{},
              std::vector<std::unique_ptr<ExprAST>>{},
              std::move(args.MainOut.argTypes), std::move(args.MainIn.argTypes),
              std::vector<llvm::Type *>{}, state.getName());
          newExternCall->callType = CallableType::External;
          generated[domainName].expr.push_back(std::move(newExternCall));
          LOG_DEBUG() << "Using external function:" << externFunc->name
                      << std::endl;
        } else {
          auto newCall = std::make_unique<LLVMCommandAST>(
              externFunc->name, std::move(args.MainOut.args),
              std::move(args.MainIn.args),
              std::vector<std::unique_ptr<ExprAST>>{},
              std::vector<std::unique_ptr<ExprAST>>{});
          // Leading data types of the inputs. A variadic entry ends them.
          if (auto inputs = funcDecl->getPropertyValue("inputs")) {
            for (const auto &input : inputs->getChildren()) {
              if (input->getNodeType() != AST::Entity) {
                break;
              }
              newCall->InArgDataTypes.push_back(ASTQuery::getNodeName(input));
            }
          }

          generated[domainName].expr.push_back(std::move(newCall));
        }
      } else { // is not external function
        // Reuse the callable declaration matched above. Re-looking it up by
        // name could return a same-named non-callable (e.g. a transition).
        if (funcDecl) {
          // --------------------- Function declaration -----------------------
          // Can we unify this parameter definition wiht argument preparation
          // above?
          // Name of the generated function, mangled for polymorphic callables.
          std::string calleeName = func->getName();
          std::unique_ptr<FunctionAST> newFuncDecl = createFunctionDeclaration(
              funcDecl, func, tree, &scope, state, &calleeName);
          if (newFuncDecl) {
            const auto &protoOuts = newFuncDecl->getProto().getOutArgs();
            while (args.MainOut.args.size() < protoOuts.size()) {
              const auto &arg = protoOuts[args.MainOut.args.size()];
              // TODO: Ensure this generated intermediate symbol is managed when
              // checking for symbol clashes across scopes.
              std::string intermediateName =
                  "__intermediate_" + state.getName() + "_" + arg.name;
              if (func) {
                func->setCompilerProperty(
                    "intermediateOutput",
                    std::make_shared<EntityNode>(intermediateName,
                                                 func->getFilename().c_str(),
                                                 func->getLine()));
              }
              args.MainOut.args.push_back(
                  std::make_unique<VariableExprAST>(intermediateName));
              args.MainOut.argTypes.push_back(arg.llvmType);
            }
            for (const auto &arg : newFuncDecl->getProto().getExternalArgs()) {
              args.External.args.push_back(
                  std::make_unique<VariableExprAST>(arg.name));
            }
            for (const auto &arg : newFuncDecl->getProto().getInternalArgs()) {
              if (arg.name != "__state") {
                args.Internal.args.push_back(
                    std::make_unique<VariableExprAST>(arg.name));
              }
            }
          } else {
            auto protoIt = state.FunctionProtos.find(calleeName);
            if (protoIt != state.FunctionProtos.end()) {
              const auto &protoOuts = protoIt->second->getOutArgs();
              while (args.MainOut.args.size() < protoOuts.size()) {
                const auto &arg = protoOuts[args.MainOut.args.size()];
                // TODO: Ensure this generated intermediate symbol is managed
                // when checking for symbol clashes across scopes.
                std::string intermediateName =
                    "__intermediate_" + state.getName() + "_" + arg.name;
                if (func) {
                  func->setCompilerProperty(
                      "intermediateOutput",
                      std::make_shared<EntityNode>(intermediateName,
                                                   func->getFilename().c_str(),
                                                   func->getLine()));
                }
                args.MainOut.args.push_back(
                    std::make_unique<VariableExprAST>(intermediateName));
                args.MainOut.argTypes.push_back(arg.llvmType);
              }
              for (const auto &arg : protoIt->second->getExternalArgs()) {
                args.External.args.push_back(
                    std::make_unique<VariableExprAST>(arg.name));
              }
              for (const auto &arg : protoIt->second->getInternalArgs()) {
                if (arg.name != "__state") {
                  args.Internal.args.push_back(
                      std::make_unique<VariableExprAST>(arg.name));
                }
              }
            }
          }
          std::vector<std::unique_ptr<ExprAST>> PortPropArgs;
          if (funcDecl) {
            auto usedPortProps = CodeAnalysis::getUsedMemberAccesses(funcDecl);
            auto innerScope = scope;
            std::vector<ASTNode> innerEntities;
            auto blocksNode = funcDecl->getPropertyValue("blocks");
            if (blocksNode) {
              innerEntities = blocksNode->getChildren();
            }
            innerScope.push_back({funcDecl, innerEntities});
            for (const auto &ppNode : usedPortProps) {
              if (ppNode->getPropertyName() == "size") {
                auto size = CodeAnalysis::evaluateSizeMemberAccess(
                    ppNode->getEntity(), innerScope, funcDecl, func, tree);
                if (size > 0) {
                  PortPropArgs.push_back(
                      std::make_unique<IntExprAST>(size, 32));
                } else {
                  std::unique_ptr<ExprAST> dynamicSizeExpr;
                  auto inBlockNode =
                      func ? func->getCompilerProperty("inputBlock") : nullptr;
                  if (!inBlockNode && func) {
                    inBlockNode = func->getCompilerProperty("mainInput");
                  }
                  if (inBlockNode) {
                    std::string inName = ASTQuery::getNodeName(inBlockNode);
                    auto inDecl =
                        ASTQuery::findDeclarationByName(inName, scope, tree);
                    if (inDecl &&
                        inDecl->getNodeType() == AST::ArrayDeclaration) {
                      auto *arrNode = static_cast<ArrayNode *>(
                          inDecl->getArrayIndex().get());
                      if (arrNode && arrNode->index()) {
                        auto idxChildren = arrNode->index()->getChildren();
                        if (!idxChildren.empty()) {
                          auto firstIdx = idxChildren[0];
                          if (firstIdx->getNodeType() == AST::MemberAccess) {
                            auto pp =
                                std::static_pointer_cast<MemberAccessNode>(
                                    firstIdx);
                            dynamicSizeExpr = std::make_unique<MemberAccessAST>(
                                pp->getEntity(), pp->getPropertyName());
                          } else if (firstIdx->getNodeType() == AST::Entity) {
                            dynamicSizeExpr = std::make_unique<VariableExprAST>(
                                std::static_pointer_cast<EntityNode>(firstIdx)
                                    ->getName());
                          }
                        }
                      }
                    }
                  }
                  if (!dynamicSizeExpr) {
                    dynamicSizeExpr = std::make_unique<MemberAccessAST>(
                        ppNode->getEntity(), ppNode->getPropertyName());
                  }
                  PortPropArgs.push_back(std::move(dynamicSizeExpr));
                }
              } else if (ppNode->getPropertyName() == "rate") {
                auto rate = CodeAnalysis::evaluateRateMemberAccess(
                    ppNode->getEntity(), innerScope, funcDecl, func, tree);
                if (rate > 0) {
                  PortPropArgs.push_back(std::make_unique<RealExprAST>(rate));
                } else {
                  PortPropArgs.push_back(std::make_unique<MemberAccessAST>(
                      ppNode->getEntity(), ppNode->getPropertyName()));
                }
              }
            }
            if (funcDecl->getEntityType() == "module") {
              LOG_DEBUG() << "Module instance:" << std::endl;
              for (const auto &declNode : innerEntities) {
                if (declNode->getNodeType() == AST::Declaration ||
                    declNode->getNodeType() == AST::ArrayDeclaration) {
                  auto decl =
                      std::static_pointer_cast<DeclarationNode>(declNode);

                  if (!ASTQuery::isPortEntity(decl, funcDecl, scope,
                                              tree) &&
                      decl->getCompilerProperty("persistent")) {
                    // Only modules create external instances for their internal
                    // values to have persistence.
                    // TODO modify to group in a state struct as globals will
                    // be less efficient
                    state.createGlobal(decl);
                  }
                } else {
                  LOG_ERROR() << "Expected declaration, got: "
                              << AST::toText(declNode);
                }
              }
            }
          }
          // -----------------------------------------------------------
          // Function call expr
          auto callexpr = std::make_unique<CallExprAST>(
              calleeName, std::move(args.MainOut.args),
              std::move(args.MainIn.args), std::move(args.Properties.args),
              std::move(args.Internal.args), std::move(args.External.args),
              std::move(PortPropArgs), std::move(args.MainOut.argTypes),
              std::move(args.MainIn.argTypes),
              std::move(args.Properties.argTypes), state.getName());
          if (func) {
            callexpr->setLocationFrom(func);
          }
          auto *callTypeTree = state.findTypeTreeNode(func);
          if (!callTypeTree && funcDecl) {
            callTypeTree = state.findTypeTreeNode(funcDecl);
          }
          if (state.doesNodeNeedState(callTypeTree)) {
            callexpr->calleeNeedsState = true;
            callexpr->funcInstance = func;
          }
          if (newFuncDecl) {
            callexpr->callType = newFuncDecl->callType;
            generated[domainName].functions.push_back(std::move(newFuncDecl));
          } else {
            // Function already declared
            if (funcDecl) {
              if (funcDecl->getEntityType() == "module") {
                callexpr->callType = CallableType::Module;
              } else if (funcDecl->getEntityType() == "reaction" ||
                         funcDecl->getEntityType() == "action") {
                callexpr->callType = CallableType::Reaction;
              } else if (funcDecl->getEntityType() == "loop") {
                callexpr->callType = CallableType::Loop;
              } else {
                auto typeDecl =
                    ASTQuery::findTypeDeclaration(funcDecl, scope, tree);
                if (typeDecl) {
                  auto inherited =
                      ASTQuery::getInheritedTypes(typeDecl, scope, tree);
                  bool resolved = false;
                  for (const auto &p : inherited) {
                    if (p->getName() == "_Reaction") {
                      callexpr->callType = CallableType::Reaction;
                      resolved = true;
                      break;
                    } else if (p->getName() == "_Loop") {
                      callexpr->callType = CallableType::Loop;
                      resolved = true;
                      break;
                    } else if (p->getName() == "_Module") {
                      callexpr->callType = CallableType::Module;
                      resolved = true;
                      break;
                    }
                  }
                  if (!resolved) {
                    LOG_INFO() << " ERROR: Can't set callable type" << std::endl;
                  }
                } else {
                  LOG_INFO() << " ERROR: Can't set callable type" << std::endl;
                }
              }
            } else {
              LOG_INFO() << " ERROR: Can't set callable type" << std::endl;
            }
          }
          generated[domainName].expr.push_back(std::move(callexpr));
        } else {
          LOG_ERROR() << "ERROR: Could not find function declaration: "
                      << func->getName() << std::endl;
        }
      }
      state.popName();
    } else if (current->getNodeType() == AST::Int ||
               current->getNodeType() == AST::Real ||
               current->getNodeType() == AST::String) {
      generated[domainName].expr.push_back(createExpr(current));
    } else if (current->getNodeType() == AST::List) {
      generated[domainName].expr.push_back(createExpr(current));
      // FIXME look recursively inside elements
      for (const auto &elem : current->getChildren()) {
        auto decl = elem->getCompilerProperty("declaration");
        if (decl) {
          if (decl->getCompilerProperty("reads")) {
            generated[domainName].readVariables.push_back(elem);
          }
        }
      }
    } else if (current->getNodeType() == AST::Switch) {
      if (current == stream->getLeft() && isTriggeredReactionStream) {
        // Skip pushing the trigger constant
      } else {
        generated[domainName].expr.push_back(createExpr(current));
      }
    } else if (current->getNodeType() == AST::MemberAccess) {
      if (current == stream->getLeft() && isTriggeredReactionStream) {
        // Skip pushing trigger switch
      } else {
        generated[domainName].expr.push_back(createExpr(current));
      }
    } else {
      LOG_ERROR() << "ERROR: Unsupported type" << std::endl;
    }
  };
  for (auto &domainCode : generated) {
    flushRateSegment(domainCode.first);
    auto segIt = rateSegments.find(domainCode.first);
    if (segIt != rateSegments.end() && !segIt->second.committedExprs.empty()) {
      domainCode.second.expr = std::move(segIt->second.committedExprs);
    }
  }
  if (isTriggeredReactionStream) {
    for (auto &domainCode : generated) {
      if (!domainCode.second.expr.empty()) {
        std::unique_ptr<ExprAST> condExpr;
        std::string condName = "stream_cond";
        if (leftDecl) {
          condName = leftDecl->getName();
          condExpr = std::make_unique<VariableExprAST>(condName);
        } else if (leftNode) {
          if (leftNode->getNodeType() == AST::MemberAccess) {
            auto pp = std::static_pointer_cast<MemberAccessNode>(leftNode);
            condName = pp->getEntity() + "_" + pp->getPropertyName();
          }
          condExpr = createExpr(leftNode);
        }
        if (condExpr) {
          std::vector<std::unique_ptr<ExprAST>> innerExprs =
              std::move(domainCode.second.expr);
          domainCode.second.expr.clear();
          domainCode.second.expr.push_back(std::make_unique<ResetExprAST>(
              condName, std::move(condExpr), std::move(innerExprs)));
        }
      }
    }
  }
  return generated;
}

bool StrideGenerator::resolveIOParamsFromDefinition(
    std::shared_ptr<DeclarationNode> funcDecl,
    std::shared_ptr<FunctionNode> funcInstance, ASTNode tree,
    ScopeStack &functionScope, StrideCompiler &state,
    std::vector<PrototypeArg> &InParams, std::vector<PrototypeArg> &OutParams,
    std::vector<PrototypeArg> &InternalParams,
    std::vector<PrototypeArg> &InternalPersistentParams,
    std::vector<PrototypeArg> &PropertyParams,
    std::vector<PrototypeArg> &ExternalParams,
    std::vector<std::shared_ptr<DeclarationNode>> &usedInternalVariables) {
  bool ioParamsResolved = false;
  auto portsList = funcDecl->getPropertyValue("ports");
  if (portsList && portsList->getNodeType() == AST::List) {
    bool canResolve = true;
    std::vector<PrototypeArg> tempIn;
    std::vector<PrototypeArg> tempOut;
    std::vector<PrototypeArg> tempProperties;
    for (const auto &portNode : portsList->getChildren()) {
      auto portDecl = std::static_pointer_cast<DeclarationNode>(portNode);
      auto portTypeStr = portDecl->getEntityType();
      if (portTypeStr == "mainInputPort" || portTypeStr == "mainOutputPort" ||
          portTypeStr == "propertyInputPort" ||
          portTypeStr == "propertyOutputPort") {
        auto portEntityNode = portDecl->getPropertyValue("block");
        if (portEntityNode && portEntityNode->getNodeType() == AST::Entity) {
          auto entityName =
              std::static_pointer_cast<EntityNode>(portEntityNode)->getName();
          auto entityDecl =
              ASTQuery::findDeclarationByName(entityName, functionScope, tree);
          if (!entityDecl) {
            canResolve = false;
            break;
          }
          if (entityDecl->getEntityType() != "switch" &&
              entityDecl->getEntityType() != "trigger" &&
              entityDecl->getEntityType() != "string") {
            auto typeNode = entityDecl->getPropertyValue("type");
            if (!typeNode) {
              canResolve = false;
              break;
            }
          }
          auto type = state.getLLVMTypeForCodegenEntity(entityDecl, funcDecl,
                                                        funcInstance);
          if (!type) {
            canResolve = false;
            break;
          }
          if (portTypeStr == "mainInputPort") {
            tempIn.push_back(
                PrototypeArg{ASTQuery::getNodeName(entityDecl), type});
          } else if (portTypeStr == "mainOutputPort") {
            tempOut.push_back(
                PrototypeArg{ASTQuery::getNodeName(entityDecl), type});
          } else if (portTypeStr == "propertyInputPort" ||
                     portTypeStr == "propertyOutputPort") {
            tempProperties.push_back(
                PrototypeArg{ASTQuery::getNodeName(entityDecl), type});
          }
        }
      }
    }
    if (canResolve) {
      InParams = std::move(tempIn);
      OutParams = std::move(tempOut);
      PropertyParams = std::move(tempProperties);
      ioParamsResolved = true;
    }
  } else if (portsList && portsList->getNodeType() == AST::None) {
    ioParamsResolved = true;
  }

  if (ioParamsResolved) {
    auto usedEntities = CodeAnalysis::getUsedEntitiesInStreams(funcDecl);
    auto termNode = funcDecl->getPropertyValue("terminateWhen");
    if (termNode && termNode->getNodeType() == AST::Entity) {
      usedEntities.push_back(termNode);
    }
    auto onExecNode = funcDecl->getPropertyValue("onExecution");
    if (onExecNode && onExecNode->getNodeType() == AST::Entity) {
      usedEntities.push_back(onExecNode);
    }
    std::vector<std::string> seen;
    for (const auto &entity : usedEntities) {
      std::shared_ptr<DeclarationNode> outEntityDecl;
      CodeAnalysis::NodeRole role = CodeAnalysis::determineNodeRole(
          entity, funcDecl, functionScope, tree, outEntityDecl);
      if (role == CodeAnalysis::NodeRole::Internal ||
          role == CodeAnalysis::NodeRole::Persistent ||
          role == CodeAnalysis::NodeRole::External) {
        if (outEntityDecl) {
          std::string entityName = ASTQuery::getNodeName(outEntityDecl);
          if (std::find(seen.begin(), seen.end(), entityName) == seen.end()) {
            seen.push_back(entityName);
            auto type = state.getLLVMTypeForCodegenEntity(
                outEntityDecl, funcDecl, funcInstance);
            if (type) {
              PrototypeArg arg{entityName, type};
              auto funcType =
                  ASTQuery::findTypeDeclaration(funcDecl, functionScope, tree);
              bool isDomainMember =
                  funcType
                      ? ASTQuery::isDomainMember(funcType, functionScope, tree)
                      : false;
              bool isCodeGenerator =
                  funcType
                      ? ASTQuery::isCodeGenerator(funcType, functionScope, tree)
                      : false;

              if (role == CodeAnalysis::NodeRole::External) {
                ExternalParams.push_back(arg);
              } else if (isDomainMember && isCodeGenerator) {
                usedInternalVariables.push_back(outEntityDecl);
              } else if (role == CodeAnalysis::NodeRole::Persistent) {
                InternalPersistentParams.push_back(arg);
              } else if (role == CodeAnalysis::NodeRole::Internal) {
                if (outEntityDecl->getPropertyValue("persistent")) {
                  InternalPersistentParams.push_back(arg);
                } else {
                  usedInternalVariables.push_back(outEntityDecl);
                }
              }
            }
          }
        }
      }
    }
  }

  return ioParamsResolved;
}

bool StrideGenerator::isPolymorphicCallable(
    std::shared_ptr<DeclarationNode> funcDecl) {
  if (!funcDecl) {
    return false;
  }
  const auto &entityType = funcDecl->getEntityType();
  if (entityType != "module" && entityType != "loop") {
    return false;
  }
  auto blocks = funcDecl->getPropertyValue("blocks");
  if (!blocks) {
    return false;
  }
  for (const auto &node : blocks->getChildren()) {
    if (node->getNodeType() != AST::Declaration &&
        node->getNodeType() != AST::ArrayDeclaration) {
      continue;
    }
    auto typeNode =
        std::static_pointer_cast<DeclarationNode>(node)->getPropertyValue(
            "type");
    if (typeNode && typeNode->getNodeType() == AST::MemberAccess) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<FunctionAST> StrideGenerator::createFunctionDeclaration(
    std::shared_ptr<DeclarationNode> funcDecl,
    std::shared_ptr<FunctionNode> funcInstance, ASTNode tree, ScopeStack *scope,
    StrideCompiler &state, std::string *symbolName) {

  std::string funcName = ASTQuery::getNodeName(funcDecl);
  if (symbolName) {
    *symbolName = funcName;
  }
  // A polymorphic callable gets one function per type signature, so it can
  // only be deduplicated once its parameter types are resolved below.
  // Standalone generation has no instance and keeps the plain name.
  const bool polymorphic = funcInstance && isPolymorphicCallable(funcDecl);
  // Port types (`type: OutputPort.type`) are resolved from what this
  // instance connects to, which is declared in the caller's scope.
  StrideCompiler::ConnectionScopeGuard connectionScope(state, scope, tree);

  if (!polymorphic) {
    llvm::Function *TheFunction = state.getFunctionInModule(funcName);
    if (TheFunction) {
      LOG_DEBUG() << " Function already defined: " << funcName << std::endl;
      return nullptr;
    }
  }

  auto functionScope = *scope;
  if (functionScope.size() == 0) {
    functionScope.push_back({nullptr, {}});
  }
  functionScope.push_back({funcDecl, {}});
  auto blocks = funcDecl->getPropertyValue("blocks");
  if (blocks) {
    for (const auto &declNode : blocks->getChildren()) {
      functionScope.back().second.push_back(declNode);
    }
  }

  // Analyze function parameters. FIrst check to see if the types are fully
  // defined, then if they are not, infer them from connections.

  std::vector<PrototypeArg> InParams;
  std::vector<PrototypeArg> OutParams;
  std::vector<PrototypeArg> InternalParams; // Internal only, on stack
  std::vector<PrototypeArg>
      InternalPersistentParams; // Internal but passed to function arguments
  std::vector<PrototypeArg> PropertyParams;
  std::vector<PrototypeArg> ExternalParams;
  std::vector<PrototypeArg> UsedPortProperties;

  // Used internal and external variables are variables within this scope's
  // streams. Internal if they are in the scope, external if they are in the
  // tree
  std::vector<std::shared_ptr<DeclarationNode>> usedInternalVariables;
  std::vector<std::shared_ptr<DeclarationNode>> usedExternalVariables;

  bool ioParamsResolved = resolveIOParamsFromDefinition(
      funcDecl, funcInstance, tree, functionScope, state, InParams, OutParams,
      InternalParams, InternalPersistentParams, PropertyParams, ExternalParams,
      usedInternalVariables);

  // If IO parameters are not resolved, infer them from connections
  if (!ioParamsResolved) {
    auto *nodeTree = state.m_intanceTree.find(funcInstance);
    if (!nodeTree && funcDecl) {
      nodeTree = state.m_intanceTree.find(funcDecl);
    }
    if (nodeTree) {
      for (const auto &var : nodeTree->input) {
        auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
        auto type =
            state.getLLVMTypeForCodegenEntity(decl, funcDecl, funcInstance);
        InParams.push_back(
            PrototypeArg{ASTQuery::getNodeName(var.first), type});
      }

      for (const auto &var : nodeTree->output) {
        auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
        auto type =
            state.getLLVMTypeForCodegenEntity(decl, funcDecl, funcInstance);
        OutParams.push_back(
            PrototypeArg{ASTQuery::getNodeName(var.first), type});
      }

      auto fallbackFuncType =
          ASTQuery::findTypeDeclaration(funcDecl, functionScope, tree);
      bool fallbackIsDomainMember =
          fallbackFuncType
              ? ASTQuery::isDomainMember(fallbackFuncType, functionScope, tree)
              : false;
      bool fallbackIsCodeGenerator =
          fallbackFuncType
              ? ASTQuery::isCodeGenerator(fallbackFuncType, functionScope, tree)
              : false;

      for (const auto &var : nodeTree->internal) {
        auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
        auto type =
            state.getLLVMTypeForCodegenEntity(decl, funcDecl, funcInstance);
        if (fallbackIsDomainMember && fallbackIsCodeGenerator) {
          usedInternalVariables.push_back(decl);
        } else if (decl->getPropertyValue("persistent")) {
          InternalPersistentParams.push_back(
              PrototypeArg{ASTQuery::getNodeName(var.first), type});
        } else {
          usedInternalVariables.push_back(decl);
        }
      }

      // Persistent values are external
      for (const auto &var : nodeTree->persistent) {
        auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
        auto type =
            state.getLLVMTypeForCodegenEntity(decl, funcDecl, funcInstance);
        if (fallbackIsDomainMember && fallbackIsCodeGenerator) {
          usedInternalVariables.push_back(decl);
        } else {
          ExternalParams.push_back(
              PrototypeArg{ASTQuery::getNodeName(var.first), type});
        }
      }

      for (const auto &var : nodeTree->external) {
        if (var.first->getNodeType() == AST::Declaration ||
            var.first->getNodeType() == AST::ArrayDeclaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
          auto type =
              state.getLLVMTypeForCodegenEntity(decl, funcDecl, funcInstance);
          ExternalParams.push_back(
              PrototypeArg{ASTQuery::getNodeName(var.first), type});
        } else if (var.first->getNodeType() == AST::MemberAccess) {
          // Port properties are added in a separate pass
          // TODO move this pass here?
        } else {
          LOG_ERROR() << "Error unexpected type: " << var.first->toText()
                      << std::endl;
        }
      }
    } else {
      LOG_ERROR() << "ERROR: type tree not found" << std::endl;
    }
  }

  auto usedPortPropertiesNodes = CodeAnalysis::getUsedMemberAccesses(funcDecl);
  for (const auto &pp : usedPortPropertiesNodes) {
    // FIXME validate unique name
    std::string name = pp->getEntity() + "_" + pp->getPropertyName();
    // TODO determine the type of integer for port properties from platform
    // defintion.
    if (pp->getPropertyName() == "size") {
      UsedPortProperties.push_back(
          PrototypeArg{name, llvm::Type::getInt32Ty(*state.TheContext)});
    } else if (pp->getPropertyName() == "rate") {
      UsedPortProperties.push_back(
          PrototypeArg{name, llvm::Type::getDoubleTy(*state.TheContext)});
    }
  }

  auto *nodeTree =
      funcInstance ? state.findTypeTreeNode(funcInstance) : nullptr;
  if (!nodeTree && funcDecl) {
    nodeTree = state.findTypeTreeNode(funcDecl);
  }
  bool funcNeedsState = state.doesNodeNeedState(nodeTree);
  if (funcNeedsState) {
    InternalPersistentParams.clear();
    InternalPersistentParams.push_back(
        PrototypeArg{"__state", llvm::PointerType::get(*state.TheContext, 0)});
  }

  if (polymorphic) {
    std::vector<llvm::Type *> signature;
    for (const auto *params : {&OutParams, &InParams, &PropertyParams}) {
      for (const auto &param : *params) {
        signature.push_back(param.llvmType);
      }
    }
    funcName =
        state.mangler.mangle(ASTQuery::getNodeName(funcDecl), {}, signature);
    if (symbolName) {
      *symbolName = funcName;
    }
    if (state.getFunctionInModule(funcName)) {
      LOG_DEBUG() << " Function already defined: " << funcName << std::endl;
      return nullptr;
    }
  }

  std::vector<std::unique_ptr<ExprAST>> collected;
  // Generate function code by going through stream blocks.
  auto streamBlocks =
      ASTQuery::getContainedStreamBlocks(funcDecl, functionScope, tree);
  for (const auto &streamBlock : streamBlocks) {
    for (const auto &stream : streamBlock.streams) {
      auto code = createStreamCode(stream, tree, functionScope, state);
      for (auto &domainCode : code) {
        while (domainCode.second.expr.size() > 0) {
          collected.push_back(std::move(domainCode.second.expr.front()));
          domainCode.second.expr.erase(domainCode.second.expr.begin());
        }
        // FIXME nested functions need to have their name mangled/hashed as
        // they move to the global namespace
        for (const auto &f : domainCode.second.functions) {
          f->codegen(state);
        }
      }
    }
  }

  auto proto = std::make_unique<PrototypeAST>(
      funcName, OutParams, InParams, InternalPersistentParams, PropertyParams,
      ExternalParams, UsedPortProperties);

  // llvm::Function *TheFunction = state.getFunctionInModule(P.getName());
  auto newfunc =
      std::make_unique<FunctionAST>(std::move(proto), std::move(collected));
  newfunc->internalVariables = usedInternalVariables;
  newfunc->hasState = funcNeedsState;
  newfunc->funcInstance =
      funcInstance ? ASTNode(funcInstance) : ASTNode(funcDecl);
  if (funcDecl) {
    newfunc->line = funcDecl->getLine();
    newfunc->column = funcDecl->getColumn();
    newfunc->relativeFilename = funcDecl->getRelativeFilename();
    newfunc->absoluteFilename = funcDecl->getAbsoluteFilename();
  }
  newfunc->displayName = funcName;

  if (funcDecl->getEntityType() == "module") {
    newfunc->callType = CallableType::Module;
  } else if (funcDecl->getEntityType() == "reaction" ||
             funcDecl->getEntityType() == "action") {
    newfunc->callType = CallableType::Reaction;
  } else if (funcDecl->getEntityType() == "loop") {
    newfunc->callType = CallableType::Loop;
  } else if (funcDecl->getEntityType() == "platformModule") {
    newfunc->callType = CallableType::External;
  } else {
    auto typeDecl =
        ASTQuery::findTypeDeclaration(funcDecl, functionScope, tree);
    if (typeDecl) {
      auto inherited =
          ASTQuery::getInheritedTypes(typeDecl, functionScope, tree);
      bool resolved = false;
      for (const auto &p : inherited) {
        if (p->getName() == "_Reaction") {
          newfunc->callType = CallableType::Reaction;
          resolved = true;
          break;
        } else if (p->getName() == "_Loop") {
          newfunc->callType = CallableType::Loop;
          resolved = true;
          break;
        } else if (p->getName() == "_Module") {
          newfunc->callType = CallableType::Module;
          resolved = true;
          break;
        }
      }
      if (!resolved) {
        LOG_ERROR() << "Callable type unsupported: "
                    << funcDecl->getEntityType() << std::endl;
      }
    } else {
      LOG_ERROR() << "Callable type unsupported: " << funcDecl->getEntityType()
                  << std::endl;
    }
  }

  auto terminateWhenNode = funcDecl->getPropertyValue("terminateWhen");
  if (terminateWhenNode) {
    if (terminateWhenNode->getNodeType() == AST::Entity) {
      newfunc->terminateWhenName =
          std::static_pointer_cast<EntityNode>(terminateWhenNode)->getName();
    } else if (terminateWhenNode->getNodeType() == AST::String) {
      newfunc->terminateWhenName =
          std::static_pointer_cast<ValueNode>(terminateWhenNode)
              ->getStringValue();
    }
  }
  return newfunc;
}

void StrideGenerator::generatePlatformFunctionSignature(
    std::shared_ptr<DeclarationNode> decl, std::vector<ASTNode> &frameworkScope,
    StrideCompiler &state) {
  std::vector<llvm::Type *> parameters;
  llvm::Type *retType = nullptr;
  auto inputList = decl->getPropertyValue("inputs");
  auto outputList = decl->getPropertyValue("outputs");
  auto functionNameNode = decl->getPropertyValue("processing");
  if (inputList && outputList && functionNameNode) {
    if (std::find(frameworkScope.begin(), frameworkScope.end(), decl) ==
        frameworkScope.end()) {
      frameworkScope.push_back(decl);
    }
    bool isConstrainedList = false;
    bool isHeterogeneousVariadic = false;
    llvm::Type *elemType = nullptr;
    std::vector<llvm::Type *> prefixTypes;
    std::vector<llvm::Type *> variadicTypes;
    int minItems = 0;
    int maxItems = -1;

    for (const auto &input : inputList->getChildren()) {
      if (input->getNodeType() == AST::Declaration) {
        auto declChild = std::static_pointer_cast<DeclarationNode>(input);
        if (declChild->getEntityType() == "variadic" ||
            declChild->getEntityType() == "constrainedList") {
          isConstrainedList = true;
          if (declChild->getEntityType() == "variadic") {
            minItems = 1;
          }
          ASTNode minNode = declChild->getPropertyValue("min");
          if (!minNode) {
            minNode = declChild->getPropertyValue("minItems");
          }
          if (minNode && minNode->getNodeType() == AST::Int) {
            minItems = static_cast<int>(
                std::static_pointer_cast<ValueNode>(minNode)->getIntValue());
          }
          ASTNode maxNode = declChild->getPropertyValue("max");
          if (!maxNode) {
            maxNode = declChild->getPropertyValue("maxItems");
          }
          if (maxNode && maxNode->getNodeType() == AST::Int) {
            maxItems = static_cast<int>(
                std::static_pointer_cast<ValueNode>(maxNode)->getIntValue());
          }
          ASTNode elemTypeNode = declChild->getPropertyValue("allowed");
          if (!elemTypeNode) {
            elemTypeNode = declChild->getPropertyValue("types");
          }
          auto addAllowedTypeName = [&](const std::string &typeName) {
            if (!typeName.empty() &&
                state.typesMap.find(typeName) != state.typesMap.end()) {
              variadicTypes.push_back(state.typesMap[typeName]);
            }
          };
          if (elemTypeNode) {
            if (elemTypeNode->getNodeType() == AST::List) {
              for (const auto &child : elemTypeNode->getChildren()) {
                if (child->getNodeType() == AST::Entity) {
                  addAllowedTypeName(
                      std::static_pointer_cast<EntityNode>(child)->getName());
                } else if (child->getNodeType() == AST::String) {
                  addAllowedTypeName(
                      std::static_pointer_cast<ValueNode>(child)
                          ->getStringValue());
                }
              }
            } else if (elemTypeNode->getNodeType() == AST::Entity) {
              addAllowedTypeName(
                  std::static_pointer_cast<EntityNode>(elemTypeNode)
                      ->getName());
            } else if (elemTypeNode->getNodeType() == AST::String) {
              addAllowedTypeName(
                  std::static_pointer_cast<ValueNode>(elemTypeNode)
                      ->getStringValue());
            }
          }
          if (!variadicTypes.empty()) {
            elemType = variadicTypes[0];
            if (variadicTypes.size() > 1) {
              isHeterogeneousVariadic = true;
            }
          }
          parameters.push_back(llvm::PointerType::get(*state.TheContext, 0));
          parameters.push_back(llvm::Type::getInt32Ty(*state.TheContext));
          break;
        }
      } else if (input->getNodeType() == AST::Entity) {
        auto inputBlock = std::static_pointer_cast<EntityNode>(input);
        auto inputType = inputBlock->getName();
        if (state.typesMap.find(inputType) != state.typesMap.end()) {
          parameters.push_back(state.typesMap[inputType]);
          prefixTypes.push_back(state.typesMap[inputType]);
        } else {
          LOG_ERROR() << "Input Type not mapped: " << inputType << std::endl;
        }
      }
    }
    assert(outputList->getChildren().size() < 2);
    if (outputList->getChildren().size() != 0) {
      auto outputBlock = outputList->getChildren()[0];
      if (outputBlock->getNodeType() == AST::Entity) {
        auto outputType =
            std::static_pointer_cast<EntityNode>(outputBlock)->getName();
        if (state.typesMap.find(outputType) != state.typesMap.end()) {
          retType = state.typesMap[outputType];
        } else {
          LOG_ERROR() << " Output Type not mapped: " << outputType << std::endl;
        }
      }
    } else {
      retType = state.typesMap[""];
    }

    if (!retType) {
      LOG_ERROR() << "Cannot register platform function " << decl->getName()
                  << " due to unmapped return type" << std::endl;
      return;
    }
    if (!isConstrainedList &&
        parameters.size() != inputList->getChildren().size()) {
      LOG_ERROR() << "Cannot register platform function " << decl->getName()
                  << " due to unmapped parameter type" << std::endl;
      return;
    }

    // Always store the function signature, as there may be multiple overloads
    // (e.g. Greater@Int_Bool, Greater@Double_Bool)
    auto name =
        std::static_pointer_cast<ValueNode>(functionNameNode)->getStringValue();
    llvm::FunctionType *FT =
        llvm::FunctionType::get(retType, parameters, false);

    ExternalFunction extFunc;
    extFunc.name = name;
    extFunc.llvmFunctionType = FT;
    extFunc.isConstrainedList = isConstrainedList;
    extFunc.isHeterogeneousVariadic = isHeterogeneousVariadic;
    extFunc.elemType = elemType;
    extFunc.prefixTypes = std::move(prefixTypes);
    extFunc.variadicTypes = std::move(variadicTypes);
    extFunc.minItems = minItems;
    extFunc.maxItems = maxItems;

    auto &funcList = state.functionMap[decl->getName()];
    for (const auto &existing : funcList) {
      if (existing.name == name && existing.llvmFunctionType == FT) {
        return; // Already registered
      }
    }
    funcList.push_back(extFunc);
    std::string atName;
    auto atNode = decl->getCompilerProperty("_at");
    if (atNode && atNode->getNodeType() == AST::String) {
      atName =
          "@" + std::static_pointer_cast<ValueNode>(atNode)->getStringValue();
    }
    LOG_DEBUG() << "Loaded platform module: " << decl->getName() << atName
                << std::endl;
  }
}

void StrideGenerator::setTypeCastMetadata(ASTNode node, ExprAST *V) {
  if (auto typecastNode = node->getCompilerProperty("typecast")) {
    if (typecastNode->getNodeType() == AST::String) {
      V->typecast =
          std::static_pointer_cast<ValueNode>(typecastNode)->getStringValue();
    }
  }
  switch (node->getNodeType()) {
  case AST::Int:
    V->dataType = DataTypes::Int;
    break;
  case AST::Real:
    V->dataType = DataTypes::Real;
    break;
  case AST::Switch:
    V->dataType = DataTypes::Switch;
    break;
  case AST::String:
    V->dataType = DataTypes::String;
    break;
  case AST::Entity:
  case AST::Array: {
    auto declNode = node->getCompilerProperty("declaration");
    if (declNode && (declNode->getNodeType() == AST::Declaration ||
                     declNode->getNodeType() == AST::ArrayDeclaration)) {
      V->dataType = StrideCompiler::getDataTypeName(
          std::static_pointer_cast<DeclarationNode>(declNode));
    }
    break;
  }
  default:
    break;
  }
}

DefaultVariant
StrideGenerator::getDefaultValue(std::shared_ptr<DeclarationNode> decl,
                                 StrideCompiler &state) {
  DefaultVariant defaultValue;
  auto *type = state.getLLVMType(decl);

  if (type->isFloatingPointTy()) {
    defaultValue = double(0.0);
  } else if (type->isIntegerTy() && !type->isIntegerTy(1)) {
    defaultValue = int32_t(0);
  } else if (type->isIntegerTy(1)) {
    defaultValue = bool(false);
  } else if (type->isStructTy()) {
    defaultValue = std::string("");
  } else {
    defaultValue = double(0.0);
  }

  if (decl->getEntityType() == "signal" || decl->getEntityType() == "string") {
    auto defaultNode = decl->getPropertyValue("default");
    if (defaultNode && defaultNode->getNodeType() == AST::String) {
      defaultValue =
          std::static_pointer_cast<ValueNode>(defaultNode)->getStringValue();
    } else if (defaultNode && (defaultNode->getNodeType() == AST::Int ||
                               defaultNode->getNodeType() == AST::Real)) {
      if (type->isFloatingPointTy()) {
        defaultValue =
            std::static_pointer_cast<ValueNode>(defaultNode)->toReal();
      } else if (type->isIntegerTy() && !type->isIntegerTy(1)) {
        if (defaultNode->getNodeType() == AST::Int) {
          auto intValue =
              std::static_pointer_cast<ValueNode>(defaultNode)->getIntValue();
          if (intValue <= INT32_MAX && intValue >= INT32_MIN) {
            defaultValue = (int32_t)intValue;
          } else {
            defaultValue = (int64_t)intValue;
          }
        } else if (defaultNode->getNodeType() == AST::Real) {
          auto doubleValue =
              std::static_pointer_cast<ValueNode>(defaultNode)->getRealValue();
          assert(doubleValue <= INT32_MAX);
          assert(doubleValue >= INT32_MIN);
          defaultValue = (int32_t)doubleValue;
        }
      }
    }
  } else if (decl->getEntityType() == "switch" ||
             decl->getEntityType() == "trigger") {
    auto defaultNode = decl->getPropertyValue("default");
    if (defaultNode && defaultNode->getNodeType() == AST::Switch) {
      if (type->isIntegerTy(1)) {
        auto switchValue =
            std::static_pointer_cast<ValueNode>(defaultNode)->getSwitchValue();
        defaultValue = (bool)switchValue;
      }
    }
  }
  return defaultValue;
}

std::unique_ptr<FunctionAST> StrideGenerator::generateStandaloneFunction(
    std::shared_ptr<DeclarationNode> funcDecl, ASTNode tree, ScopeStack &scope,
    StrideCompiler &state) {
  if (!funcDecl) {
    return nullptr;
  }
  state.m_tree = tree;
  state.registerDataTypes(tree);
  if (tree) {
    for (const auto &node : tree->getChildren()) {
      if (node->getNodeType() == AST::Declaration ||
          node->getNodeType() == AST::ArrayDeclaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(node);
        if (decl->getEntityType() == "platformModule") {
          std::vector<ASTNode> dummy;
          StrideGenerator::generatePlatformFunctionSignature(
              decl, scope.empty() ? dummy : scope.back().second, state);
        }
      }
    }
  }
  state.m_intanceTree = CodeAnalysis::getStateStructInformationForDeclaration(
      funcDecl, scope, tree);
  state.buildStateStructTypes(state.m_intanceTree, scope, tree);
  auto func = createFunctionDeclaration(funcDecl, nullptr, tree, &scope, state);
  if (func) {
    auto *llvmFunc = func->codegen(state);
    if (llvmFunc) {
      generateInvoker(llvmFunc, state);
    }
  }
  return func;
}

void StrideGenerator::generateInvoker(llvm::Function *TheFunction,
                                      StrideCompiler &state) {
  if (!TheFunction)
    return;
  std::string invokerName = TheFunction->getName().str() + "_invoker";
  if (state.TheModule->getFunction(invokerName))
    return;

  auto *retTy = llvm::Type::getInt32Ty(*state.TheContext);
  auto *ptrTy = llvm::PointerType::get(*state.TheContext, 0);
  auto *funcTy = llvm::FunctionType::get(retTy, {ptrTy}, false);
  auto *invoker =
      llvm::Function::Create(funcTy, llvm::Function::ExternalLinkage,
                             invokerName, state.TheModule.get());

  auto *entry = llvm::BasicBlock::Create(*state.TheContext, "entry", invoker);
  llvm::IRBuilder<> builder(entry);

  auto *argsArray = invoker->getArg(0);

  std::vector<llvm::Value *> callArgs;
  unsigned idx = 0;
  for (auto &arg : TheFunction->args()) {
    auto *elemPtr =
        builder.CreateGEP(ptrTy, argsArray,
                          {llvm::ConstantInt::get(
                              llvm::Type::getInt64Ty(*state.TheContext), idx)});
    auto *loadedPtr = builder.CreateLoad(ptrTy, elemPtr);

    if (arg.getType()->isPointerTy()) {
      callArgs.push_back(loadedPtr);
    } else {
      auto *scalarVal = builder.CreateLoad(arg.getType(), loadedPtr);
      callArgs.push_back(scalarVal);
    }
    ++idx;
  }

  auto *callRes = builder.CreateCall(TheFunction, callArgs);
  if (TheFunction->getReturnType()->isVoidTy()) {
    builder.CreateRet(llvm::ConstantInt::get(retTy, 0));
  } else if (TheFunction->getReturnType()->isIntegerTy()) {
    builder.CreateRet(builder.CreateZExtOrTrunc(callRes, retTy));
  } else {
    builder.CreateRet(llvm::ConstantInt::get(retTy, 0));
  }
}

std::unique_ptr<FunctionAST>
StrideGenerator::generateStandaloneFunction(const std::string &funcName,
                                            ASTNode tree, ScopeStack &scope,
                                            StrideCompiler &state) {
  auto funcDecl = ASTQuery::findDeclarationByName(funcName, scope, tree);
  if (!funcDecl) {
    return nullptr;
  }
  return generateStandaloneFunction(funcDecl, tree, scope, state);
}
