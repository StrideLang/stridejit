#include "strideenvironment.hpp"

StrideEnvironment::StrideEnvironment() {}

bool StrideEnvironment::compile(std::string path) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  ASTNode tree;
  tree = ASTFunctions::parseFile(path.c_str());

  generateCode(tree, state);

  for (auto &F : *state.TheModule) {
    state.TheFPM->run(F);
  }

  auto JTMB = llvm::orc::JITTargetMachineBuilder::detectHost();
  if (!JTMB) {
    std::cerr << " No machine builder" << std::endl;
    return false;
  }
  JTMB->setCodeModel(llvm::CodeModel::Small);

  auto JIT_ =
      llvm::orc::LLJITBuilder()
          .setJITTargetMachineBuilder(std::move(*JTMB))
          //          .setObjectLinkingLayerCreator(
          //              [&](orc::ExecutionSession &ES, const Triple &TT) {
          //                  // Create ObjectLinkingLayer.
          //                  auto ObjLinkingLayer =
          //                  std::make_unique<orc::ObjectLinkingLayer>(
          //                      ES,
          // jitlink::InProcessMemoryManager::Create());
          //                  // Add an instance of our plugin.
          //// ObjLinkingLayer->addPlugin(std::make_unique<MyPlugin>());
          //                  return ObjLinkingLayer;
          //              })
          .create();
  if (!JIT_)
    return false; // JIT.takeError();
  JIT = std::move(*JIT_);
  auto librarySearch =
      llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
          JIT->getDataLayout().getGlobalPrefix());
  if (!librarySearch) {
    return false;
  }
  JIT->getMainJITDylib().addGenerator(std::move(*librarySearch));
  // TODO only load required libraries
  if (!llvm::sys::DynamicLibrary::LoadLibraryPermanently("m")) {
    std::cerr << "Failed to load m" << std::endl;
  }
  llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
  if (mVerbose) {
    state.TheModule->dump();
  }
  if (auto Err = JIT->addIRModule(llvm::orc::ThreadSafeModule(
          std::move(state.TheModule), std::move(state.TheContext)))) {
    return false;
  }
  return true;
}

///////// -----------------------

#include "exprast.hpp"
#include "functionast.hpp"
#include "numberexprast.hpp"

#include "astquery.h"

//#include "llvm/ExecutionEngine/ExecutionEngine.h"
//#include "llvm/ExecutionEngine/GenericValue.h"
//#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

llvm::orc::ThreadSafeModule irgenAndTakeOwnership(FunctionAST &FnAST,
                                                  const std::string &Suffix,
                                                  JitState &state) {
  if (auto *F = FnAST.codegen(state)) {
    F->setName(F->getName() + Suffix);
    auto TSM = llvm::orc::ThreadSafeModule(std::move(state.TheModule),
                                           std::move(state.TheContext));
    //        // Start a new module.
    //        InitializeModule();
    return TSM;
  } else {

    //        report_fatal_error("Couldn't compile lazily JIT'd function");
  }
}

std::unique_ptr<ExprAST> createExpr(ASTNode node) {
  if (node->getNodeType() == AST::Block) {
    return std::make_unique<VariableExprAST>(
        std::static_pointer_cast<BlockNode>(node)->getName());
  } else if (node->getNodeType() == AST::Real) {
    // TODO separate float types
    return std::make_unique<RealExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getRealValue());
  } else if (node->getNodeType() == AST::Int) {
    // TODO separate int types
    return std::make_unique<IntExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getIntValue());
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

      return std::make_unique<BinaryExprAST>(op, std::move(l), std::move(r));
    }
  } else if (node->getNodeType() == AST::List) {
    auto list = std::make_unique<ListExprAST>();
    for (const auto &elem : node->getChildren()) {
      list->addElement(createExpr(elem));
    }
    return list;
  }
  std::cerr << " Node type not supported " << std::endl;
  return nullptr;
}

struct FunctionMapEntry {
  std::string name;
  std::vector<llvm::Type *> args;
};

GeneratedCode createStreamCode(std::shared_ptr<StreamNode> stream, ASTNode tree,
                               JitState &state) {
  GeneratedCode generated;
  ASTNode prev = nullptr;
  ASTNode next;
  ASTNode current;

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
    if (current->getNodeType() == AST::Expression) {
      assert(prev == nullptr);
      generated.expr = createExpr(current);
    } else if (current->getNodeType() == AST::Block) {
      auto block = createExpr(current);
      if (prev && prev->getNodeType() == AST::Function) {
        auto prevFunc = std::static_pointer_cast<FunctionNode>(prev);
        auto externFunc = state.functionMap.find(prevFunc->getName());
        if (externFunc != state.functionMap.end()) {
          generated.expr = std::make_unique<BinaryExprAST>(
              '=', std::move(block), std::move(generated.expr));
        } else {
          // Stride Functions pass the output as arguments, so no need to assign
        }
      } else if (generated.expr) {
        generated.expr = std::make_unique<BinaryExprAST>(
            '=', std::move(block), std::move(generated.expr));
      } else {
        generated.expr = std::move(block);
      }

    } else if (current->getNodeType() == AST::Function) {
      auto newFunc = std::static_pointer_cast<FunctionNode>(current);
      std::vector<std::unique_ptr<ExprAST>> Args;
      if (prev) { // inputs
        if (auto *v = dynamic_cast<ListExprAST *>(generated.expr.get())) {
          for (auto elem = v->elements().begin(); elem != v->elements().end();
               elem++) {
            Args.emplace_back(std::move(*elem));
          }
        } else {
          Args.emplace_back(std::move(generated.expr));
        }
      }
      auto externFunc = state.functionMap.find(newFunc->getName());
      if (externFunc != state.functionMap.end()) {
        if (!state.TheModule->getFunction(externFunc->second.first)) {
          generated.externalFunctions.push_back(llvm::Function::Create(
              externFunc->second.second, llvm::Function::ExternalLinkage,
              externFunc->second.first, *state.TheModule));
        }

        generated.expr = std::make_unique<CallExprAST>(externFunc->second.first,
                                                       std::move(Args));
      } else {
        if (next) { // outputs
          // FIXME: ensure next node is processed correctly. This will not work
          // for many cases, e.g. if next is a function
          auto nextExpr = createExpr(next);
          Args.emplace_back(createExpr(next));
        }
        generated.functions.push_back(
            createFunctionDeclaration(newFunc, prev, next, tree, state));

        generated.expr = std::make_unique<CallExprAST>(
            std::string(newFunc->getName()), std::move(Args));
      }

    } else if (current->getNodeType() == AST::Int ||
               current->getNodeType() == AST::String) {
      generated.expr = createExpr(current);
    } else if (current->getNodeType() == AST::List) {
      generated.expr = createExpr(current);
    }
    prev = current;
    current = next;
  } while (current);
  return generated;
}

std::unique_ptr<FunctionAST>
createFunctionDecl(std::shared_ptr<FunctionNode> func, ASTNode prev,
                   ASTNode next, ASTNode tree, JitState &state) {

  auto funcDecl = ASTQuery::findDeclarationByName(func->getName(), {}, tree);
  if (!funcDecl) {
    return nullptr;
  }
  std::vector<std::string> Args;
  std::vector<std::string> OutArgs;

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
        Args.push_back(portBlockName);
      } else if (decl->getObjectType() == "mainOutputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        OutArgs.push_back(portBlockName);
      } else if (decl->getObjectType() == "propertyInputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        Args.push_back(portBlockName);
      } else if (decl->getObjectType() == "propertyOutputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        OutArgs.push_back(portBlockName);
      }
    }
  }

  auto streams = funcDecl->getPropertyValue("streams");

  std::unique_ptr<ExprAST> collected, out;
  std::vector<llvm::Function *> externalFunctions;
  for (const auto &streamNode : streams->getChildren()) {
    if (streamNode->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(streamNode);
      auto code = createStreamCode(stream, tree, state);
      collected = std::move(code.expr);
      externalFunctions.insert(externalFunctions.end(),
                               code.externalFunctions.begin(),
                               code.externalFunctions.end());
    } else {
    }
  }

  auto proto = std::make_unique<PrototypeAST>(func->getName(), Args, OutArgs);
  llvm::Function *TheFunction = state.getFunction(func->getName());
  if (TheFunction) {
    // TODO should check if current function is the same
    return nullptr;
  }

  // llvm::Function *TheFunction = state.getFunction(P.getName());
  auto newfunc =
      std::make_unique<FunctionAST>(std::move(proto), std::move(collected));
  newfunc->externalFunctions = std::move(externalFunctions);
  return newfunc;
}

std::unique_ptr<FunctionAST>
createFunctionDeclaration(std::shared_ptr<FunctionNode> func, ASTNode prev,
                          ASTNode next, ASTNode tree, JitState &state) {

  auto funcDecl = createFunctionDecl(func, prev, next, tree, state);

  return funcDecl;
}

// void createGlobals(ASTNode tree, JitState &state) {
//  for (const auto &node : tree->getChildren()) {
//    if (node->getNodeType() == AST::Declaration) {
//      auto decl = std::static_pointer_cast<DeclarationNode>(node);
//      if (decl->getObjectType() == "signal" ||
//          decl->getObjectType() == "constant") {

//        auto global = state.TheModule->getOrInsertGlobal(
//            decl->getName(), llvm::Type::getDoubleTy(*state.TheContext));
//        //      // Add arguments to variable symbol table.
//        //        state.Builder->CreateStore(decl->getName(), Alloca);
//        //        global->setCon
//        auto globalvar = state.TheModule->getGlobalVariable(decl->getName());
//        globalvar->setConstant(false);
//        globalvar->setInitializer(llvm::ConstantFP::get(
//            llvm::Type::getDoubleTy(*state.TheContext), 0.0));
//        state.NamedValues[decl->getName()] = globalvar;

//        // llvm::Function *TheFunction = state.getFunction(P.getName());
//      }
//    }
//  }
//}

void generateCode(ASTNode tree, JitState &state) {

  //  createGlobals(tree, state);
  std::map<std::string, std::vector<std::unique_ptr<ExprAST>>> domainCode;
  std::vector<std::string> MainArgs;
  std::vector<std::string> OutArgs;

  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);
      std::vector<std::unique_ptr<ExprAST>> Args;

      auto code = createStreamCode(stream, tree, state);

      for (const auto &f : code.functions) {
        f->codegen(state);
      }

      std::string domain = "DefaultDomain";
      domainCode[domain].emplace_back(std::move(code.expr));
      //      auto *RetVal =
      //          llvm::ConstantInt::get(state.Builder->getInt32Ty(), 0, true);
      //      state.Builder->CreateRet(RetVal);
    } else if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getObjectType() == "signal" ||
          decl->getObjectType() == "constant") {
        // Global signals become pointers to domain function
        OutArgs.push_back(decl->getName());
      }
    }
  }
  for (auto it = domainCode.begin(); it != domainCode.end(); it++) {
    auto proto = std::make_unique<PrototypeAST>(
        std::string(it->first + "_process"), MainArgs, OutArgs);

    auto newfunc =
        std::make_unique<FunctionAST>(std::move(proto), std::move(it->second));

    newfunc->codegen(state);
  }
}

extern "C" {
__declspec(dllexport) double __stride_Greater(double a, double b) {
  return a > b ? 1.0 : 0.0;
}
}
