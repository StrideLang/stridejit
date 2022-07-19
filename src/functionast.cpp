#include "functionast.hpp"

#include "exprast.hpp"
#include "strideenvironment.hpp"

#include <iostream>

FunctionAST::FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                         std::unique_ptr<ExprAST> Body)
    : Proto(std::move(Proto)), Body(std::move(Body)) {}

const PrototypeAST &FunctionAST::getProto() const { return *Proto; }

const std::string &FunctionAST::getName() const { return Proto->getName(); }

llvm::Function *FunctionAST::codegen(JitState &state) {
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto &P = *Proto;
  state.FunctionProtos[Proto->getName()] = std::move(Proto);
  llvm::Function *TheFunction = state.getFunction(P.getName());
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
  //  state.TheModule->dump();

  //  auto *out = state.TheModule->getGlobalVariable("Out");

  //  auto *v = state.Builder->CreateLoad(
  //      llvm::Type::getDoubleTy(*state.TheContext), out, "Out");
  //  state.NamedValues["Out"] = v;
  // Add arguments to variable symbol table.

  if (llvm::Value *RetVal = Body->codegen(state)) {
    // Finish off the function.
    //    state.Builder->CreateRet(RetVal);

    auto *outVal = llvm::ConstantInt::get(state.Builder->getInt32Ty(), 0, true);
    state.Builder->CreateRet(outVal);
    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    //    state.TheModule->dump();
    //    state.TheFPM->run(*TheFunction);
    return TheFunction;
  }
  // Error reading body, remove function.
  TheFunction->eraseFromParent();
  if (P.isBinaryOp())
    state.BinopPrecedence.erase(P.getOperatorName());
  return nullptr;
}

llvm::Function *PrototypeAST::codegen(JitState &state) {
  // Make the function type:  double(double,double) etc.
  std::vector<llvm::Type *> Doubles(Args.size(),
                                    llvm::Type::getDoubleTy(*state.TheContext));

  for (int i = 0; i < OutArgs.size(); i++) {
    Doubles.emplace_back(llvm::Type::getDoublePtrTy(*state.TheContext));
  }
  llvm::FunctionType *FT = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(*state.TheContext), Doubles, false);

  llvm::Function *F = llvm::Function::Create(
      FT, llvm::Function::ExternalLinkage, Name, state.TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args()) {
    if (Idx < Args.size()) {
      Arg.setName(Args[Idx]);
    } else {
      Arg.setName(OutArgs[Idx - Args.size()]);
    }
    Idx++;
  }

  return F;
}

llvm::Value *CallExprAST::codegen(JitState &state) {
  std::cout << "CallExprAST codegen" << std::endl;
  // Look up the name in the global module table.
  llvm::Function *CalleeF = state.getFunction(Callee);
  if (!CalleeF) {
    if (Callee == "Sine") {
      //      llvm::FunctionType *doubleDouble = llvm::FunctionType::get(
      //          llvm::Type::getDoubleTy(*state.TheContext),
      //          {llvm::Type::getDoubleTy(*state.TheContext)}, false);
      //      llvm::Function *SinFunction =
      //          llvm::Function::Create(doubleDouble,
      //          llvm::Function::ExternalLinkage,
      //                                 "sin", *state.TheModule);
      //      state.Builder->CreateCall(CalleeF, ArgsV, "sin");
      //          llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
    }

    return state.LogErrorV("Unknown function referenced");
  }

  // If argument mismatch error.
  if (CalleeF->arg_size() != Args.size())
    return state.LogErrorV("Incorrect # arguments passed");

  std::vector<llvm::Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen(state));
    ArgsV.back()->dump();
    ArgsV.back()->getType()->dump();
    if (!ArgsV.back())
      return nullptr;
  }
  CalleeF->dump();
  state.Builder->CreateCall(CalleeF, ArgsV, CalleeF->getName());
  CalleeF->getArg(1)->dump();
  return CalleeF->getArg(1);
}
