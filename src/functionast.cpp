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
  for (const auto &decl : internalVariables) {
    // TODO avoid namespace clashes
    if (state.NamedValues.find(std::string(decl->getName())) ==
        state.NamedValues.end()) {
      // Create an alloca for this variable.
      llvm::AllocaInst *Alloca =
          state.CreateEntryBlockAlloca(TheFunction, decl->getName());
      state.NamedValues[decl->getName()] = Alloca;
    } else {
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

std::vector<PrototypeArg> PrototypeAST::getExternalArgs() const {
  return ExternalArgs;
}

llvm::Function *PrototypeAST::codegen(StrideCompiler &state) {
  // Make the function type:  double(double,double) etc.
  std::vector<llvm::Type *> ProtoArguments;

  for (const auto &arg : OutArgs) {
    ProtoArguments.emplace_back(arg.llvmType);
  }
  for (const auto &arg : Args) {
    ProtoArguments.emplace_back(arg.llvmType);
  }
  for (const auto &arg : ExternalArgs) {
    ProtoArguments.emplace_back(arg.llvmType);
  }
  llvm::FunctionType *FT = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(*state.TheContext), ProtoArguments, false);

  llvm::Function *F = llvm::Function::Create(
      FT, llvm::Function::ExternalLinkage, Name, state.TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args()) {
    if (Idx < OutArgs.size()) {
      Arg.setName(OutArgs[Idx].name);
    } else if (Idx < (OutArgs.size() + Args.size())) {
      Arg.setName(Args[Idx - Args.size()].name);
    } else {
      Arg.setName(ExternalArgs[Idx - (OutArgs.size() + Args.size())].name);
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
    return state.LogErrorV(("Unknown function referenced: " + Callee).c_str());
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

  if (callType == CallableType::Module || callType == CallableType::External) {
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
                llvm::Type::getDoubleTy(*state.TheContext), CallArgs.back(),
                "");
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
  } else if (callType == CallableType::Reaction) {

  } else {
    //    assert(0 == 1);
  }

  for (unsigned i = 0, e = ExternalArgs.size(); i != e; ++i) {
    llvm::Value *value = ExternalArgs[i]->codegen(state);
    if (value->getType()->isTokenTy()) {
      assert(0 == 1); // TODO implement
      //      auto *list = dynamic_cast<ListExprAST *>(OutArgs[i].get());
      //      assert(list);
      //      for (const auto &expr : list->elements()) {
      //        auto newArg = func(expr->codegen(state), CalleeF->getArg(i),
      //        state); if (!newArg)
      //          return nullptr;
      //        CallArgs.push_back(std::move(newArg));
      //        if (CallArgs.back()->getType()->isPointerTy() &&
      //            !CalleeF->getArg(i)->getType()->isPointerTy()) {
      //          CallArgs.back() = state.Builder->CreateLoad(
      //              llvm::Type::getDoubleTy(*state.TheContext),
      //              CallArgs.back(), "");
      //        }
      //      }

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
  llvm::CallInst *call;

  if (callType == CallableType::Reaction) {
    llvm::Value *CondV = InArgs[0]->codegen(state);
    CondV->dump();
    // Convert condition to a bool by comparing non-equal to 0.0.
    if (CondV->getType()->isDoubleTy()) {

      CondV = state.Builder->CreateFCmpONE(
          CondV, llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(0.0)),
          "ifcond");

    } else if (CondV->getType()->isIntegerTy()) {
      CondV = state.Builder->CreateICmpNE(
          CondV, llvm::ConstantInt::get(*state.TheContext, llvm::APInt(1, 0)),
          "ifcond");
    }
    llvm::Function *TheFunction = state.Builder->GetInsertBlock()->getParent();
    llvm::BasicBlock *ThenBB =
        llvm::BasicBlock::Create(*state.TheContext, "then", TheFunction);
    llvm::BasicBlock *MergeBB =
        llvm::BasicBlock::Create(*state.TheContext, "ifcont", TheFunction);

    state.Builder->CreateCondBr(CondV, ThenBB, MergeBB);

    state.Builder->SetInsertPoint(ThenBB);
    call = state.Builder->CreateCall(CalleeF, CallArgs, CalleeF->getName());

    state.Builder->CreateBr(MergeBB);
    ThenBB = state.Builder->GetInsertBlock();
    state.Builder->SetInsertPoint(MergeBB);
  } else {
    call = state.Builder->CreateCall(CalleeF, CallArgs, CalleeF->getName());
  }

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
