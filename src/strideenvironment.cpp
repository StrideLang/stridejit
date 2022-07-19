#include "strideenvironment.hpp"

StrideEnvironment::StrideEnvironment() {}

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

// llvm::orc::MaterializationUnit::Interface
// llvm::orc::StrideASTLayer::getInterface(FunctionAST &F) {
//  MangleAndInterner Mangle(BaseLayer.getExecutionSession(), DL);
//  SymbolFlagsMap Symbols;
//  Symbols[Mangle(F.getName())] =
//      JITSymbolFlags(JITSymbolFlags::Exported | JITSymbolFlags::Callable);
//  return MaterializationUnit::Interface(std::move(Symbols), nullptr);
//}

// llvm::orc::StrideASTLayer::StrideASTLayer(IRLayer &BaseLayer,
//                                          const DataLayout &DL)
//    : BaseLayer(BaseLayer), DL(DL) {}

// llvm::Error llvm::orc::StrideASTLayer::add(ResourceTrackerSP RT,
//                                           std::unique_ptr<FunctionAST> F) {
//  return RT->getJITDylib().define(
//      std::make_unique<StrideASTMaterializationUnit>(*this, std::move(F)),
//      RT);
//}

// void llvm::orc::StrideASTLayer::emit(
//    std::unique_ptr<MaterializationResponsibility> MR,
//    std::unique_ptr<FunctionAST> F) {
//  // FIXME use actual state
//  JitState state;
//  BaseLayer.emit(std::move(MR), irgenAndTakeOwnership(*F, "", state));
//}

// llvm::orc::StrideASTMaterializationUnit::StrideASTMaterializationUnit(
//    StrideASTLayer &L, std::unique_ptr<FunctionAST> F)
//    : MaterializationUnit(L.getInterface(*F)), L(L), F(std::move(F)) {}

// void llvm::orc::StrideASTMaterializationUnit::materialize(
//    std::unique_ptr<MaterializationResponsibility> R) {
//  L.emit(std::move(R), std::move(F));
//}

/// --------------------------
// llvm::orc::StrideJIT::StrideJIT(
//    std::unique_ptr<llvm::orc::ExecutionSession> ES,
//    std::unique_ptr<llvm::orc::EPCIndirectionUtils> EPCIU,
//    llvm::orc::JITTargetMachineBuilder JTMB, llvm::DataLayout DL)
//    : ES(std::move(ES)), EPCIU(std::move(EPCIU)), DL(std::move(DL)),
//      Mangle(*this->ES, this->DL),
//      ObjectLayer(*this->ES,
//                  []() { return std::make_unique<SectionMemoryManager>(); }),
//      CompileLayer(*this->ES, ObjectLayer,
//                   std::make_unique<ConcurrentIRCompiler>(std::move(JTMB))),
//      OptimizeLayer(*this->ES, CompileLayer, optimizeModule),
//      ASTLayer(OptimizeLayer, this->DL),
//      MainJD(this->ES->createBareJITDylib("<main>")) {

//  LLVMInitializeNativeTarget();
//  LLVMInitializeNativeAsmPrinter();

//  MainJD.addGenerator(
//      cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
//          DL.getGlobalPrefix())));
//}

// llvm::orc::StrideJIT::~StrideJIT() {
//  if (auto Err = ES->endSession())
//    ES->reportError(std::move(Err));
//  if (auto Err = EPCIU->cleanup())
//    ES->reportError(std::move(Err));
//}

// llvm::Expected<std::unique_ptr<llvm::orc::StrideJIT>>
// llvm::orc::StrideJIT::Create() {
//  auto EPC = SelfExecutorProcessControl::Create();
//  if (!EPC)
//    return EPC.takeError();

//  auto ES = std::make_unique<ExecutionSession>(std::move(*EPC));

//  auto EPCIU =
//      llvm::orc::EPCIndirectionUtils::Create(ES->getExecutorProcessControl());
//  if (!EPCIU)
//    return EPCIU.takeError();

//  (*EPCIU)->createLazyCallThroughManager(
//      *ES, pointerToJITTargetAddress(&handleLazyCallThroughError));

//  if (auto Err = setUpInProcessLCTMReentryViaEPCIU(**EPCIU))
//    return std::move(Err);

//  llvm::orc::JITTargetMachineBuilder JTMB(
//      ES->getExecutorProcessControl().getTargetTriple());

//  auto DL = JTMB.getDefaultDataLayoutForTarget();
//  if (!DL)
//    return DL.takeError();

//  return std::make_unique<StrideJIT>(std::move(ES), std::move(*EPCIU),
//                                     std::move(JTMB), std::move(*DL));
//}

// llvm::Error llvm::orc::StrideJIT::addAST(std::unique_ptr<FunctionAST> F,
//                                         ResourceTrackerSP RT) {
//  if (!RT)
//    RT = MainJD.getDefaultResourceTracker();
//  return ASTLayer.add(RT, std::move(F));
//}

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
  }
  return nullptr;
}

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
      if (prev->getNodeType() != AST::Function) {
        // Functions pass the output as arguments, so no need to assign
        generated.expr = std::make_unique<BinaryExprAST>(
            '=', std::move(block), std::move(generated.expr));
      }

    } else if (current->getNodeType() == AST::Function) {
      auto newFunc = std::static_pointer_cast<FunctionNode>(current);
      generated.functions.push_back(
          createFunctionDeclaration(newFunc, prev, next, tree, state));
      std::vector<std::unique_ptr<ExprAST>> Args;
      if (prev) {
        Args.emplace_back(std::move(generated.expr));
      }
      if (next) {
        // FIXME: ensure next node is processed correctly. This will not work
        // for many cases, e.g. if next is a function
        auto nextExpr = createExpr(next);
        Args.emplace_back(createExpr(next));
      }
      generated.expr = std::make_unique<CallExprAST>(
          std::string(newFunc->getName()), std::move(Args));
    } else if (current->getNodeType() == AST::Int ||
               current->getNodeType() == AST::String) {
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
  for (const auto &streamNode : streams->getChildren()) {
    if (streamNode->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(streamNode);
      collected = createStreamCode(stream, tree, state).expr;
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
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);
      std::vector<std::unique_ptr<ExprAST>> Args;

      auto code = createStreamCode(stream, tree, state);

      for (const auto &f : code.functions) {
        f->codegen(state);
      }

      std::vector<std::string> MainArgs;
      std::vector<std::string> OutArgs;

      for (const auto &node : tree->getChildren()) {
        if (node->getNodeType() == AST::Declaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(node);
          if (decl->getObjectType() == "signal" ||
              decl->getObjectType() == "constant") {
            OutArgs.push_back(decl->getName());
          }
        }
      }

      auto proto = std::make_unique<PrototypeAST>(std::string("entry"),
                                                  MainArgs, OutArgs);

      auto newfunc =
          std::make_unique<FunctionAST>(std::move(proto), std::move(code.expr));

      newfunc->codegen(state);
      //      auto *RetVal =
      //          llvm::ConstantInt::get(state.Builder->getInt32Ty(), 0, true);
      //      state.Builder->CreateRet(RetVal);
    }
  }
}
