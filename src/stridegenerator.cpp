#include <iostream>

#include "stride/stridejit/stridecompiler.hpp"
#include "stride/stridejit/stridegenerator.hpp"

#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/functionast.hpp"
#include "stride/stridejit/listexprast.hpp"
#include "stride/stridejit/numberexprast.hpp"

// stride
#include "stride/codegen/astquery.hpp"
#include "stride/codegen/codeanalysis.hpp"

// llvm
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

void StrideGenerator::generateCode(ASTNode tree, ScopeStack *scope,
                                   StrideCompiler &state) {

  //  createGlobals(tree, state);
  std::map<std::string, std::vector<std::unique_ptr<ExprAST>>>
      domainGeneratedCode;
  std::vector<std::shared_ptr<DeclarationNode>> GlobalSignals;

  std::vector<DomainArg> domainArgs;

  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);

      auto code = createStreamCode(stream, tree, scope, state);
      for (auto &domainCode : code) {
        for (const auto &f : domainCode.second.functions) {
          f->codegen(state);
        }

        while (domainCode.second.expr.size() > 0) {
          domainGeneratedCode[domainCode.first].emplace_back(
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
        // Global signals become pointers to domain function
        auto *type = state.getLLVMType(decl);
        // TODO Group globals by domain
        GlobalSignals.push_back(decl);
        if (type->isDoubleTy()) {
          domainArgs.push_back({decl->getName(), DataType::DOUBLE});
        } else if (type->isIntegerTy(1)) {
          domainArgs.push_back({decl->getName(), DataType::BOOL});
        } else if (type->isIntegerTy(32)) {
          domainArgs.push_back({decl->getName(), DataType::INT32});
          //        } else if (type->isIntegerTy(64)) {
          //          domainArgs.push_back({decl->getName(), DataType::INT64});
        } else {
          assert(0 == 1);
        }
      } else if (decl->getObjectType() == "switch") {
        GlobalSignals.push_back(decl);
        domainArgs.push_back({decl->getName(), DataType::BOOL});
      } else {
        continue;
      }
    }
  }
  // Generate domain functions

  // Determine external variables that become arguments to the domain function.
  std::vector<PrototypeArg> ExternalArgs;
  for (auto it = domainGeneratedCode.begin(); it != domainGeneratedCode.end();
       it++) {
    const std::string &domainName = it->first;
    // Find domain and insert inputs and output to tree.
    auto domainDecl = ASTQuery::findDeclarationByName(domainName, *scope, tree);
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

            // TODO for now, all domain inputs are external. Internal (pass by
            // value) inputs should be allowed
            bool external = true;
            if (external) {

              ExternalArgs.push_back(
                  {decl->getName(),
                   llvm::PointerType::get(state.getLLVMType(decl), 0),
                   std::string(), getDefaultValue(decl, state)});
            } else {
              ExternalArgs.push_back({decl->getName(), state.getLLVMType(decl),
                                      std::string(),
                                      getDefaultValue(decl, state)});
            }
            // Remove external from globals list
            for (auto global = GlobalSignals.begin();
                 global != GlobalSignals.end(); global++) {
              if ((*global)->getName() == decl->getName()) {
                GlobalSignals.erase(global);
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
            // TODO for now, all domain outputs are external. Internal (pass by
            // value) outputs should be allowed
            bool external = true;
            if (external) {

              ExternalArgs.push_back(
                  {decl->getName(),
                   llvm::PointerType::get(state.getLLVMType(decl), 0),
                   std::string(), getDefaultValue(decl, state)});
            } else {
              ExternalArgs.push_back({decl->getName(), state.getLLVMType(decl),
                                      std::string(),
                                      getDefaultValue(decl, state)});
            }
            // Remove external from globals list
            for (auto global = GlobalSignals.begin();
                 global != GlobalSignals.end(); global++) {
              if ((*global)->getName() == decl->getName()) {
                GlobalSignals.erase(global);
                break;
              }
            }
          }
        }
      }
    }

    auto proto = std::make_unique<PrototypeAST>(
        std::string(domainName + "_process"), std::vector<PrototypeArg>{},
        std::vector<PrototypeArg>{}, ExternalArgs, std::vector<PrototypeArg>{});
    auto newfunc =
        std::make_unique<FunctionAST>(std::move(proto), std::move(it->second));
    newfunc->callType = CallableType::DomainFunction;

    // Add domain member variables (globals)
    for (const auto &global : GlobalSignals) {
      newfunc->internalVariables.push_back(global);
    }

    newfunc->codegen(state);

    auto initProto = std::make_unique<PrototypeAST>(
        std::string(domainName + "_init"), std::vector<PrototypeArg>{},
        std::vector<PrototypeArg>{}, ExternalArgs, std::vector<PrototypeArg>{});
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
    auto initFunc = std::make_unique<FunctionAST>(std::move(initProto),
                                                  std::move(resetBody));
    initFunc->callType = CallableType::DomainFunction;
    // Add domain member variables (globals)
    for (const auto &global : GlobalSignals) {
      initFunc->internalVariables.push_back(global);
    }
    initFunc->codegen(state);

    state.domainArgs[domainName] = domainArgs;
  }
}

std::unique_ptr<ExprAST> StrideGenerator::createExpr(ASTNode node) {
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

struct FunctionMapEntry {
  std::string name;
  std::vector<llvm::Type *> args;
};

StrideGenerator::GeneratedCode
StrideGenerator::createStreamCode(std::shared_ptr<StreamNode> stream,
                                  ASTNode tree, ScopeStack *scope,
                                  StrideCompiler &state) {
  StrideGenerator::GeneratedCode generated;
  ASTNode prev = nullptr;
  ASTNode next;
  ASTNode current;
  std::shared_ptr<StreamNode> inputStream = stream;

  do {
    if (stream && stream->getNodeType() == AST::Stream) {
      current = stream->getLeft();

      if (stream->getRight()->getNodeType() == AST::Stream) {
        stream = std::static_pointer_cast<StreamNode>(stream->getRight());
        next = stream->getLeft();
      } else {
        next = stream->getRight();
        stream = nullptr;
      }
    } else {
      next = nullptr;
      stream = nullptr;
    }
    auto domainName = CodeAnalysis::getNodeDomainName(current, *scope, tree);
    if (domainName.size() == 0) {
      auto streamPtr = stream;
      // FIXME resolve downstream domains correctly
      auto connection =
          CodeAnalysis::resolveConnectionBlock(next, *scope, tree, true);
      if (connection) {
        domainName = CodeAnalysis::getNodeDomainName(connection, *scope, tree);
      } else {
        domainName = "RootDomain";
      }
    }
    if (current->getNodeType() == AST::Expression) {
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
      auto block = createExpr(current);
      auto decl = ASTQuery::findDeclarationByName(
          ASTQuery::getNodeName(current), *scope, tree);
      if (current->getNodeType() == AST::Bundle) {
        auto indexNode = std::static_pointer_cast<BundleNode>(current)->index();
        if (indexNode->getNodeType() == AST::List) {
          for (const auto &elem : indexNode->getChildren()) {
            if (elem->getNodeType() == AST::Block) {
              auto indexDecl = ASTQuery::findDeclarationByName(
                  ASTQuery::getNodeName(elem), *scope, tree);
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
        std::cerr << "No declaration for: " << ASTQuery::getNodeName(current)
                  << " in " << AST::toText(inputStream) << " in "
                  << inputStream->getFilename() << ":" << inputStream->getLine()
                  << std::endl;
        return generated;
      }
      // TODO accumulate read and write variables for switch, expressions,
      // lists and function arguments
      // If on root namespace
      if (current == inputStream->getLeft()) {
        generated[domainName].readVariables.push_back(current);
      } else {
        generated[domainName].writeVariables.push_back(current);
        if (!next) {
          // TODO needed?
          generated[domainName].readVariables.push_back(current);
        }
      }
      if (prev && prev->getNodeType() == AST::Function) {
        auto prevFunc = std::static_pointer_cast<FunctionNode>(prev);
        auto funcDecl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(prev), *scope, tree);
        if (!funcDecl) {
          assert(0 == 1);
        }
        auto retType = state.getLLVMType(decl);
        if (auto callexpr = dynamic_cast<CallExprAST *>(
                generated[domainName].expr.back().get())) {

          std::vector<llvm::Type *> argTypes;
          for (auto it = callexpr->OutArgs.begin();
               it != callexpr->OutArgs.end(); it++) {

            if (auto *ve = dynamic_cast<VariableExprAST *>(it->get())) {
              if (!funcDecl->getPropertyValue("blocks")) {
                assert(0 == 1);
              } else {
                auto argDecl = ASTQuery::findDeclarationByName(
                    ve->getName(),
                    {{nullptr,
                      funcDecl->getPropertyValue("blocks")->getChildren()}},
                    nullptr);
                if (argDecl) {
                  argTypes.push_back(state.getLLVMType(argDecl));
                } else {
                  std::cerr
                      << "ERROR falling back on double type. Arg type nto found"
                      << std::endl;
                  argTypes.push_back(
                      llvm::Type::getDoubleTy(*state.TheContext));
                }
              }
            }
          }
          for (auto it = callexpr->InArgs.begin(); it != callexpr->InArgs.end();
               it++) {
            if (auto *pp = dynamic_cast<PortPropertyAST *>(it->get())) {
              // TODO get port property integer type from platform definition
              // This appears in other places, make suer to change all
              argTypes.push_back(llvm::Type::getInt32Ty(*state.TheContext));
            } else if (auto *ve = dynamic_cast<VariableExprAST *>(it->get())) {
              if (!funcDecl->getPropertyValue("blocks")) {
                // FIXME this will happen for platform functions where the
                // inputs and output types are not in the blocks port.
                std::cerr
                    << "ERROR falling back on double type. Arg type not found"
                    << std::endl;
                argTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
              } else {
                auto argDecl = ASTQuery::findDeclarationByName(
                    ve->getName(),
                    {{nullptr,
                      funcDecl->getPropertyValue("blocks")->getChildren()}},
                    nullptr);
                if (argDecl) {
                  argTypes.push_back(state.getLLVMType(argDecl));
                } else {
                  std::cerr
                      << "ERROR falling back on double type. Arg type not found"
                      << std::endl;
                  argTypes.push_back(
                      llvm::Type::getDoubleTy(*state.TheContext));
                }
              }
            } else {
              argTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
            }
          }
          //        if (callexpr->isExternal) {
          //          assert(argTypes.size() > 0);
          //          argTypes.resize(argTypes.size() - 1);
          //        }

          // TODO do we also test that the function is not declared in Stride
          // code? Or is this check enough?
          auto externFunc =
              state.getExternalFunction(prevFunc->getName(), retType, argTypes);
          if (externFunc) {
            generated[domainName].expr.back() = std::make_unique<BinaryExprAST>(
                '=', std::move(block),
                std::move(generated[domainName].expr.back()));
          } else {
            // Stride Functions pass the output as arguments, so no need to
            // assign
          }
        } else if (dynamic_cast<LLVMCommandAST *>(
                       generated[domainName].expr.back().get())) {
          auto outputsNode = funcDecl->getPropertyValue("outputs");
          llvm::Type *outType;
          if (outputsNode && outputsNode->getChildren().size() > 0) {
            auto outputTypeNode = outputsNode->getChildren()[0];
            if (outputTypeNode && outputTypeNode->getNodeType() == AST::Block) {
              auto outputType =
                  std::static_pointer_cast<BlockNode>(outputTypeNode);
              if (state.typesMap.find(outputType->getName()) !=
                  state.typesMap.end()) {
                outType = state.typesMap[outputType->getName()];
              }
            }
          }

          if (retType != outType) {
            if (retType->isIntegerTy() && outType->isDoubleTy()) {
              //              block = state.Builder->CreateFPToSI(
              //                  block,
              //                  llvm::Type::getDoubleTy(*state.TheContext));
            }
          }

          if (decl->getObjectType() == "trigger") {
            std::vector<std::unique_ptr<ExprAST>> Expressions;
            auto triggerResetsNode = decl->getCompilerProperty("triggerResets");
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
                          std::static_pointer_cast<BlockNode>(typeNode)
                              ->getName();
                      auto defaultNode =
                          resetNodeDecl->getPropertyValue("default");
                      if (!defaultNode) {
                        continue;
                      }
                      if (typeName == "_RealType" &&
                          defaultNode->getNodeType() == AST::Real) {
                        double defaultValue =
                            std::static_pointer_cast<ValueNode>(defaultNode)
                                ->getRealValue();
                        auto varInit = std::make_unique<BinaryExprAST>(
                            '=',
                            std::move(std::make_unique<VariableExprAST>(
                                resetNodeDecl->getName())),
                            std::move(
                                std::make_unique<RealExprAST>(defaultValue)));
                        Expressions.push_back(std::move(varInit));
                      } else if (typeName == "_IntType" &&
                                 defaultNode->getNodeType() == AST::Int) {
                        int32_t defaultValue =
                            std::static_pointer_cast<ValueNode>(defaultNode)
                                ->getIntValue();
                        auto varInit = std::make_unique<BinaryExprAST>(
                            '=',
                            std::move(std::make_unique<VariableExprAST>(
                                resetNodeDecl->getName())),
                            std::move(
                                std::make_unique<IntExprAST>(defaultValue)));
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
                '=', std::move(block),
                std::move(generated[domainName].expr.back()));
          }
        } else {
          assert(0 == 1);
        }
      } else if (generated[domainName].expr.size() > 0) {
        generated[domainName].expr.back() = std::make_unique<BinaryExprAST>(
            '=', std::move(block),
            std::move(generated[domainName].expr.back()));
      } else {
        generated[domainName].expr.push_back(std::move(block));
      }
    } else if (current->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(current);
      auto funcDecl =
          ASTQuery::findDeclarationByName(func->getName(), *scope, tree);

      std::vector<std::unique_ptr<ExprAST>> mainInArgs;
      std::vector<llvm::Type *> mainInArgTypes;
      std::vector<std::unique_ptr<ExprAST>> mainOutArgs;
      std::vector<llvm::Type *> mainOutArgTypes;
      //      std::unique_ptr<ExprAST> ret;
      llvm::Type *retType;
      if (prev) { // inputs
        if (auto prevFunc = std::dynamic_pointer_cast<FunctionNode>(prev)) {
          mainInArgs.emplace_back(std::move(generated[domainName].expr.back()));
          generated[domainName].expr.pop_back();
          // FIXME do automatic type casting int ->float
          mainInArgTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
        } else if (auto prevList = std::dynamic_pointer_cast<ListNode>(prev)) {
          auto *v = dynamic_cast<ListExprAST *>(
              generated[domainName].expr.back().get());
          auto listNodes = prevList->getChildren();
          auto nodeIt = listNodes.begin();
          if (v->getType() == ListExprAST::Type::MUTABLE_CONSISTENT) {
            for (auto elem = v->elements().begin(); elem != v->elements().end();
                 elem++) {
              mainInArgs.emplace_back(std::move(*elem));
              auto elemDecl = ASTQuery::findDeclarationByName(
                  ASTQuery::getNodeName(*nodeIt), *scope, tree);
              if (elemDecl) {
                mainInArgTypes.push_back(state.getLLVMType(elemDecl));
              } else if ((*nodeIt)->getNodeType() == AST::Int) {
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
                mainInArgTypes.push_back(
                    llvm::Type::getDoubleTy(*state.TheContext));
              }

              nodeIt++;
            }
          } else if (v->getType() == ListExprAST::Type::IMMUTABLE_CONSISTENT) {
            mainInArgs.emplace_back(
                std::move(generated[domainName].expr.back()));
            mainInArgTypes.push_back(llvm::PointerType::get(
                llvm::Type::getDoubleTy(*state.TheContext), 0));
          } else {
            assert(0 == 1);
          }
          generated[domainName].expr.pop_back();

        } else {
          mainInArgs.emplace_back(std::move(generated[domainName].expr.back()));
          generated[domainName].expr.pop_back();
          // FIXME set correct type
          mainInArgTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
        }
      }

      std::optional<ExternalFunction> externFunc;
      if (next) { // outputs
        // FIXME: ensure next node is processed correctly. This will not
        // work for many cases, e.g. if next is a function
        auto nextExpr = createExpr(next);
        std::shared_ptr<DeclarationNode> nextDecl =
            ASTQuery::findDeclarationByName(ASTQuery::getNodeName(next), *scope,
                                            tree);
        externFunc = state.getExternalFunction(
            func->getName(), state.getLLVMType(nextDecl), mainInArgTypes);
        if (!externFunc) {
          assert(nextExpr);
          mainOutArgs.emplace_back(std::move(nextExpr));
          mainOutArgTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
        }
      } else {
        externFunc = state.getExternalFunction(
            func->getName(), state.getLLVMType(nullptr), mainInArgTypes);
      }

      if (externFunc) {
        if (externFunc->name.size() > 0 && externFunc->name[0] == '@') {
          if (!state.TheModule->getFunction(externFunc->name)) {
            auto *newFunc = llvm::Function::Create(
                externFunc->llvmFunctionType, llvm::Function::ExternalLinkage,
                externFunc->name.substr(1), *state.TheModule);
            generated[domainName].externalFunctions.push_back(newFunc);
          }
          auto newCall = std::make_unique<CallExprAST>(
              externFunc->name.substr(1), std::move(mainOutArgs),
              std::move(mainInArgs), std::vector<std::unique_ptr<ExprAST>>{},
              std::vector<std::unique_ptr<ExprAST>>{});
          newCall->callType = CallableType::External;
          generated[domainName].expr.push_back(std::move(newCall));
          std::cerr << " Using external function:" << externFunc->name
                    << std::endl;
        } else {
          auto newCall = std::make_unique<LLVMCommandAST>(
              externFunc->name, std::move(mainOutArgs), std::move(mainInArgs),
              std::vector<std::unique_ptr<ExprAST>>{},
              std::vector<std::unique_ptr<ExprAST>>{});

          generated[domainName].expr.push_back(std::move(newCall));
        }
      } else {
        auto newFuncDecl =
            createFunctionDeclaration(func, prev, next, tree, scope, state);
        std::vector<std::unique_ptr<ExprAST>> ExternalArgs;
        if (newFuncDecl) {
          for (const auto &arg : newFuncDecl->getProto().getExternalArgs()) {
            ExternalArgs.push_back(std::make_unique<VariableExprAST>(arg.name));
          }
        }
        std::vector<std::unique_ptr<ExprAST>> PortPropArgs;
        if (funcDecl) {
          auto usedPortProps = CodeAnalysis::getUsedPortProperties(funcDecl);
          auto innerScope = *scope;
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
              // TODO determine integer type for size from platform defintion.
              PortPropArgs.push_back(std::make_unique<IntExprAST>(size, 32));
            } else if (ppNode->getPortName() == "rate") {
              auto rate = CodeAnalysis::evaluateRatePortProperty(
                  ppNode->getName(), innerScope, funcDecl, func, tree);
              PortPropArgs.push_back(std::make_unique<RealExprAST>(rate));
            }
          }
        }
        auto callexpr = std::make_unique<CallExprAST>(
            std::string(func->getName()), std::move(mainOutArgs),
            std::move(mainInArgs), std::move(ExternalArgs),
            std::move(PortPropArgs));
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
      }
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
    prev = current;
    current = next;
  } while (current);
  return generated;
}

std::unique_ptr<FunctionAST> StrideGenerator::createFunctionDeclaration(
    std::shared_ptr<FunctionNode> func, ASTNode prev, ASTNode next,
    ASTNode tree, ScopeStack *scope, StrideCompiler &state) {

  auto funcDecl =
      ASTQuery::findDeclarationByName(func->getName(), *scope, tree);
  if (!funcDecl) {
    return nullptr;
  }
  std::vector<PrototypeArg> Args;
  std::vector<PrototypeArg> OutArgs;
  std::vector<PrototypeArg> ExternalArgs;
  std::vector<PrototypeArg> UsedPortProperties;

  auto portsNode = funcDecl->getPropertyValue("ports");
  std::string portBlockName;
  for (const auto &node : portsNode->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getObjectType() == "mainInputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        auto type = llvm::Type::getDoubleTy(*state.TheContext);
        auto blockDecl =
            ASTQuery::findDeclarationByName(portBlockName, {}, tree);
        if (blockDecl) {
          type = state.getLLVMType(blockDecl);
        }
        Args.push_back(
            PrototypeArg{portBlockName, llvm::PointerType::get(type, 0)});
      } else if (decl->getObjectType() == "mainOutputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        auto type = llvm::Type::getDoubleTy(*state.TheContext);
        auto blockDecl =
            ASTQuery::findDeclarationByName(portBlockName, {}, tree);
        if (blockDecl) {
          type = state.getLLVMType(blockDecl);
        }
        OutArgs.push_back(
            PrototypeArg{portBlockName, llvm::PointerType::get(type, 0)});
      } else if (decl->getObjectType() == "propertyInputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        auto type = llvm::Type::getDoubleTy(*state.TheContext);
        auto blockDecl =
            ASTQuery::findDeclarationByName(portBlockName, {}, tree);
        if (blockDecl) {
          type = state.getLLVMType(blockDecl);
        }
        Args.push_back(
            PrototypeArg{portBlockName, llvm::PointerType::get(type, 0)});
      } else if (decl->getObjectType() == "propertyOutputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        auto type = llvm::Type::getDoubleTy(*state.TheContext);
        auto blockDecl =
            ASTQuery::findDeclarationByName(portBlockName, {}, tree);
        if (blockDecl) {
          type = state.getLLVMType(blockDecl);
        }
        OutArgs.push_back(
            PrototypeArg{portBlockName, llvm::PointerType::get(type, 0)});
      }
    }
  }

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
  }

  functionScope.push_back({funcDecl, {}});

  auto blocks = funcDecl->getPropertyValue("blocks");
  if (blocks) {
    for (const auto &blockDecl : blocks->getChildren()) {
      functionScope.back().second.push_back(blockDecl);
    }
  }

  auto streams = funcDecl->getPropertyValue("streams");
  std::vector<std::shared_ptr<DeclarationNode>> usedInternalVariables;
  std::vector<std::shared_ptr<DeclarationNode>> usedExternalVariables;
  std::vector<std::unique_ptr<ExprAST>> collected;
  std::unique_ptr<ExprAST> out;
  std::vector<llvm::Function *> externalFunctions;
  for (const auto &streamNode : streams->getChildren()) {
    if (streamNode->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(streamNode);
      auto code = createStreamCode(stream, tree, &functionScope, state);
      for (auto &domainCode : code) {
        while (domainCode.second.expr.size() > 0) {
          collected.push_back(std::move(domainCode.second.expr.front()));
          domainCode.second.expr.erase(domainCode.second.expr.begin());
        }
        // FIXME nested functions need to have their name mangled/hashed as they
        // move to the global namespace
        for (const auto &f : domainCode.second.functions) {
          f->codegen(state);
        }
        externalFunctions.insert(externalFunctions.end(),
                                 domainCode.second.externalFunctions.begin(),
                                 domainCode.second.externalFunctions.end());
        for (const auto &readVar : domainCode.second.readVariables) {
          std::string blockName = ASTQuery::getNodeName(readVar);
          if (blockName.size() > 0 &&
              std::find_if(Args.begin(), Args.end(),
                           [&blockName](const PrototypeArg &x) {
                             return x.name == blockName;
                           }) == Args.end() &&
              std::find_if(OutArgs.begin(), OutArgs.end(),
                           [&blockName](const PrototypeArg &x) {
                             return x.name == blockName;
                           }) == OutArgs.end()) {
            auto decl = ASTQuery::findDeclarationByName(
                std::static_pointer_cast<BlockNode>(readVar)->getName(), {},
                blocks);
            if (decl) {
              usedInternalVariables.push_back(decl);
            } else if (funcDecl->getObjectType() == "reaction" ||
                       funcDecl->getObjectType() == "loop") {
              decl = ASTQuery::findDeclarationByName(
                  std::static_pointer_cast<BlockNode>(readVar)->getName(),
                  *scope, tree);
              if (decl) {
                usedExternalVariables.push_back(decl);
              }
            }
          }
        }
        for (const auto &writeVar : domainCode.second.writeVariables) {
        }
      }
    } else {
    }
  }
  for (const auto &decl : usedExternalVariables) {
    ExternalArgs.push_back(PrototypeArg{
        decl->getName(), llvm::PointerType::get(state.getLLVMType(decl), 0)});
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
  llvm::Function *TheFunction = state.getFunctionInModule(func->getName());
  if (TheFunction) {
    // TODO this is here to check if current function is the same as existing
    // function
    return nullptr;
  }
  auto proto = std::make_unique<PrototypeAST>(func->getName(), OutArgs, Args,
                                              ExternalArgs, UsedPortProperties);

  // llvm::Function *TheFunction = state.getFunctionInModule(P.getName());
  auto newfunc =
      std::make_unique<FunctionAST>(std::move(proto), std::move(collected));
  newfunc->externalFunctions = std::move(externalFunctions);
  newfunc->internalVariables = std::move(usedInternalVariables);

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
  } else {
    std::cerr << "Callable type unsuported" << std::endl;
  }
  return newfunc;
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
