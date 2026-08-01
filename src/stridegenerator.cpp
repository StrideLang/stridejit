#include <iostream>

#include "stride/stridejit/binaryexprast.hpp"
#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/functionast.hpp"
#include "stride/stridejit/listexprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// stride
#include "stride/codegen/codeanalysis.hpp"
#include "stride/utils/astquery.h"

// llvm
#include "llvm/IR/Module.h"
// #include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

using namespace strd;

void StrideGenerator::compile(ASTNode tree, ScopeStack &scope,
                              StrideCompiler &state) {
  GeneratedIRCode generatedIRCode = generateCodeForTree(tree, scope, state);

  std::vector<PrototypeArg> ExternalArgs; // Args to domain functions
  for (auto it = generatedIRCode.domainGeneratedCode.begin();
       it != generatedIRCode.domainGeneratedCode.end(); it++) {
    const std::string &domainName = it->first;
    // Find domain and insert inputs and output to tree.
    auto domainDecl = ASTQuery::findDeclarationByName(domainName, scope, tree);
    if (domainDecl) {
      std::cout << " Found domain declaration for " << domainName << std::endl;
      auto domainExternalInputNode = domainDecl->getPropertyValue("inputs");
      if (domainExternalInputNode) {
        for (const auto &externalInput :
             domainExternalInputNode->getChildren()) {
          if (externalInput->getNodeType() == AST::Declaration ||
              externalInput->getNodeType() == AST::BundleDeclaration) {
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
              externalOutput->getNodeType() == AST::BundleDeclaration) {
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

    auto proto = std::make_unique<PrototypeAST>(
        std::string(domainName + "_process"), std::vector<PrototypeArg>{},
        std::vector<PrototypeArg>{}, std::vector<PrototypeArg>{}, ExternalArgs,
        std::vector<PrototypeArg>{});
    auto processFunc =
        std::make_unique<FunctionAST>(std::move(proto), std::move(it->second));
    processFunc->callType = CallableType::DomainFunction;

    // Domain member variables (globals)
    // TODO set global linkage according to debug mode
    for (const auto &globalDecl : generatedIRCode.GlobalSignals) {
      state.createGlobal(globalDecl);
    }

    processFunc->codegen(state);

    auto initProto = std::make_unique<PrototypeAST>(
        std::string(domainName + "_init"), std::vector<PrototypeArg>{},
        std::vector<PrototypeArg>{}, std::vector<PrototypeArg>{}, ExternalArgs,
        std::vector<PrototypeArg>{});
    std::vector<std::unique_ptr<ExprAST>> resetBody;
    for (auto &extArg : ExternalArgs) {
      if (std::holds_alternative<int32_t>(extArg.defaultValue)) {
        auto varInit = std::make_unique<BinaryExprAST>(
            '=', std::make_unique<VariableExprAST>(extArg.name),
            std::make_unique<IntExprAST>(
                std::get<int32_t>(extArg.defaultValue)));
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
      } else {
        std::cerr << "ERROR unexpected type for default." << std::endl;
      }
    }

    // TODO Use information on generated code to initialize instead of going
    // through tree again.
    for (const auto &node : tree->getChildren()) {
      if (node->getNodeType() == AST::Declaration ||
          node->getNodeType() == AST::BundleDeclaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(node);
        auto domainNode = decl->getPropertyValue("domain");
        auto defaultValueNode = decl->getPropertyValue("default");
        if (defaultValueNode && domainNode &&
            domainNode->getNodeType() == AST::Block) {
          if (ASTQuery::getNodeName(domainNode) == domainName) {
            if (decl->getObjectType() == "switch") {
              if (defaultValueNode->getNodeType() == AST::Switch) {
                auto varInit = std::make_unique<BinaryExprAST>(
                    '=', std::make_unique<VariableExprAST>(decl->getName()),
                    std::make_unique<BoolExprAST>(
                        std::static_pointer_cast<ValueNode>(defaultValueNode)
                            ->getSwitchValue()));
                resetBody.push_back(std::move(varInit));
              } else {
                std::cerr << "Unsupported type for switch default: "
                          << defaultValueNode->toText() << std::endl;
              }
            } else if (decl->getObjectType() == "signal") {
              if (node->getNodeType() == AST::Declaration) {
                if (defaultValueNode->getNodeType() == AST::Real) {
                  auto varInit = std::make_unique<BinaryExprAST>(
                      '=', std::make_unique<VariableExprAST>(decl->getName()),
                      std::make_unique<RealExprAST>(
                          std::static_pointer_cast<ValueNode>(defaultValueNode)
                              ->getRealValue()));
                  resetBody.push_back(std::move(varInit));
                } else if (defaultValueNode->getNodeType() == AST::Int) {
                  auto varInit = std::make_unique<BinaryExprAST>(
                      '=', std::make_unique<VariableExprAST>(decl->getName()),
                      std::make_unique<IntExprAST>(
                          std::static_pointer_cast<ValueNode>(defaultValueNode)
                              ->getIntValue()));
                  resetBody.push_back(std::move(varInit));
                } else if (defaultValueNode->getNodeType() == AST::Switch) {
                  auto varInit = std::make_unique<BinaryExprAST>(
                      '=', std::make_unique<VariableExprAST>(decl->getName()),
                      std::make_unique<BoolExprAST>(
                          std::static_pointer_cast<ValueNode>(defaultValueNode)
                              ->getSwitchValue()));
                  resetBody.push_back(std::move(varInit));
                }
              } else if (node->getNodeType() == AST::BundleDeclaration) {
                std::vector<LangError> errors;
                int size = ASTQuery::getBlockDeclaredSize(
                    std::static_pointer_cast<DeclarationNode>(node), scope,
                    tree, &errors);
                std::vector<ASTNode> defaultValues;
                if (defaultValueNode->getNodeType() != AST::List) {
                  defaultValues.resize(size);
                  std::fill(defaultValues.begin(), defaultValues.end(),
                            defaultValueNode);
                } else {
                  defaultValues = defaultValueNode->getChildren();

                  if (defaultValues.size() != size) {
                    std::cerr << "ERROR default values size does not match "
                                 "declared size"
                              << std::endl;
                    continue;
                  }
                }
                size_t index = 0;
                for (const auto &defaultValue : defaultValues) {
                  if (defaultValue->getNodeType() == AST::Real) {
                    auto varInit = std::make_unique<BinaryExprAST>(
                        '=',
                        std::make_unique<VariableExprAST>(
                            decl->getName(),
                            std::vector<std::variant<size_t, std::string>>{
                                index}),
                        std::make_unique<RealExprAST>(
                            std::static_pointer_cast<ValueNode>(defaultValue)
                                ->getRealValue()));
                    resetBody.push_back(std::move(varInit));
                  } else if (defaultValue->getNodeType() == AST::Int) {
                    auto varInit = std::make_unique<BinaryExprAST>(
                        '=',
                        std::make_unique<VariableExprAST>(
                            decl->getName(),
                            std::vector<std::variant<size_t, std::string>>{
                                index}),
                        std::make_unique<IntExprAST>(
                            std::static_pointer_cast<ValueNode>(defaultValue)
                                ->getIntValue()));
                    resetBody.push_back(std::move(varInit));
                  } else if (defaultValue->getNodeType() == AST::Switch) {
                    auto varInit = std::make_unique<BinaryExprAST>(
                        '=',
                        std::make_unique<VariableExprAST>(
                            decl->getName(),
                            std::vector<std::variant<size_t, std::string>>{
                                index}),
                        std::make_unique<BoolExprAST>(
                            std::static_pointer_cast<ValueNode>(defaultValue)
                                ->getSwitchValue()));
                    resetBody.push_back(std::move(varInit));
                  }
                  index++;
                }
              }
            }
          }
        }
      }
    }

    auto initFunc = std::make_unique<FunctionAST>(std::move(initProto),
                                                  std::move(resetBody));
    initFunc->callType = CallableType::DomainFunction;
    // Add domain member variables (globals)
    // for (const auto &global : generatedIRCode.GlobalSignals) {
    //   initFunc->internalVariables.push_back(global);
    // }
    initFunc->codegen(state);

    state.domainArgs[domainName] = generatedIRCode.domainArgs;
  }
}

std::unique_ptr<ExprAST> StrideGenerator::createExpr(ASTNode node) {
  if (!node) {
    return nullptr;
  }
  if (node->getNodeType() == AST::Block) {
    std::unique_ptr<ExprAST> V = std::make_unique<VariableExprAST>(
        std::static_pointer_cast<BlockNode>(node)->getName());
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Bundle) {
    auto bundleNode = std::static_pointer_cast<BundleNode>(node);

    std::vector<std::variant<size_t, std::string>> indeces;
    for (const auto &idx : bundleNode->index()->getChildren()) {
      if (idx->getNodeType() == AST::Block) {
        indeces.push_back(std::static_pointer_cast<BlockNode>(idx)->getName());
      } else if (idx->getNodeType() == AST::Int) {
        indeces.push_back(
            (size_t)std::static_pointer_cast<ValueNode>(idx)->getIntValue());
      } else {
        std::cerr << "Unsupported node type for bundle index.";
      }
    }
    auto V = std::make_unique<VariableExprAST>(bundleNode->getName(), indeces);
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Real) {
    // TODO separate float types
    auto V = std::make_unique<RealExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getRealValue());
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Int) {
    // TODO separate int types
    auto V = std::make_unique<IntExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getIntValue());
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Switch) {
    // TODO separate int types
    auto V = std::make_unique<BoolExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getSwitchValue());
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->isUnary()) {
      assert(0 == 1);

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

      auto V = std::make_unique<BinaryExprAST>(op, std::move(l), std::move(r));
      setTypeCastMetadata(node, V.get());
      return V;
    }
  } else if (node->getNodeType() == AST::List) {
    auto list = std::make_unique<ListExprAST>(node->getChildren());
    return list;
  } else if (node->getNodeType() == AST::String) {
    auto list = std::make_unique<ListExprAST>(node->getChildren());
    return list;
  } else if (node->getNodeType() == AST::PortProperty) {
    auto pp = std::static_pointer_cast<PortPropertyNode>(node);
    auto V =
        std::make_unique<PortPropertyAST>(pp->getName(), pp->getPortName());
    setTypeCastMetadata(node, V.get());
    return V;
  }
  std::cerr << " Node type not supported " << std::endl;
  return nullptr;
}

StrideGenerator::GeneratedIRCode
StrideGenerator::generateCodeForTree(ASTNode tree, ScopeStack &scope,
                                     StrideCompiler &state) {

  state.m_intanceTree = CodeAnalysis::getStateStructInformation({}, tree);
  StrideGenerator::GeneratedIRCode generatedIRCode;
  for (const auto &node : tree->getChildren()) {
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
               node->getNodeType() == AST::BundleDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getObjectType() == "signal" ||
          decl->getObjectType() == "constant") {
        auto *type = state.getLLVMType(decl);
        // TODO identify globals by domain better and ensure we are catching all
        // globals in the domain.
        generatedIRCode.GlobalSignals.push_back(decl);
        // TODO remove domain args?
        if (type->isDoubleTy()) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::DOUBLE});
        } else if (type->isFloatTy()) {
          assert(0 == 1); // Not yet supported
        } else if (type->isIntegerTy(1)) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::BOOL});
        } else if (type->isIntegerTy(32)) {
          generatedIRCode.domainArgs.push_back(
              {decl->getName(), DataType::INT32});
          //        } else if (type->isIntegerTy(64)) {
          //          domainArgs.push_back({decl->getName(), DataType::INT64});
        } else {
          assert(0 == 1);
        }
      } else if (decl->getObjectType() == "switch") {
        generatedIRCode.GlobalSignals.push_back(decl);
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
    std::shared_ptr<DeclarationNode> blockDecl,
    std::unique_ptr<ExprAST> currentExpr,
    StrideGenerator::GeneratedCode &generated, std::string domainName,
    const ScopeStack &scope, ASTNode tree, StrideCompiler &state) {
  auto funcDecl = ASTQuery::findDeclarationByName(
      ASTQuery::getNodeName(prevFuncCall), scope, tree);
  if (!funcDecl) {
    std::cerr << "ERROR: no function declaration for "
              << ASTQuery::getNodeName(prevFuncCall) << std::endl;
    return false;
  }
  auto retType = state.getLLVMType(funcDecl);
  if (auto callexpr = dynamic_cast<CallExprAST *>(
          generated[domainName].expr.back().get())) {

    std::vector<llvm::Type *> argTypes;
    for (auto it = callexpr->OutArgsDataType.begin();
         it != callexpr->OutArgsDataType.end(); it++) {
      argTypes.push_back(*it);
    }
    for (auto it = callexpr->InArgsDataType.begin();
         it != callexpr->InArgsDataType.end(); it++) {
      argTypes.push_back(*it);
    }

    // TODO do we also test that the function is not declared in Stride
    // code? Or is this check enough?
    auto externFunc =
        state.getExternalFunction(prevFuncCall->getName(), retType, argTypes);
    if (externFunc) {
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
      if (outputTypeNode && outputTypeNode->getNodeType() == AST::Block) {
        auto outputType = std::static_pointer_cast<BlockNode>(outputTypeNode);
        if (state.typesMap.find(outputType->getName()) !=
            state.typesMap.end()) {
          outType = state.typesMap[outputType->getName()];
        }
      }
    }

    if (outType && retType != outType) {
      if (retType->isIntegerTy() && outType->isDoubleTy()) {
        //              block = state.Builder->CreateFPToSI(
        //                  block,
        //                  llvm::Type::getDoubleTy(*state.TheContext));
      }
    }

    if (blockDecl->getObjectType() == "trigger") {
      std::vector<std::unique_ptr<ExprAST>> Expressions;
      auto triggerResetsNode = blockDecl->getCompilerProperty("triggerResets");
      if (triggerResetsNode) {
        for (const auto &node : triggerResetsNode->getChildren()) {
          if (node->getNodeType() == AST::Declaration ||
              node->getNodeType() == AST::BundleDeclaration) {
            auto resetNodeDecl =
                std::static_pointer_cast<DeclarationNode>(node);
            // TODO is this code duplicated?
            if (resetNodeDecl->getObjectType() == "signal") {
              auto typeNode = resetNodeDecl->getPropertyValue("type");
              if (typeNode && typeNode->getNodeType() == AST::Block) {
                auto typeName =
                    std::static_pointer_cast<BlockNode>(typeNode)->getName();
                auto defaultNode = resetNodeDecl->getPropertyValue("default");
                if (!defaultNode) {
                  continue;
                }
                if (typeName == "_RealType" &&
                    defaultNode->getNodeType() == AST::Real) {
                  double defaultValue =
                      std::static_pointer_cast<ValueNode>(defaultNode)
                          ->getRealValue();
                  auto varReal = std::make_unique<BinaryExprAST>(
                      '=',
                      std::make_unique<VariableExprAST>(
                          resetNodeDecl->getName()),
                      std::make_unique<RealExprAST>(defaultValue));
                  Expressions.push_back(std::move(varReal));
                } else if (typeName == "_IntType" &&
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

            } else if (resetNodeDecl->getObjectType() == "switch") {

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

StrideGenerator::GeneratedCode
StrideGenerator::createStreamCode(std::shared_ptr<StreamNode> stream,
                                  ASTNode tree, ScopeStack &scope,
                                  StrideCompiler &state) {
  StrideGenerator::GeneratedCode generated;
  ASTNode prev = nullptr;
  StreamNodeIterator streamIt(stream);
  ASTNode current;
  ASTNode next = streamIt.next();

  while (next) {
    prev = current;
    current = next;
    next = streamIt.next();
    auto domainName = CodeAnalysis::getNodeDomainName(current, scope, tree);
    if (domainName.size() == 0) {
      auto streamPtr = stream;
      // FIXME resolve downstream domains correctly
      // FIXME this should be done in the resolver, not here
      auto connection =
          CodeAnalysis::resolveConnectionBlock(next, scope, tree, true);
      if (connection) {
        domainName = CodeAnalysis::getNodeDomainName(connection, scope, tree);
        if (domainName.size() == 0) {
          domainName = "RootDomain";
        }
      } else {
        domainName = "RootDomain";
      }
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
    } else if (current->getNodeType() == AST::Block ||
               current->getNodeType() == AST::Bundle) {
      // -------------------------------------------------------------
      // Block/Bundle
      auto block = createExpr(current);
      auto decl = ASTQuery::findDeclarationByName(
          ASTQuery::getNodeName(current), scope, tree);
      if (current->getNodeType() == AST::Bundle) {
        auto indexNode = std::static_pointer_cast<BundleNode>(current)->index();
        if (indexNode->getNodeType() == AST::List) {
          for (const auto &elem : indexNode->getChildren()) {
            if (elem->getNodeType() == AST::Block) {
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
        std::cerr << "No declaration for: " << AST::toText(current) << " in "
                  << AST::toText(stream) << " in " << stream->getFilename()
                  << ":" << stream->getLine() << std::endl;
        return generated;
      }
      // TODO accumulate read and write variables for switch, expressions,
      // lists and function arguments if on root namespace
      if (current == stream->getLeft()) {
        generated[domainName].readVariables.push_back(current);
      } else {
        generated[domainName].writeVariables.push_back(current);
      }
      if (prev && prev->getNodeType() == AST::Function) {
        // TODO do we need to process again here?
        // Connect to previous function
        if (!processPreviousFunction(
                std::static_pointer_cast<FunctionNode>(prev), decl,
                std::move(block), generated, domainName, scope, tree, state)) {
          continue;
        }
      } else if (generated[domainName].expr.size() > 0) {
        // Connect to previous stream node
        generated[domainName].expr.back() = std::make_unique<BinaryExprAST>(
            '=', std::move(block),
            std::move(generated[domainName].expr.back()));
      } else {
        generated[domainName].expr.push_back(std::move(block));
      }
    } else if (current->getNodeType() == AST::Function) {
      // -------------------------------------------------------------
      // Function
      auto func = std::static_pointer_cast<FunctionNode>(current);
      auto decls = ASTQuery::findAllDeclarations(func->getName(), scope, tree);
      auto funcDecl = CodeAnalysis::matchDefinitionToTypes(decls, func);

      if (!funcDecl) {
        std::cerr << "ERROR can't find/match declaration for "
                  << func->getName() << std::endl;
        continue;
      }

      state.pushName(funcDecl->getName());
      // Collect arguments for function call ------------------------------
      std::vector<std::unique_ptr<ExprAST>> mainInArgs;
      std::vector<llvm::Type *> mainInArgTypes;
      std::vector<std::unique_ptr<ExprAST>> mainOutArgs;
      std::vector<llvm::Type *> mainOutArgTypes;
      std::vector<std::unique_ptr<ExprAST>> internalArgs;
      std::vector<llvm::Type *> internalArgTypes;
      std::vector<std::unique_ptr<ExprAST>> ExternalArgs;

      // Collect input arguments
      // TODO collection function port arguments

      auto *domainTypeTree = state.m_intanceTree.getDomainRootTree(domainName);
      auto *typeTree = domainTypeTree->find(func);

      if (typeTree) {
        auto input = typeTree->instance->getCompilerProperty("mainInput");
        if (input) {
          if (auto prevFunc = std::dynamic_pointer_cast<FunctionNode>(input)) {
            mainInArgs.emplace_back(
                std::move(generated[domainName].expr.back()));
            generated[domainName].expr.pop_back();
            // FIXME do automatic type casting int ->float
            mainInArgTypes.push_back(
                llvm::Type::getDoubleTy(*state.TheContext));
          } else if (auto prevList =
                         std::dynamic_pointer_cast<ListNode>(input)) {
            auto *v = dynamic_cast<ListExprAST *>(
                generated[domainName].expr.back().get());
            auto listNodes = prevList->getChildren();
            auto nodeIt = listNodes.begin();
            if (v->getType() == ListExprAST::Type::MUTABLE_CONSISTENT) {
              for (auto elemExpr = v->elements().begin();
                   elemExpr != v->elements().end(); elemExpr++) {
                mainInArgs.emplace_back(std::move(*elemExpr));
                auto elemDecl = ASTQuery::findDeclarationByName(
                    ASTQuery::getNodeName(*nodeIt), scope, tree);
                // auto dataType =
                //     CodeAnalysis::getOutputDataTypes(*nodeIt, scope, tree);

                if (elemDecl) {
                  mainInArgTypes.push_back(state.getLLVMTypeForCodegenBlock(
                      elemDecl, funcDecl, func));
                } else if ((*nodeIt)->getNodeType() == AST::Int) {
                  // TODO get types from framework
                  mainInArgTypes.push_back(
                      llvm::Type::getInt32Ty(*state.TheContext));
                } else if ((*nodeIt)->getNodeType() == AST::Real) {
                  mainInArgTypes.push_back(
                      llvm::Type::getDoubleTy(*state.TheContext));
                } else if ((*nodeIt)->getNodeType() == AST::PortProperty) {
                  auto pp = std::static_pointer_cast<PortPropertyNode>(*nodeIt);
                  if (pp->getPortName() == "size") {
                    mainInArgTypes.push_back(
                        llvm::Type::getInt32Ty(*state.TheContext));
                  } else if (pp->getPortName() == "rate") {
                    mainInArgTypes.push_back(
                        llvm::Type::getDoubleTy(*state.TheContext));
                  } else {
                    assert(0 == 1);
                  }
                } else {
                  // Fallback. We shouldn't get here when things are fully
                  // implemented
                  mainInArgTypes.push_back(state.getLLVMTypeForCodegenBlock(
                      elemDecl, funcDecl, func));
                  std::cerr << __FILE__ << ":" << __LINE__
                            << " Unsupported type for: " << (*nodeIt)->toText()
                            << std::endl;
                }

                nodeIt++;
              }
            } else if (v->getType() ==
                       ListExprAST::Type::IMMUTABLE_CONSISTENT) {
              mainInArgs.emplace_back(
                  std::move(generated[domainName].expr.back()));
              auto elemDecl = ASTQuery::findDeclarationByName(
                  ASTQuery::getNodeName(*nodeIt), scope, tree);
              mainInArgTypes.push_back(
                  state.getLLVMTypeForCodegenBlock(elemDecl, funcDecl, func));
            } else {
              // Not supported
              std::cerr << "ERROR: List type not supported" << std::endl;
              assert(0 == 1);
            }
            generated[domainName].expr.pop_back();

          } else {
            if (generated[domainName].expr.size() > 0) {
              mainInArgs.emplace_back(
                  std::move(generated[domainName].expr.back()));
              generated[domainName].expr.pop_back();
              auto prevTypes =
                  CodeAnalysis::getOutputDataTypes(input, scope, tree);
              for (const auto &prevType : prevTypes) {

                auto typeName = ASTQuery::getNodeName(prevType);
                if (state.typesMap.find(typeName) != state.typesMap.end()) {
                  mainInArgTypes.push_back(state.typesMap[typeName]);
                } else {
                  std::cerr << "ERROR: Unsupported type: " << typeName
                            << std::endl;
                }
              }
            } else {
              std::cerr << "ERROR: No code generated for domain: " << domainName
                        << std::endl;
            }
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

      if (funcDecl->getObjectType() == "platformModule") {
        externFunc = state.getExternalFunction(
            func->getName(), state.getLLVMType(nextDecl), mainInArgTypes);
      }

      // Create function call expr
      if (externFunc) {
        if (externFunc->name.size() > 0 && externFunc->name[0] == '@') {
          if (!state.TheModule->getFunction(externFunc->name)) {
            auto *newFunc = llvm::Function::Create(
                externFunc->llvmFunctionType, llvm::Function::ExternalLinkage,
                externFunc->name.substr(1), *state.TheModule);
            generated[domainName].externalFunctions.push_back(newFunc);
          }
          auto newExternCall = std::make_unique<CallExprAST>(
              externFunc->name.substr(1), std::move(mainOutArgs),
              std::move(mainInArgs), std::vector<std::unique_ptr<ExprAST>>{},
              std::vector<std::unique_ptr<ExprAST>>{},
              std::vector<std::unique_ptr<ExprAST>>{},
              std::move(mainOutArgTypes), std::move(mainInArgTypes),
              state.getName());
          newExternCall->callType = CallableType::External;
          generated[domainName].expr.push_back(std::move(newExternCall));
          std::cout << "Using external function:" << externFunc->name
                    << std::endl;
        } else {
          auto newCall = std::make_unique<LLVMCommandAST>(
              externFunc->name, std::move(mainOutArgs), std::move(mainInArgs),
              std::vector<std::unique_ptr<ExprAST>>{},
              std::vector<std::unique_ptr<ExprAST>>{});

          generated[domainName].expr.push_back(std::move(newCall));
        }
      } else { // is not external function

        auto nextExpr = createExpr(next);
        if (nextExpr) {
          auto outputNode =
              typeTree->instance->getCompilerProperty("mainOutput");
          mainOutArgs.emplace_back(std::move(nextExpr));
          auto blockName = ASTQuery::getNodeName(outputNode);
          auto mainOutputDecl = ASTQuery::findDeclarationByName(
              blockName,
              {std::pair<ASTNode, std::vector<ASTNode>>(
                  nullptr,
                  funcDecl->getPropertyValue("blocks")->getChildren())},
              nullptr);

          mainOutArgTypes.push_back(state.getLLVMType(mainOutputDecl));
        }

        auto funcDecl =
            ASTQuery::findDeclarationByName(func->getName(), scope, tree);
        if (funcDecl) {
          // --------------------- Function declaration -----------------------
          // Can we unify this parameter definition wiht argument preparation
          // above?
          std::unique_ptr<FunctionAST> newFuncDecl =
              createFunctionDeclaration(funcDecl, func, tree, &scope, state);
          if (newFuncDecl) {
            for (const auto &arg : newFuncDecl->getProto().getExternalArgs()) {
              ExternalArgs.push_back(
                  std::make_unique<VariableExprAST>(arg.name));
            }
            for (const auto &arg : newFuncDecl->getProto().getInternalArgs()) {
              internalArgs.push_back(
                  std::make_unique<VariableExprAST>(arg.name));
            }
          }
          std::vector<std::unique_ptr<ExprAST>> PortPropArgs;
          if (funcDecl) {
            auto usedPortProps = CodeAnalysis::getUsedPortProperties(funcDecl);
            auto innerScope = scope;
            std::vector<ASTNode> blocks;
            auto blocksNode = funcDecl->getPropertyValue("blocks");
            if (blocksNode) {
              blocks = blocksNode->getChildren();
            }
            innerScope.push_back({funcDecl, blocks});
            for (const auto &ppNode : usedPortProps) {
              if (ppNode->getPortName() == "size") {
                auto size = CodeAnalysis::evaluateSizePortProperty(
                    ppNode->getName(), innerScope, funcDecl, func, tree);
                // TODO determine integer type for size from platform
                // definition.
                PortPropArgs.push_back(std::make_unique<IntExprAST>(size, 32));
              } else if (ppNode->getPortName() == "rate") {
                auto rate = CodeAnalysis::evaluateRatePortProperty(
                    ppNode->getName(), innerScope, funcDecl, func, tree);
                PortPropArgs.push_back(std::make_unique<RealExprAST>(rate));
              }
            }
            if (funcDecl->getObjectType() == "module") {
              std::cout << "Module instance:" << std::endl;
              for (const auto &blockNode : blocks) {
                if (blockNode->getNodeType() == AST::Declaration ||
                    blockNode->getNodeType() == AST::BundleDeclaration) {
                  auto blockDecl =
                      std::static_pointer_cast<DeclarationNode>(blockNode);

                  if (!ASTQuery::isPortBlock(blockDecl, funcDecl, scope,
                                             tree) &&
                      blockDecl->getCompilerProperty("persistent")) {
                    // Only modules create external instances for their internal
                    // values to have persistence.
                    // TODO modify to bundle in a state struct as globals will
                    // be less efficient
                    state.createGlobal(blockDecl);
                  }
                } else {
                  std::cerr << "Expected declaration, got: "
                            << AST::toText(blockNode);
                }
              }
            }
          }
          // -----------------------------------------------------------
          // Function call expr
          auto callexpr = std::make_unique<CallExprAST>(
              std::string(func->getName()), std::move(mainOutArgs),
              std::move(mainInArgs), std::move(internalArgs),
              std::move(ExternalArgs), std::move(PortPropArgs),
              std::move(mainOutArgTypes), std::move(mainInArgTypes),
              state.getName());
          if (newFuncDecl) {
            callexpr->callType = newFuncDecl->callType;
            generated[domainName].functions.push_back(std::move(newFuncDecl));
          } else {
            // Function already declared
            if (funcDecl) {
              if (funcDecl->getObjectType() == "module") {
                callexpr->callType = CallableType::Module;
              } else if (funcDecl->getObjectType() == "reaction") {
                callexpr->callType = CallableType::Reaction;
              } else if (funcDecl->getObjectType() == "loop") {
                callexpr->callType = CallableType::Loop;
              } else {
                std::cout << " ERROR: Can't set callable type" << std::endl;
              }
            } else {
              std::cout << " ERROR: Can't set callable type" << std::endl;
            }
          }
          generated[domainName].expr.push_back(std::move(callexpr));
        } else {
          std::cerr << "ERROR: Could not find function declaration: "
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
      generated[domainName].expr.push_back(createExpr(current));
    } else if (current->getNodeType() == AST::PortProperty) {
      generated[domainName].expr.push_back(createExpr(current));
    } else {
      std::cerr << "ERROR: Unsupported type" << std::endl;
    }
  };
  return generated;
}

std::unique_ptr<FunctionAST> StrideGenerator::createFunctionDeclaration(
    std::shared_ptr<DeclarationNode> funcDecl,
    std::shared_ptr<FunctionNode> funcInstance, ASTNode tree, ScopeStack *scope,
    StrideCompiler &state) {

  // if (!ASTQuery::isCallable(funcDecl, scope, tree)) {
  //   std::cerr << "ERROR: Can't create function for: " << funcDecl->toText()
  //             << std::endl
  //             << "is not _Callable." << std::endl;
  //   return nullptr;
  // }
  std::string funcName = funcDecl->getName();

  std::vector<PrototypeArg> InParams;
  std::vector<PrototypeArg> OutParams;
  std::vector<PrototypeArg> InternalParams; // Internal only, on stack
  std::vector<PrototypeArg>
      InternalPersistentParams; // Internal but passed to function arguments
  std::vector<PrototypeArg> ExternalParams;
  std::vector<PrototypeArg> UsedPortProperties;

  auto functionScope = *scope;
  if (functionScope.size() == 0) {
    functionScope.push_back({nullptr, {}});
  }
  if (funcDecl->getObjectType() == "reaction" ||
      funcDecl->getObjectType() == "loop") {
    // TODO grab correct scope, currently passing all.
    for (const auto &scopeEntry : *scope) {
      functionScope.back().second.insert(functionScope.back().second.begin(),
                                         scopeEntry.second.begin(),
                                         scopeEntry.second.end());
    }
  } else {
    functionScope.push_back({funcDecl, {}});
  }
  auto blocks = funcDecl->getPropertyValue("blocks");
  if (blocks) {
    for (const auto &blockDecl : blocks->getChildren()) {
      functionScope.back().second.push_back(blockDecl);
    }
  }

  auto portsNode = funcDecl->getPropertyValue("ports");
  std::string portBlockName;

  // InParams and OutParams are capture directly from port blocks.
  // for (const auto &node : portsNode->getChildren()) {
  //   if (node->getNodeType() == AST::Declaration) {
  //     auto decl = std::static_pointer_cast<DeclarationNode>(node);
  //     if (decl->getObjectType() == "mainInputPort") {
  //       auto blockNode = decl->getPropertyValue("block");
  //       if (blockNode && blockNode->getNodeType() == AST::Block) {
  //         portBlockName =
  //             std::static_pointer_cast<BlockNode>(blockNode)->getName();
  //       }
  //       auto type = llvm::Type::getDoubleTy(*state.TheContext);
  //       auto blockDecl =
  //           ASTQuery::findDeclarationByName(portBlockName, functionScope,
  //           tree);
  //       if (blockDecl) {
  //         type = state.getLLVMTypeForCodegenBlock(blockDecl, funcDecl,
  //                                                 funcInstance);
  //       } else {
  //         std::cerr << "ERROR: Block declaration not found for: "
  //                   << portBlockName << std::endl;
  //       }
  //       InParams.push_back(PrototypeArg{portBlockName, type});
  //     } else if (decl->getObjectType() == "mainOutputPort") {
  //       auto blockNode = decl->getPropertyValue("block");
  //       if (blockNode && blockNode->getNodeType() == AST::Block) {
  //         portBlockName =
  //             std::static_pointer_cast<BlockNode>(blockNode)->getName();
  //       }
  //       auto type = llvm::Type::getDoubleTy(*state.TheContext);
  //       auto blockDecl =
  //           ASTQuery::findDeclarationByName(portBlockName, functionScope,
  //           tree);
  //       if (blockDecl) {
  //         type = state.getLLVMTypeForCodegenBlock(blockDecl, funcDecl,
  //                                                 funcInstance);
  //       } else {
  //         std::cerr << "ERROR: Block declaration not found for: "
  //                   << portBlockName << std::endl;
  //       }
  //       OutParams.push_back(PrototypeArg{portBlockName, type});
  //     } else if (decl->getObjectType() == "propertyInputPort") {
  //       auto blockNode = decl->getPropertyValue("block");
  //       if (blockNode && blockNode->getNodeType() == AST::Block) {
  //         portBlockName =
  //             std::static_pointer_cast<BlockNode>(blockNode)->getName();
  //       }
  //       auto type = llvm::Type::getDoubleTy(*state.TheContext);
  //       auto blockDecl =
  //           ASTQuery::findDeclarationByName(portBlockName, functionScope,
  //           tree);
  //       if (blockDecl) {
  //         type = state.getLLVMTypeForCodegenBlock(blockDecl, funcDecl,
  //                                                 funcInstance);
  //       } else {
  //         std::cerr << "ERROR: Block declaration not found for: "
  //                   << portBlockName << std::endl;
  //       }
  //       InParams.push_back(PrototypeArg{portBlockName, type});
  //     } else if (decl->getObjectType() == "propertyOutputPort") {
  //       auto blockNode = decl->getPropertyValue("block");
  //       if (blockNode && blockNode->getNodeType() == AST::Block) {
  //         portBlockName =
  //             std::static_pointer_cast<BlockNode>(blockNode)->getName();
  //       }
  //       auto type = llvm::Type::getDoubleTy(*state.TheContext);
  //       auto blockDecl =
  //           ASTQuery::findDeclarationByName(portBlockName, functionScope,
  //           tree);
  //       if (blockDecl) {
  //         type = state.getLLVMTypeForCodegenBlock(blockDecl, funcDecl,
  //                                                 funcInstance);
  //       } else {
  //         std::cerr << "ERROR: Block declaration not found for: "
  //                   << portBlockName << std::endl;
  //       }
  //       OutParams.push_back(PrototypeArg{portBlockName, type});
  //     }
  //   }
  // }

  // Create args for internal blocks
  // InternalParams are all declared blocks that are not port blocks.
  // for (const auto &node : blocks->getChildren()) {
  //   if (node->getNodeType() == AST::Declaration) {
  //     auto decl = std::static_pointer_cast<DeclarationNode>(node);
  //     auto name = decl->getName();
  //     bool isInternal = true;
  //     for (const auto &arg : InParams) {
  //       if (arg.name == name) {
  //         isInternal = false;
  //         break;
  //       }
  //     }
  //     for (const auto &arg : OutParams) {
  //       if (arg.name == name) {
  //         isInternal = false;
  //         break;
  //       }
  //     }
  //     if (isInternal) {
  //       // TODO decide if block should be in the function parameters or
  //       // declared internally according to whether it is read before it is
  //       // written to and whether this change is more performant. For now
  //       // al internal arguments are put in the function prototype.
  //       if (funcDecl->getObjectType() == "module") {
  //         // Only modules keep state so they will have their internal
  //         variables
  //         // stored externally.
  //         auto type =
  //             state.getLLVMTypeForCodegenBlock(decl, funcDecl, funcInstance);
  //         InternalParams.push_back(PrototypeArg{name, type});
  //       }
  //     }
  //   }
  // }

  // Used internal and external variables are variables within this scope's
  // streams. Internal if they are in the scope, external if they are in the
  // tree
  std::vector<std::shared_ptr<DeclarationNode>> usedInternalVariables;
  std::vector<std::shared_ptr<DeclarationNode>> usedExternalVariables;
  // These externalFunctions appear to serve no purpose.
  // std::vector<llvm::Function *> externalFunctions;

  std::vector<std::unique_ptr<ExprAST>> collected;
  // Generate function code by going through streams.
  auto streams = funcDecl->getPropertyValue("streams");
  if (streams) {
    for (const auto &streamNode : streams->getChildren()) {
      if (streamNode->getNodeType() == AST::Stream) {
        auto stream = std::static_pointer_cast<StreamNode>(streamNode);
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
          // externalFunctions.insert(externalFunctions.end(),
          //                          domainCode.second.externalFunctions.begin(),
          //                          domainCode.second.externalFunctions.end());

          // auto addExternalVar = [&](ASTNode var) {
          //   std::string blockName = ASTQuery::getNodeName(var);
          //   if (blockName.size() > 0 &&
          //       std::find_if(InParams.begin(), InParams.end(),
          //                    [&blockName](const PrototypeArg &x) {
          //                      return x.name == blockName;
          //                    }) == InParams.end() &&
          //       std::find_if(OutParams.begin(), OutParams.end(),
          //                    [&blockName](const PrototypeArg &x) {
          //                      return x.name == blockName;
          //                    }) == OutParams.end()) {
          //     auto decl = ASTQuery::findDeclarationByName(
          //         std::static_pointer_cast<BlockNode>(var)->getName(), {},
          //         blocks);
          //     if (decl) {
          //       // TODO there will be a mismatch if there are unused
          //       variables.
          //       // We need to consolidate InternalArgs and
          //       // usedInternalVariables
          //       // usedInternalVariables.push_back(decl);
          //     } else if (funcDecl->getObjectType() == "reaction" ||
          //                funcDecl->getObjectType() == "loop") {
          //       decl = ASTQuery::findDeclarationByName(
          //           std::static_pointer_cast<BlockNode>(var)->getName(),
          //           *scope, tree);
          //       if (decl) {
          //         usedExternalVariables.push_back(decl);
          //       }
          //     }
          //   }
          // };
          // for (const auto &readVar : domainCode.second.readVariables) {
          //   addExternalVar(readVar);
          // }
          // for (const auto &writeVar : domainCode.second.writeVariables) {
          //   addExternalVar(writeVar);
          // }
        }
      } else {
      }
    }
  }
  // for (const auto &decl : usedExternalVariables) {
  //   ExternalParams.push_back(
  //       PrototypeArg{decl->getName(), state.getLLVMType(decl)});
  // }

  auto *nodeTree = state.m_intanceTree.find(funcInstance);

  if (nodeTree) {
    for (const auto &var : nodeTree->input) {
      auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
      auto type =
          state.getLLVMTypeForCodegenBlock(decl, funcDecl, funcInstance);
      InParams.push_back(PrototypeArg{ASTQuery::getNodeName(var.first), type});
    }

    for (const auto &var : nodeTree->output) {
      auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
      auto type =
          state.getLLVMTypeForCodegenBlock(decl, funcDecl, funcInstance);
      OutParams.push_back(PrototypeArg{ASTQuery::getNodeName(var.first), type});
    }

    for (const auto &var : nodeTree->internal) {
      auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
      auto type =
          state.getLLVMTypeForCodegenBlock(decl, funcDecl, funcInstance);
      if (decl->getPropertyValue("persistent")) {
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
          state.getLLVMTypeForCodegenBlock(decl, funcDecl, funcInstance);
      ExternalParams.push_back(
          PrototypeArg{ASTQuery::getNodeName(var.first), type});
    }

    for (const auto &var : nodeTree->external) {
      if (var.first->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
        auto type =
            state.getLLVMTypeForCodegenBlock(decl, funcDecl, funcInstance);
        ExternalParams.push_back(
            PrototypeArg{ASTQuery::getNodeName(var.first), type});
      } else if (var.first->getNodeType() == AST::BundleDeclaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
        auto type =
            state.getLLVMTypeForCodegenBlock(decl, funcDecl, funcInstance);
        ExternalParams.push_back(
            PrototypeArg{ASTQuery::getNodeName(var.first), type});
        assert(0 == 1);
      } else if (var.first->getNodeType() == AST::PortProperty) {
        // Port properties are added in a separate pass
        // TODO move this pass here?
      } else {
        std::cerr << __FILE__ << ":" << __LINE__
                  << " Error unexpected type: " << var.first->toText()
                  << std::endl;
        ;
      }
    }

  } else {
    std::cerr << __FILE__ << ":" << __LINE__ << " ERROR: type tree not found"
              << std::endl;
  }

  auto usedPortPropertiesNodes = CodeAnalysis::getUsedPortProperties(funcDecl);
  for (const auto &pp : usedPortPropertiesNodes) {
    // FIXME validate unique name
    std::string name = pp->getName() + "_" + pp->getPortName();
    // TODO determine the type of integer for port properties from platform
    // defintion.
    if (pp->getPortName() == "size") {
      UsedPortProperties.push_back(
          PrototypeArg{name, llvm::Type::getInt32Ty(*state.TheContext)});
    } else if (pp->getPortName() == "rate") {
      UsedPortProperties.push_back(
          PrototypeArg{name, llvm::Type::getDoubleTy(*state.TheContext)});
    }
  }
  llvm::Function *TheFunction = state.getFunctionInModule(funcName);
  if (TheFunction) {
    // TODO check if current function is the same as existing function
    return nullptr;
  }
  auto proto = std::make_unique<PrototypeAST>(
      funcName, OutParams, InParams, InternalPersistentParams, ExternalParams,
      UsedPortProperties);

  // llvm::Function *TheFunction = state.getFunctionInModule(P.getName());
  auto newfunc =
      std::make_unique<FunctionAST>(std::move(proto), std::move(collected));
  newfunc->internalVariables = usedInternalVariables;

  if (funcDecl->getObjectType() == "module") {
    newfunc->callType = CallableType::Module;
  } else if (funcDecl->getObjectType() == "reaction") {
    newfunc->callType = CallableType::Reaction;
  } else if (funcDecl->getObjectType() == "loop") {
    newfunc->callType = CallableType::Loop;
    auto terminateWhenNode = funcDecl->getPropertyValue("terminateWhen");
    if (terminateWhenNode) {
      if (terminateWhenNode->getNodeType() == AST::Block) {
        newfunc->terminateWhenName =
            std::static_pointer_cast<BlockNode>(terminateWhenNode)->getName();
      }
    }
  } else if (funcDecl->getObjectType() == "platformModule") {
    newfunc->callType = CallableType::External;
  } else {
    std::cerr << "Callable type unsuported" << std::endl;
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
    //            std::cout << "Loaded: " << decl->getName() <<
    //            std::endl;
    frameworkScope.push_back(decl);
    for (const auto &input : inputList->getChildren()) {
      if (input->getNodeType() == AST::Block) {
        auto inputBlock = std::static_pointer_cast<BlockNode>(input);
        auto inputType = inputBlock->getName();
        if (state.typesMap.find(inputType) != state.typesMap.end()) {
          parameters.push_back(state.typesMap[inputType]);
        } else {
          std::cerr << "Input Type not mapped: " << inputType << std::endl;
        }
      }
    }
    assert(outputList->getChildren().size() < 2);
    if (outputList->getChildren().size() != 0) {
      auto outputBlock = outputList->getChildren()[0];
      if (outputBlock->getNodeType() == AST::Block) {
        auto outputType =
            std::static_pointer_cast<BlockNode>(outputBlock)->getName();
        if (state.typesMap.find(outputType) != state.typesMap.end()) {
          retType = state.typesMap[outputType];
        } else {
          std::cerr << " Output Type not mapped: " << outputType << std::endl;
        }
      }
    }
    {
      auto name = std::static_pointer_cast<ValueNode>(functionNameNode)
                      ->getStringValue();
      llvm::FunctionType *FT =
          llvm::FunctionType::get(retType, parameters, false);
      state.functionMap[decl->getName()].push_back(ExternalFunction{name, FT});
      std::string atName;
      auto atNode = decl->getCompilerProperty("_at");
      if (atNode && atNode->getNodeType() == AST::String) {
        atName =
            "@" + std::static_pointer_cast<ValueNode>(atNode)->getStringValue();
      }
      std::cout << "Loaded platform module: " << decl->getName() << atName
                << std::endl;
    }
  }
}

void StrideGenerator::setTypeCastMetadata(ASTNode node, ExprAST *V) {
  if (auto typecastNode = node->getCompilerProperty("typecast")) {
    if (typecastNode->getNodeType() == AST::String) {
      V->typecast =
          std::static_pointer_cast<ValueNode>(typecastNode)->getStringValue();
    }
  }
}

DefaultVariant
StrideGenerator::getDefaultValue(std::shared_ptr<DeclarationNode> decl,
                                 StrideCompiler &state) {
  DefaultVariant defaultValue;
  auto *type = state.getLLVMType(decl);
  if (decl->getObjectType() == "signal") {
    auto defaultNode = decl->getPropertyValue("default");
    if (defaultNode && (defaultNode->getNodeType() == AST::Int ||
                        defaultNode->getNodeType() == AST::Real)) {
      if (type->isDoubleTy()) {
        defaultValue =
            std::static_pointer_cast<ValueNode>(defaultNode)->toReal();
      } else if (type->isIntegerTy(32)) {
        if (defaultNode->getNodeType() == AST::Int) {
          auto intValue =
              std::static_pointer_cast<ValueNode>(defaultNode)->getIntValue();
          assert(intValue <= INT32_MAX);
          assert(intValue >= INT32_MIN);
          defaultValue = (int32_t)intValue;
        } else if (defaultNode->getNodeType() == AST::Real) {
          auto doubleValue =
              std::static_pointer_cast<ValueNode>(defaultNode)->getRealValue();
          assert(doubleValue <= INT32_MAX);
          assert(doubleValue >= INT32_MIN);
          defaultValue = (int32_t)doubleValue;
        }
      }
    }
  } else if (decl->getObjectType() == "switch") {
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
