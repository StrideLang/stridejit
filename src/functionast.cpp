#include "functionast.hpp"
#include "exprast.hpp"
#include "strideenvironment.hpp"

#include <iostream>

FunctionAST::FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                         std::unique_ptr<ExprAST> Body_)
    : Proto(std::move(Proto)) {
  Body.emplace_back(std::move(Body_));
}

FunctionAST::FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                         std::vector<std::unique_ptr<ExprAST>> Body)
    : Proto(std::move(Proto)), Body(std::move(Body)) {}

const PrototypeAST &FunctionAST::getProto() const { return *Proto; }

const std::string &FunctionAST::getName() const { return Proto->getName(); }

llvm::Function *FunctionAST::codegen(StrideCompiler &state) {
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto &P = *Proto;
  state.FunctionProtos[Proto->getName()] = std::move(Proto);
  llvm::Function *TheFunction = state.getFunctionInModule(P.getName());
  if (!TheFunction) {
    TheFunction = P.codegen(state);
  }
  if (!TheFunction)
    return nullptr;
  // If this is an operator, install it.
  if (P.isBinaryOp())
    state.BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();
  // Create a new basic block to start insertion into.
  llvm::BasicBlock *BB =
      llvm::BasicBlock::Create(*state.TheContext, "entry", TheFunction);
  state.Builder->SetInsertPoint(BB);
  // Record the function arguments in the NamedValues map.
  state.NamedValues.clear();
  for (auto &Arg : TheFunction->args()) {
    state.NamedValues[std::string(Arg.getName())] = &Arg;

    //    if (state.NamedValues.find(std::string(Arg.getName())) ==
    //        state.NamedValues.end()) {
    //      // Create an alloca for this variable.
    //      llvm::AllocaInst *Alloca =
    //          state.CreateEntryBlockAlloca(TheFunction, Arg.getName());
    //      //      // Add arguments to variable symbol table.
    //      state.Builder->CreateStore(&Arg, Alloca);
    //      //      // Store the initial value into the alloca.
    //      //      auto argLoad = state.Builder->CreateLoad(Arg.getType(),
    //      &Arg,
    //      //      "Input");
    //      state.NamedValues[std::string(Arg.getName())] = Alloca;
    //    } else {

    //      //      state.Builder->CreateStore(&Arg,
    //      // state.NamedValues[std::string(Arg.getName())]);
    //    }
  }
  for (const auto &v : internalVariables) {
    // TODO avoid namespace clashes

    if (v->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(v);
      if (state.NamedValues.find(std::string(decl->getName())) ==
          state.NamedValues.end()) {
        // Create an alloca for this variable.
        llvm::AllocaInst *Alloca =
            state.CreateEntryBlockAlloca(TheFunction, decl->getName());
        state.NamedValues[decl->getName()] = Alloca;
      } else {
      }
    } else {
      std::cerr << " Unsupported block declaration: " << AST::toText(v)
                << std::endl;
    }
  }
  //  state.TheModule->dump();

  //  auto *out = state.TheModule->getGlobalVariable("Out");

  //  auto *v = state.Builder->CreateLoad(
  //      llvm::Type::getDoubleTy(*state.TheContext), out, "Out");
  //  state.NamedValues["Out"] = v;
  // Add arguments to variable symbol table.

  for (const auto &statement : Body) {
    if (!statement) {
      return nullptr;
    }
    llvm::Value *RetVal = statement->codegen(state);
    if (!RetVal) {
      // Error reading body, remove function.
      TheFunction->eraseFromParent();
      if (P.isBinaryOp())
        state.BinopPrecedence.erase(P.getOperatorName());
      return nullptr;
    }
  }
  // Finish off the function.
  //    state.Builder->CreateRet(RetVal);

  auto *outVal = llvm::ConstantInt::get(state.Builder->getInt32Ty(), 0, true);
  state.Builder->CreateRet(outVal);
  // Validate the generated code, checking for consistency.
  verifyFunction(*TheFunction);
  return TheFunction;
}

llvm::Function *PrototypeAST::codegen(StrideCompiler &state) {
  // Make the function type:  double(double,double) etc.
  std::vector<llvm::Type *> Doubles;

  for (const auto &arg : OutArgs) {
    Doubles.emplace_back(arg.llvmType);
  }
  for (const auto &arg : Args) {
    Doubles.emplace_back(arg.llvmType);
  }
  llvm::FunctionType *FT = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(*state.TheContext), Doubles, false);

  llvm::Function *F = llvm::Function::Create(
      FT, llvm::Function::ExternalLinkage, Name, state.TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args()) {
    if (Idx < OutArgs.size()) {
      Arg.setName(OutArgs[Idx].name);
    } else {
      Arg.setName(Args[Idx - Args.size()].name);
    }
    Idx++;
  }

  return F;
}

llvm::Value *CallExprAST::codegen(StrideCompiler &state) {
  std::cout << "CallExprAST codegen" << std::endl;
  // Look up the name in the global module table.
  llvm::Function *CalleeF = state.getFunctionInModule(Callee);
  if (!CalleeF) {
    return state.LogErrorV("Unknown function referenced");
  }

  auto func = [](llvm::Value *value, llvm::Argument *arg,
                 StrideCompiler &state) {
    llvm::Value *ArgsV = value;
    if (value->getType()->isPointerTy() && !arg->getType()->isPointerTy()) {
      ArgsV = state.Builder->CreateLoad(
          llvm::Type::getDoubleTy(*state.TheContext), value, "");
    }
    if (!value->getType()->isPointerTy() && arg->getType()->isPointerTy()) {
      ArgsV = state.Builder->CreateAlloca(
          llvm::Type::getDoubleTy(*state.TheContext), nullptr, "Temp");
      state.Builder->CreateStore(value, ArgsV);
    }
    //    ArgsV->dump();
    //    ArgsV->getType()->dump();
    return ArgsV;
  };

  std::vector<llvm::Value *> CallArgs;

  for (unsigned i = 0, e = OutArgs.size(); i != e; ++i) {
    llvm::Value *value = OutArgs[i]->codegen(state);
    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(OutArgs[i].get());
      assert(list);
      for (const auto &expr : list->elements()) {
        auto newArg = func(expr->codegen(state), CalleeF->getArg(i), state);
        if (!newArg)
          return nullptr;
        CallArgs.push_back(std::move(newArg));
        if (CallArgs.back()->getType()->isPointerTy() &&
            !CalleeF->getArg(i)->getType()->isPointerTy()) {
          CallArgs.back() = state.Builder->CreateLoad(
              llvm::Type::getDoubleTy(*state.TheContext), CallArgs.back(), "");
        }
      }

    } else {
      auto newArg = func(value, CalleeF->getArg(i), state);
      if (!newArg)
        return nullptr;
      CallArgs.push_back(std::move(newArg));
      if (CallArgs.back()->getType()->isPointerTy() &&
          !CalleeF->getArg(i)->getType()->isPointerTy()) {
        CallArgs.back() = state.Builder->CreateLoad(
            llvm::Type::getDoubleTy(*state.TheContext), CallArgs.back(), "");
      }
    }
  }

  for (unsigned i = 0, e = InArgs.size(); i != e; ++i) {
    llvm::Value *value = InArgs[i]->codegen(state);
    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(InArgs[i].get());
      assert(list);
      for (const auto &expr : list->elements()) {
        auto newArg = func(expr->codegen(state), CalleeF->getArg(i), state);
        if (!newArg)
          return nullptr;
        CallArgs.push_back(std::move(newArg));
        if (CallArgs.back()->getType()->isPointerTy() &&
            !CalleeF->getArg(i)->getType()->isPointerTy()) {
          CallArgs.back() = state.Builder->CreateLoad(
              llvm::Type::getDoubleTy(*state.TheContext), CallArgs.back(), "");
        }
      }
    } else {
      auto newArg = func(value, CalleeF->getArg(i), state);
      if (!newArg)
        return nullptr;
      CallArgs.push_back(std::move(newArg));
      if (CallArgs.back()->getType()->isPointerTy() &&
          !CalleeF->getArg(i)->getType()->isPointerTy()) {
        CallArgs.back() = state.Builder->CreateLoad(
            llvm::Type::getDoubleTy(*state.TheContext), CallArgs.back(), "");
      }
    }
  }

  CalleeF->dump();
  llvm::CallInst *call =
      state.Builder->CreateCall(CalleeF, CallArgs, CalleeF->getName());

  // Write to output
  for (unsigned i = 0, e = OutArgs.size(); i != e; ++i) {
    llvm::Value *argValue = CallArgs[i];
    if (CalleeF->getArg(i)->getType()->isTokenTy()) {
      //      auto *list = dynamic_cast<ListExprAST *>(InArgs[i].get());
      //      assert(list);
      //      for (const auto &expr : list->elements()) {
      //        if (CallArgs.back()->getType()->isPointerTy() &&
      //            !CalleeF->getArg(i)->getType()->isPointerTy()) {
      //          CallArgs.back() = state.Builder->CreateStore(
      //              llvm::Type::getDoubleTy(*state.TheContext),
      //              CallArgs.back(), "");
      //        }
      //      }
    } else {
      if (CalleeF->getArg(i)->getType()->isPointerTy()) {
        if (argValue->getType()->isPointerTy()) {
          //          auto loadInst = state.Builder->CreateLoad(
          //              argValue->getType()->getNonOpaquePointerElementType(),
          //              argValue);
          //          state.Builder->CreateStore(loadInst, CalleeF->getArg(i));
        } else {
          state.Builder->CreateStore(argValue, CalleeF->getArg(i));
        }
      }
    }
  }

  return call;
}
