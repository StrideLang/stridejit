//#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
//#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
//#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
//#include "llvm/ExecutionEngine/Orc/Core.h"
//#include "llvm/ExecutionEngine/Orc/EPCIndirectionUtils.h"
//#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
//#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
//#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
//#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
//#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
//#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
//#include "llvm/ExecutionEngine/SectionMemoryManager.h"
//#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
//#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
//#include "llvm/Transforms/InstCombine/InstCombine.h"
//#include "llvm/Transforms/Scalar.h"
//#include "llvm/Transforms/Scalar/GVN.h"

#include "exprast.hpp"
#include "functionast.hpp"
#include "listexprast.hpp"
#include "stridecompiler.hpp"

#include "blocknode.h"
#include "valuenode.h"

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
  llvm::Function *TheFunction = state.getFunctionInModule(P.getName());
  if (!TheFunction) {
    P.callType = callType;
    TheFunction = P.codegen(state);
    TheFunction->dump();
    state.FunctionProtos[Proto->getName()] = std::move(Proto);
  }
  // If this is an operator, install it.
  if (P.isBinaryOp())
    state.BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();
  // Create a new basic block to start insertion into.
  llvm::BasicBlock *BB =
      llvm::BasicBlock::Create(*state.TheContext, "entry", TheFunction);
  state.Builder->SetInsertPoint(BB);
  // Record the function arguments in the NamedValues map.
  state.NamedValues.clear();
  state.PortBlockMap.clear();

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

  auto defineInternal = [&]() {
    for (const auto &decl : internalVariables) {
      // TODO avoid namespace clashes
      if (state.NamedValues.find(std::string(decl->getName())) ==
          state.NamedValues.end()) {
        // Create an alloca for this variable.
        llvm::Type *type;
        if (decl->getObjectType() == "signal") {
          auto typeNode = decl->getPropertyValue("type");
          if (typeNode && typeNode->getNodeType() == AST::Block) {
            auto typeBlockName =
                std::static_pointer_cast<BlockNode>(typeNode)->getName();
            if (state.typesMap.find(typeBlockName) != state.typesMap.end()) {
              type = state.typesMap[typeBlockName];
            } else {
              std::cerr << "Unknown type " << typeBlockName
                        << " . Falling back on double" << std::endl;
            }
          } else {
            std::cerr << "Undefined type. Falling back on double" << std::endl;
            type = llvm::Type::getDoubleTy(*state.TheContext);
          }
        } else if (decl->getObjectType() == "switch") {
          type = state.typesMap["_SwitchType"];
        } else {
          std::cerr << "Invalid declaration for block '" << decl->getName()
                    << "' . Ignoring" << std::endl;
          continue;
        }
        llvm::AllocaInst *Alloca =
            state.CreateEntryBlockAlloca(TheFunction, decl->getName(), type);
        state.NamedValues[decl->getName()] = Alloca;
      } else {
      }
    }
  };

  // Pre Body

  // For loops
  std::map<std::string, llvm::Value *> OldVals;
  std::map<std::string, llvm::PHINode *> PHIVariables;
  llvm::BasicBlock *LoopBB;

  if (callType == CallableType::DomainFunction) {
    defineInternal();
    if (state.hasConfiguration(StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL)) {

      llvm::BasicBlock *EntryBB = &TheFunction->getEntryBlock();
      state.Builder->SetInsertPoint(EntryBB);
      EntryBB->dump();
      auto arg = TheFunction->getArg(TheFunction->arg_size() - 1);
      arg->dump();
      arg->getType()->dump();
      int argCounter = 0;
      for (const auto &externalArg : P.getExternalArgs()) {
        //        auto alloca =
        //        state.Builder->CreateAlloca(externalArg.llvmType, nullptr,
        //                                                  externalArg.name);
        std::vector<llvm::Value *> idxList;
        idxList.push_back(
            llvm::ConstantInt::get(*state.TheContext, llvm::APInt(64, 0)));
        idxList.push_back(llvm::ConstantInt::get(*state.TheContext,
                                                 llvm::APInt(32, argCounter)));
        auto out = state.Builder->CreateGEP(
            arg->getType()->getNonOpaquePointerElementType(), arg, idxList);
        out = state.Builder->CreateLoad(externalArg.llvmType, out,
                                        externalArg.name);

        state.NamedValues[externalArg.name] = out;
        argCounter++;
      }
    }
  } else if (callType == CallableType::Loop) {
    llvm::Function *TheFunction = state.Builder->GetInsertBlock()->getParent();
    llvm::BasicBlock *PreheaderBB = state.Builder->GetInsertBlock();
    LoopBB = llvm::BasicBlock::Create(*state.TheContext, "loop", TheFunction);

    // Insert an explicit fall through from the current block to the LoopBB.
    state.Builder->CreateBr(LoopBB);
    // Start insertion in LoopBB.
    state.Builder->SetInsertPoint(LoopBB);
    { // Define internal vars as PHI
      for (const auto &decl : internalVariables) {
        // Create an alloca for this variable.
        llvm::Type *type;
        llvm::Value *defaultValue;
        if (decl->getObjectType() == "signal") {
          auto defaultNode = decl->getPropertyValue("default");
          if (!defaultNode) {
            std::cerr << "No default provided for internal variable."
                      << std::endl;
            continue;
          }
          auto typeNode = decl->getPropertyValue("type");
          if (typeNode && typeNode->getNodeType() == AST::Block) {
            auto typeBlockName =
                std::static_pointer_cast<BlockNode>(typeNode)->getName();
            if (state.typesMap.find(typeBlockName) != state.typesMap.end()) {
              type = state.typesMap[typeBlockName];
            } else {
              std::cerr << "Unknown type " << typeBlockName
                        << " . Falling back on double" << std::endl;
            }
            if (typeBlockName == "_RealType") {
              double val =
                  std::static_pointer_cast<ValueNode>(defaultNode)->toReal();
              defaultValue = llvm::ConstantFP::get(
                  llvm::Type::getDoubleTy(*state.TheContext),
                  llvm::APFloat(val));

            } else if (typeBlockName == "_IntType") {
              if (defaultNode->getNodeType() == AST::Int) {
                // TODO determine best int for this case.
                int64_t val = std::static_pointer_cast<ValueNode>(defaultNode)
                                  ->getIntValue();
                defaultValue = llvm::ConstantInt::get(
                    llvm::Type::getInt32Ty(*state.TheContext),
                    llvm::APInt(32, val));
              } else if (defaultNode->getNodeType() == AST::Real) {
                double val = std::static_pointer_cast<ValueNode>(defaultNode)
                                 ->getRealValue();

                defaultValue = llvm::ConstantInt::get(
                    llvm::Type::getInt32Ty(*state.TheContext),
                    llvm::APInt(32, int32_t(val)));
              }
            }
          } else {
            std::cerr << "Undefined type. Falling back on double" << std::endl;
            assert(0 == 1);
            type = llvm::Type::getDoubleTy(*state.TheContext);
          }
          // Start the PHI node with an entry for Start.
          std::string VarName = decl->getName();
          llvm::PHINode *Variable = state.Builder->CreatePHI(type, 2, VarName);
          Variable->addIncoming(defaultValue, PreheaderBB);

          PHIVariables[VarName] = Variable;
          OldVals[VarName] = state.NamedValues[VarName];
          state.NamedValues[VarName] = Variable;

        } else if (decl->getObjectType() == "switch") {
          type = state.typesMap["_SwitchType"];
          auto defaultNode = decl->getPropertyValue("default");
          if (defaultNode) {
            if (defaultNode->getNodeType() == AST::Switch) {
              bool val = std::static_pointer_cast<ValueNode>(defaultNode)
                             ->getSwitchValue();
              defaultValue = llvm::ConstantInt::get(
                  llvm::Type::getInt1Ty(*state.TheContext),
                  llvm::APInt(1, val ? 1 : 0));
            } else {
              std::cerr << "Invalid default for switch" << std::endl;
            }
          } else {
            std::cerr << "No default for switch" << std::endl;
          }
        } else {
          std::cerr << "Invalid declaration for block '" << decl->getName()
                    << "' . Ignoring" << std::endl;
          continue;
        }
        // Start the PHI node with an entry for Start.
        std::string VarName = decl->getName();
        llvm::PHINode *Variable = state.Builder->CreatePHI(type, 2, VarName);
        Variable->addIncoming(defaultValue, PreheaderBB);

        PHIVariables[VarName] = Variable;
        OldVals[VarName] = state.NamedValues[VarName];
        state.NamedValues[VarName] = Variable;
      }
    }
  } else {
    defineInternal();
  }

  for (const auto &statement : Body) {
    if (!statement) {
      return nullptr;
    }
    llvm::Value *RetVal = statement->codegen(state);
    //    if (!RetVal) {
    //      // Error reading body, remove function.
    //      TheFunction->eraseFromParent();
    //      if (P.isBinaryOp())
    //        state.BinopPrecedence.erase(P.getOperatorName());
    //      return nullptr;
    //    }
  }

  // Post Body
  if (callType == CallableType::Loop) {

    //    // Emit the step value.
    //    llvm::Value *StepVal = nullptr;
    //    if (Step) {
    //      StepVal = Step->codegen();
    //      if (!StepVal)
    //        return nullptr;
    //    } else {
    //      // If not specified, use 1.0.
    //      StepVal = llvm::ConstantFP::get(*state.TheContext,
    //      llvm::APFloat(1.0));
    //    }

    //    llvm::Value *NextVar = state.Builder->CreateFAdd(Variable, StepVal,
    //    "nextvar");
    // Compute the end condition.
    llvm::Value *EndCond;
    //    llvm::Value *EndCond = End->codegen();
    //    if (!EndCond)
    //      return nullptr;

    // Convert condition to a bool by comparing non-equal to 0.0.
    //        EndCond = Builder->CreateFCmpONE(
    //            EndCond, llvm::ConstantFP::get(*TheContext,
    //            llvm::APFloat(0.0)), "loopcond");

    // Create the "after loop" block and insert it.
    llvm::BasicBlock *LoopEndBB = state.Builder->GetInsertBlock();
    llvm::BasicBlock *AfterBB =
        llvm::BasicBlock::Create(*state.TheContext, "afterloop", TheFunction);

    if (terminateWhenName.size() > 0) {
      EndCond = state.NamedValues[terminateWhenName];
    } else {
      // TODO need to define EndCond
      assert(0 == 1);
    }
    // Insert the conditional branch into the end of LoopEndBB.
    state.Builder->CreateCondBr(EndCond, AfterBB, LoopBB);
    // Any new code will be inserted in AfterBB.
    state.Builder->SetInsertPoint(AfterBB);
    for (const auto &oldVal : OldVals) {
      // Add a new entry to the PHI node for the backedge.
      PHIVariables[oldVal.first]->addIncoming(state.NamedValues[oldVal.first],
                                              LoopEndBB);
      // Restore the unshadowed variable.
      if (oldVal.second)
        state.NamedValues[oldVal.first] = oldVal.second;
      else
        state.NamedValues.erase(oldVal.first);
    }

    // for expr always returns 0.0.
    //    return
    //    llvm::Constant::getNullValue(llvm::Type::getDoubleTy(*TheContext));
  }
  auto *outVal = llvm::ConstantInt::get(state.Builder->getInt32Ty(), 0, true);
  state.Builder->CreateRet(outVal);
  // Validate the generated code, checking for consistency.
  verifyFunction(*TheFunction);
  TheFunction->dump();
  return TheFunction;
}

std::vector<PrototypeArg> PrototypeAST::getExternalArgs() const {
  return ExternalArgs;
}

std::vector<PrototypeArg> PrototypeAST::getUsedPortProperties() const {
  return UsedPortProperties;
}

llvm::Function *PrototypeAST::codegen(StrideCompiler &state) {
  // Make the function type:  double(double,double) etc.
  std::vector<llvm::Type *> ProtoArguments;
  if (callType == CallableType::DomainFunction &&
      state.hasConfiguration(StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL)) {
    auto structType =
        llvm::StructType::create(*state.TheContext, "DomainInStructType");
    std::vector<llvm::Type *> elements;
    for (const auto &arg : ExternalArgs) {
      elements.push_back(arg.llvmType);
      //      auto out = state.Builder->CreateGEP(ptr, idxList, name);
      //      state.Builder->CreateLoad(arg.llvmType, out, arg.name);
    }
    structType->setBody(elements);

    //    ExternalArgs.push_back(
    //        PrototypeArg{"DomainArgs", llvm::PointerType::get(structType,
    //        0)});
    ProtoArguments.emplace_back(llvm::PointerType::get(structType, 0));
  } else {
    for (const auto &arg : OutArgs) {
      ProtoArguments.emplace_back(arg.llvmType);
    }
    for (const auto &arg : Args) {
      ProtoArguments.emplace_back(arg.llvmType);
    }
    for (const auto &arg : ExternalArgs) {
      ProtoArguments.emplace_back(arg.llvmType);
    }
    for (const auto &arg : UsedPortProperties) {
      ProtoArguments.emplace_back(arg.llvmType);
    }
  }

  llvm::FunctionType *FT = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(*state.TheContext), ProtoArguments, false);

  llvm::Function *F = llvm::Function::Create(
      FT, llvm::Function::ExternalLinkage, Name, state.TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args()) {
    if (callType == CallableType::DomainFunction &&
        state.hasConfiguration(StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL)) {
      Arg.setName("DomainPackedArgs");
    } else if (Idx < OutArgs.size()) {
      Arg.setName(OutArgs[Idx].name);
    } else if (Idx < (OutArgs.size() + Args.size())) {
      Arg.setName(Args[Idx - OutArgs.size()].name);
    } else if (Idx < (OutArgs.size() + Args.size() + ExternalArgs.size())) {
      Arg.setName(ExternalArgs[Idx - (OutArgs.size() + Args.size())].name);
    } else {
      Arg.setName(UsedPortProperties[Idx - (OutArgs.size() + Args.size() +
                                            ExternalArgs.size())]
                      .name);
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
          value->getType()->getNonOpaquePointerElementType(), value, "");
    }
    if (!value->getType()->isPointerTy() && arg->getType()->isPointerTy()) {
      ArgsV = state.Builder->CreateAlloca(value->getType(), nullptr, "Temp");
      state.Builder->CreateStore(value, ArgsV);
    }
    //    value->dump();
    //    arg->dump();
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
      llvm::Value *newArgVal = nullptr;
      if (auto varExpr = dynamic_cast<VariableExprAST *>(OutArgs[i].get())) {
        auto indeces = varExpr->getIndeces();
        if (indeces.size() > 0) {
          // FIXME support ranges
          std::vector<llvm::Value *> idxList;
          auto idx = indeces[0];
          const size_t *intIdx = std::get_if<size_t>(&idx);
          if (intIdx) {
            idxList.push_back(llvm::ConstantInt::get(*state.TheContext,
                                                     llvm::APInt(64, *intIdx)));
          }
          const std::string *strIdx = std::get_if<std::string>(&idx);
          if (strIdx) {
            idxList.push_back(state.NamedValues[*strIdx]);
          }

          auto *GEP = state.Builder->CreateGEP(
              value->getType()->getNonOpaquePointerElementType(), value,
              idxList);
          value = GEP;
          //          value = state.Builder->CreateLoad(
          //              value->getType()->getNonOpaquePointerElementType(),
          //              GEP, varExpr->getName());
        } else {
          //          value = state.Builder->CreateLoad(
          //              value->getType()->getNonOpaquePointerElementType(),
          //              value);
        }
        newArgVal = value;
      } else {
        newArgVal = func(value, CalleeF->getArg(i), state);
      }
      if (!newArgVal)
        return nullptr;
      CallArgs.push_back(std::move(newArgVal));
      if (CallArgs.back()->getType()->isPointerTy() &&
          !CalleeF->getArg(i)->getType()->isPointerTy()) {
        CallArgs.back() = state.Builder->CreateLoad(
            llvm::Type::getDoubleTy(*state.TheContext), CallArgs.back(), "");
      }
    }
  }

  if (callType == CallableType::Module || callType == CallableType::Loop ||
      callType == CallableType::External) {
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
                CallArgs.back()->getType()->getNonOpaquePointerElementType(),
                CallArgs.back(), "");
          }
        }
      } else {
        llvm::Value *newArgVal = nullptr;
        if (auto varExpr = dynamic_cast<VariableExprAST *>(InArgs[i].get())) {
          auto indeces = varExpr->getIndeces();
          if (indeces.size() > 0) {
            // FIXME support ranges
            std::vector<llvm::Value *> idxList;
            auto idx = indeces[0];
            const size_t *intIdx = std::get_if<size_t>(&idx);
            if (intIdx) {
              idxList.push_back(llvm::ConstantInt::get(
                  *state.TheContext, llvm::APInt(64, *intIdx)));
            }
            const std::string *strIdx = std::get_if<std::string>(&idx);
            if (strIdx) {
              idxList.push_back(state.NamedValues[*strIdx]);
            }
            auto *GEP = state.Builder->CreateGEP(
                value->getType()->getNonOpaquePointerElementType(), value,
                idxList);
            value = GEP;
            //            value = state.Builder->CreateLoad(
            //                value->getType()->getNonOpaquePointerElementType(),
            //                GEP, varExpr->getName());
          } else {
            //            value = state.Builder->CreateLoad(
            //                value->getType()->getNonOpaquePointerElementType(),
            //                value);
          }
          newArgVal = value;
        } else {
          newArgVal = func(value, CalleeF->getArg(i), state);
        }
        if (!newArgVal)
          return nullptr;
        CallArgs.push_back(std::move(newArgVal));
        if (CallArgs.back()->getType()->isPointerTy() &&
            !CalleeF->getArg(i)->getType()->isPointerTy()) {
          CallArgs.back() = state.Builder->CreateLoad(
              CallArgs.back()->getType()->getNonOpaquePointerElementType(),
              CallArgs.back(), "");
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
      auto *list = dynamic_cast<ListExprAST *>(OutArgs[i].get());
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
            CallArgs.back()->getType()->getNonOpaquePointerElementType(),
            CallArgs.back(), "");
      }
    }
  }

  for (unsigned i = 0, e = PortPropArgs.size(); i != e; ++i) {
    llvm::Value *value = PortPropArgs[i]->codegen(state);
    CallArgs.push_back(std::move(value));
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
  } else if (callType == CallableType::Loop) {
    call = state.Builder->CreateCall(CalleeF, CallArgs, CalleeF->getName());
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
          //          state.Builder->CreateStore(loadInst,
          //          CalleeF->getArg(i));
        } else {
          state.Builder->CreateStore(argValue, CalleeF->getArg(i));
        }
      }
    }
  }

  return call;
}
