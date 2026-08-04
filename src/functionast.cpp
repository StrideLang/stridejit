// #include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
// #include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
// #include "llvm/ExecutionEngine/Orc/CompileUtils.h"
// #include "llvm/ExecutionEngine/Orc/Core.h"
// #include "llvm/ExecutionEngine/Orc/EPCIndirectionUtils.h"
// #include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
// #include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
// #include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
// #include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
// #include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
// #include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
// #include "llvm/ExecutionEngine/SectionMemoryManager.h"
// #include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
// #include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
// #include "llvm/Transforms/InstCombine/InstCombine.h"
// #include "llvm/Transforms/Scalar.h"
// #include "llvm/Transforms/Scalar/GVN.h"

#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/functionast.hpp"
#include "stride/stridejit/listexprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include "stride/parser/ast.h"
#include "stride/parser/blocknode.h"
#include "stride/parser/valuenode.h"

#include "stride/utils/astquery.h"

#include <iostream>

using namespace strd;

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

void FunctionAST::allocateInternalVariables(StrideCompiler &state,
                                            llvm::Function *TheFunction) {
  // for (const auto &arg :
  //      state.FunctionProtos[std::string(TheFunction->getName())]
  //          ->getInternalArgs()) {
  //   llvm::AllocaInst *Alloca =
  //       state.CreateEntryBlockAlloca(TheFunction, arg.name, arg.llvmType);
  //   state.NamedValues[arg.name] = {Alloca, arg.llvmType};
  // }
  // Allocate non-persistent local internal variables on the stack
  for (const auto &decl : internalVariables) {
    std::string varName = decl->getName();
    auto defaultNode = decl->getPropertyValue("default");
    if (defaultNode) {
      if (decl->getNodeType() == AST::Declaration) {
        llvm::Type *type = state.getLLVMType(decl);
        // Create entry block stack allocation
        llvm::AllocaInst *alloca =
            state.CreateEntryBlockAlloca(TheFunction, varName, type);
        // Register in NamedValues so VariableExprAST and assignments locate the
        // alloca
        state.NamedValues[varName] = {alloca, type};

        // Set default values. Should be initialized on every function call as
        // they are not persistent
        // Should we optmize here if values are not modified, or just defer to
        // the compiler?
        if (defaultNode->getNodeType() == AST::Int) {
          llvm::Value *intVal = state.Builder->getInt64(
              std::static_pointer_cast<ValueNode>(defaultNode)->getIntValue());
          state.Builder->CreateStore(intVal, alloca);
        } else if (defaultNode->getNodeType() == AST::Real) {
          llvm::Value *realVal = llvm::ConstantFP::get(
              *state.TheContext,
              llvm::APFloat(std::static_pointer_cast<ValueNode>(defaultNode)
                                ->getRealValue()));
          state.Builder->CreateStore(realVal, alloca);
        } else {
          std::cerr << __FILE__ << ":" << __LINE__
                    << " ERROR: type not supported for default" << std::endl;
          assert(0 == 1);
        }
      } else if (decl->getNodeType() == AST::BundleDeclaration) {
        llvm::Type *Int32Ty = state.Builder->getInt32Ty();
        int size = ASTQuery::getBlockDeclaredSize(decl, {}, nullptr);
        llvm::Type *type = state.getLLVMType(decl);

        // TODO support non-literal bundle sizes. We need to have the tree here,
        // or have previously passed the size so that we know it here.
        llvm::AllocaInst *alloca =
            state.CreateEntryBlockAllocaArray(TheFunction, varName, type, size);
        // TODO optmize when possible for const arrays.
        // llvm::AllocaInst *alloca =
        //     state.CreateEntryBlockAllocaArrayConst(TheFunction, varName,
        //     type, size);
        if (defaultNode->getNodeType() == AST::Int) {
          assert(0 == 1); // TODO implement
          auto functionName = TheFunction->getName();

          llvm::BasicBlock *EntryBB = &TheFunction->getEntryBlock();
          state.Builder->SetInsertPoint(EntryBB);

          // 2. Create Basic Blocks for the Loop
          llvm::BasicBlock *CondBB = llvm::BasicBlock::Create(
              *state.TheContext, functionName + "_loop.cond", TheFunction);
          llvm::BasicBlock *BodyBB = llvm::BasicBlock::Create(
              *state.TheContext, functionName + "_loop.body", TheFunction);
          llvm::BasicBlock *IncBB = llvm::BasicBlock::Create(
              *state.TheContext, functionName + "_loop.inc", TheFunction);
          llvm::BasicBlock *EndBB = llvm::BasicBlock::Create(
              *state.TheContext, functionName + "_loop.end", TheFunction);

          // Jump from entry into the condition block
          state.Builder->CreateBr(CondBB);

          // --- CONDITION BLOCK (Host for the PHI Node) ---
          state.Builder->SetInsertPoint(CondBB);

          // Create the PHI node for loop index 'i'
          // It takes 2 incoming values: one from EntryBB, one from IncBB
          llvm::PHINode *CurrI = state.Builder->CreatePHI(Int32Ty, 2, "i");
          CurrI->addIncoming(state.Builder->getInt32(0),
                             EntryBB); // Initial value: i = 0

          // TODO handle dynamic sizes
          // Condition check:
          llvm::Value *Cmp = state.Builder->CreateICmpSLT(
              CurrI, state.Builder->getInt32(size), "cmp");
          state.Builder->CreateCondBr(Cmp, BodyBB, EndBB);

          // --- BODY BLOCK ---
          state.Builder->SetInsertPoint(BodyBB);

          llvm::Value *Val = state.Builder->getInt64(
              std::static_pointer_cast<ValueNode>(defaultNode)->getIntValue());

          // Compute pointer to arr[i] using two indices {0, i}
          llvm::Value *IdxList[] = {state.Builder->getInt32(0), CurrI};
          llvm::Value *ElemPtr = state.Builder->CreateInBoundsGEP(
              type, alloca, IdxList, "elem.ptr");

          // Store value into array memory
          state.Builder->CreateStore(Val, ElemPtr);
          state.Builder->CreateBr(IncBB);

          // --- INCREMENT BLOCK ---
          state.Builder->SetInsertPoint(IncBB);

          // next_i = i + 1
          llvm::Value *NextI = state.Builder->CreateAdd(
              CurrI, state.Builder->getInt32(1), "next.i");

          // Wire up the 2nd incoming value for the PHI node back in CondBB
          CurrI->addIncoming(NextI, IncBB);

          state.Builder->CreateBr(CondBB);

          // --- END BLOCK ---
          state.Builder->SetInsertPoint(EndBB);

        } else if (defaultNode->getNodeType() == AST::Real) {
          assert(0 == 1); // TODO implement
          // llvm::Value *realVal = llvm::ConstantFP::get(
          //     *state.TheContext,
          //     llvm::APFloat(std::static_pointer_cast<ValueNode>(defaultNode)
          //                       ->getRealValue()));
          // state.Builder->CreateStore(realVal, alloca);
        } else if (defaultNode->getNodeType() == AST::List) {

          for (int i = 0; i < size; i++) {
            llvm::Value *Val = nullptr;
            if (type == state.Builder->getInt32Ty()) {
              Val = state.Builder->getInt32(std::static_pointer_cast<ValueNode>(
                                                defaultNode->getChildren()[i])
                                                ->getIntValue());

            } else if (type == state.Builder->getDoubleTy()) {
              Val = llvm::ConstantFP::get(
                  *state.TheContext,
                  llvm::APFloat(std::static_pointer_cast<ValueNode>(
                                    defaultNode->getChildren()[i])
                                    ->getRealValue()));
            }
            assert(Val != nullptr);

            // Compute pointer to arr[i] using two indices {0, i}
            llvm::Value *IdxList[] = {state.Builder->getInt32(0),
                                      state.Builder->getInt32(i)};
            llvm::ArrayType *arrayTy = llvm::ArrayType::get(type, size);
            llvm::Value *ElemPtr = state.Builder->CreateInBoundsGEP(
                arrayTy, alloca, IdxList, "elem.ptr");

            // Store value into array memory
            state.Builder->CreateStore(Val, ElemPtr);
          }

        } else {
          std::cerr << __FILE__ << ":" << __LINE__
                    << " ERROR: type not supported for default" << std::endl;
          assert(0 == 1);
        }

        // Register in NamedValues so VariableExprAST and assignments locate the
        // alloca
        state.NamedValues[varName] = {alloca, type};
      }
    }
  }
}

llvm::Function *FunctionAST::codegen(StrideCompiler &state) {

  std::cout << " == FunctionAST codegen : " << Proto->getName() << std::endl;
  // Record the function arguments in the NamedValues map.
  state.NamedValues.clear();
  state.PortBlockMap.clear();
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto &P = *Proto;
  llvm::Function *TheFunction = state.getFunctionInModule(P.getName());
  if (!TheFunction) {
    P.callType = callType;
    TheFunction = P.codegen(state);
    TheFunction->print(llvm::outs());
    llvm::outs() << "\n";
    state.FunctionProtos[Proto->getName()] = std::move(Proto);
  }
  // If this is an operator, install it.
  if (P.isBinaryOp())
    state.BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();
  // Create a new basic block to start insertion into.
  llvm::BasicBlock *BB =
      llvm::BasicBlock::Create(*state.TheContext, "entry", TheFunction);
  state.Builder->SetInsertPoint(BB);

  auto argTypes = P.getUsedArgsTypes();

  int i = 0;
  for (auto &Arg : TheFunction->args()) {
    auto argType = argTypes[i];
    // TODO verify how this interacts with the other NamedValues setting in
    // allocateInternalVariables and other places
    state.NamedValues[std::string(Arg.getName())] = {&Arg, argType};
    i++;
    std::cout << std::string(Arg.getName()) << ", ";
  }
  std::cout << std::endl;

  // Pre Body
  // Before actual function code there is some work done to loops to manage the
  // iteration and to functions that have packed arguments, to unpack them. For
  // everything else, local variables need to be allocated

  // For loops
  std::map<std::string, llvm::Value *> OldVals;
  std::map<std::string, llvm::PHINode *> PHIVariables;
  llvm::BasicBlock *LoopBB;
  int64_t itStart = 0, itLimit = 0, itIncrement = 0;
  std::string itName;

  if (callType == CallableType::DomainFunction) {
    allocateInternalVariables(state, TheFunction);
    if (state.hasConfiguration(StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL)) {

      llvm::BasicBlock *EntryBB = &TheFunction->getEntryBlock();
      state.Builder->SetInsertPoint(EntryBB);
      EntryBB->print(llvm::outs());
      auto arg = TheFunction->getArg(TheFunction->arg_size() - 1);
      arg->print(llvm::outs());
      arg->getType()->print(llvm::outs());
      llvm::outs() << "\n";
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
        llvm::Value *out =
            state.Builder->CreateGEP(arg->getType(), arg, idxList);
        out = state.Builder->CreateLoad(externalArg.llvmType, std::move(out),
                                        externalArg.name);

        state.NamedValues[externalArg.name] = {out, externalArg.llvmType};
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
    { // Define internal vars as PHI, needed for SSA (single static assignment)
      // when having branching code.
      for (const auto &decl : internalVariables) {
        llvm::Type *type;
        llvm::Value *defaultValue = nullptr;
        if (decl->getObjectType() == "signal") {
          //  Local signals in loops are reset on every trigger, so they are
          //  allocated and set to their default value
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
              } else {
                std::cerr << __FILE__ << ":" << __LINE__
                          << "Unsupported type. Falling back on double"
                          << std::endl;
                assert(0 == 1);
                type = llvm::Type::getDoubleTy(*state.TheContext);
                defaultValue = llvm::ConstantFP::get(
                    llvm::Type::getDoubleTy(*state.TheContext),
                    llvm::APFloat(0.0));
              }
            } else {
              std::cerr << __FILE__ << ":" << __LINE__
                        << "Unsupported type. Falling back on double"
                        << std::endl;
              assert(0 == 1);
              type = llvm::Type::getDoubleTy(*state.TheContext);
              defaultValue = llvm::ConstantFP::get(
                  llvm::Type::getDoubleTy(*state.TheContext),
                  llvm::APFloat(0.0));
            }
          } else {
            std::cerr << __FILE__ << ":" << __LINE__
                      << "Unsupported type. Falling back on double"
                      << std::endl;
            assert(0 == 1);
            type = llvm::Type::getDoubleTy(*state.TheContext);
            defaultValue = llvm::ConstantFP::get(
                llvm::Type::getDoubleTy(*state.TheContext), llvm::APFloat(0.0));
          }
          // Start the PHI node with an entry for Start.
          std::string VarName = decl->getName();
          llvm::PHINode *Variable = state.Builder->CreatePHI(type, 2, VarName);
          assert(defaultValue);
          Variable->addIncoming(defaultValue, PreheaderBB);

          PHIVariables[VarName] = Variable;
          OldVals[VarName] = state.NamedValues[VarName].first;
          state.NamedValues[VarName] = {Variable, type};

        } else if (decl->getObjectType() == "switch") {
          //  Local switches in loops are reset on every trigger, so they are
          //  allocated and set to their default value
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
        } else if (decl->getObjectType() == "iterator") {
          std::cout << "iterator" << std::endl;
          auto startNode = decl->getPropertyValue("default");
          auto limitNode = decl->getPropertyValue("limit");
          auto incrementNode = decl->getPropertyValue("increment");
          if (startNode && startNode->getNodeType() == AST::Int && limitNode &&
              limitNode->getNodeType() == AST::Int && incrementNode &&
              incrementNode->getNodeType() == AST::Int) {
            itName = decl->getName();
            itStart =
                std::static_pointer_cast<ValueNode>(startNode)->getIntValue();
            itLimit =
                std::static_pointer_cast<ValueNode>(limitNode)->getIntValue();
            itIncrement = std::static_pointer_cast<ValueNode>(incrementNode)
                              ->getIntValue();
          }

          type = llvm::IntegerType::get(*state.TheContext, 32);
          defaultValue =
              llvm::ConstantInt::get(llvm::Type::getInt32Ty(*state.TheContext),
                                     llvm::APInt(32, int32_t(itStart)));
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
        OldVals[VarName] = state.NamedValues[VarName].first;
        state.NamedValues[VarName] = {Variable, type};
      }

      for (const auto &portPropArg : P.getUsedPortProperties()) {
        // state.NamedValues[externalArg.name] = {externalArg,
        // externalArg.llvmType};
      }
    }
  } else {
    allocateInternalVariables(state, TheFunction);
  }

  // Generate the function body
  for (const auto &statement : Body) {
    if (!statement) {
      return nullptr;
    }
    auto [RetVal, retType] = statement->codegen(state);
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
    llvm::Value *EndCond = nullptr;
    //    llvm::Value *EndCond = End->codegen();
    //    if (!EndCond)
    //      return nullptr;

    // Convert condition to a bool by comparing non-equal to 0.0.
    //        EndCond = Builder->CreateFCmpONE(
    //            EndCond, llvm::ConstantFP::get(*TheContext,
    //            llvm::APFloat(0.0)), "loopcond");

    if (terminateWhenName.size() > 0) {
      EndCond = state.NamedValues[terminateWhenName].first;
    } else if (itIncrement != 0) {
      auto *newIteratorValue = state.Builder->CreateAdd(
          PHIVariables[itName],
          llvm::ConstantInt::get(*state.TheContext,
                                 llvm::APInt(32, itIncrement)));
      EndCond = state.Builder->CreateICmpEQ(
          newIteratorValue,
          llvm::ConstantInt::get(*state.TheContext,
                                 llvm::APInt(itLimit, itLimit)));
    } else {
      assert(0 == 1);
    }

    // Create the "after loop" block and insert it.
    llvm::BasicBlock *LoopEndBB = state.Builder->GetInsertBlock();
    llvm::BasicBlock *AfterBB =
        llvm::BasicBlock::Create(*state.TheContext, "afterloop", TheFunction);

    // Insert the conditional branch into the end of LoopEndBB.
    state.Builder->CreateCondBr(EndCond, AfterBB, LoopBB);
    // Any new code will be inserted in AfterBB.
    state.Builder->SetInsertPoint(AfterBB);
    for (const auto &oldVal : OldVals) {
      // Add a new entry to the PHI node for the backedge.
      PHIVariables[oldVal.first]->addIncoming(
          state.NamedValues[oldVal.first].first, LoopEndBB);
      // Restore the unshadowed variable.
      if (oldVal.second)
        state.NamedValues[oldVal.first] = {oldVal.second, std::nullopt};
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
  TheFunction->print(llvm::outs());
  llvm::outs() << "\n";
  return TheFunction;
}

std::vector<PrototypeArg> PrototypeAST::getExternalArgs() const {
  return ExternalArgs;
}

std::vector<PrototypeArg> PrototypeAST::getInternalArgs() const {
  return InternalPersistentArgs;
}

std::vector<PrototypeArg> PrototypeAST::getUsedPortProperties() const {
  return UsedPortProperties;
}

llvm::Function *PrototypeAST::codegen(StrideCompiler &state) {
  // Make the function type:  double(double,double) etc.

  std::cout << " == PrototypeAST codegen" << std::endl;
  std::vector<llvm::Type *> ProtoArguments;
  if (callType == CallableType::DomainFunction &&
      state.hasConfiguration(StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL)) {
    auto structType =
        llvm::StructType::create(*state.TheContext, "DomainInStructType");
    std::vector<llvm::Type *> elements;
    for (const auto &arg : ExternalArgs) {
      elements.push_back(llvm::PointerType::get(arg.llvmType, 0));
      //      auto out = state.Builder->CreateGEP(ptr, idxList, name);
      //      state.Builder->CreateLoad(arg.llvmType, out, arg.name);
    }
    structType->setBody(elements);

    //    ExternalArgs.push_back(
    //        PrototypeArg{"DomainArgs", llvm::PointerType::get(structType,
    //        0)});
    ProtoArguments.emplace_back(llvm::PointerType::get(*state.TheContext, 0));
  } else {
    for (const auto &arg : OutArgs) {
      ProtoArguments.emplace_back(llvm::PointerType::get(*state.TheContext, 0));
    }
    for (const auto &arg : InArgs) {
      ProtoArguments.emplace_back(llvm::PointerType::get(*state.TheContext, 0));
    }
    for (const auto &arg : ExternalArgs) {
      ProtoArguments.emplace_back(llvm::PointerType::get(*state.TheContext, 0));
    }
    for (const auto &arg : InternalPersistentArgs) {
      ProtoArguments.emplace_back(llvm::PointerType::get(*state.TheContext, 0));
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
    } else if (Idx < (OutArgs.size() + InArgs.size())) {
      Arg.setName(InArgs[Idx - OutArgs.size()].name);
    } else if (Idx < (OutArgs.size() + InArgs.size() +
                      InternalPersistentArgs.size())) {
      Arg.setName(
          InternalPersistentArgs[Idx - (OutArgs.size() + InArgs.size())].name);
    } else if (Idx < (OutArgs.size() + InArgs.size() +
                      InternalPersistentArgs.size() + ExternalArgs.size())) {
      Arg.setName(ExternalArgs[Idx - (OutArgs.size() + InArgs.size() +
                                      InternalPersistentArgs.size())]
                      .name);
    } else {
      Arg.setName(UsedPortProperties[Idx - (OutArgs.size() + InArgs.size() +
                                            InternalPersistentArgs.size() +
                                            ExternalArgs.size())]
                      .name);
    }
    Idx++;
  }
  return F;
}

char PrototypeAST::getOperatorName() const {
  assert(isUnaryOp() || isBinaryOp());
  return Name[Name.size() - 1];
}

std::vector<llvm::Type *> PrototypeAST::getUsedArgsTypes() const {
  std::vector<llvm::Type *> ProtoArguments;

  for (const auto &arg : OutArgs) {
    ProtoArguments.emplace_back(arg.llvmType);
  }
  for (const auto &arg : InArgs) {
    ProtoArguments.emplace_back(arg.llvmType);
  }
  for (const auto &arg : InternalPersistentArgs) {
    ProtoArguments.emplace_back(arg.llvmType);
  }
  for (const auto &arg : ExternalArgs) {
    ProtoArguments.emplace_back(arg.llvmType);
  }
  for (const auto &arg : UsedPortProperties) {
    ProtoArguments.emplace_back(arg.llvmType);
  }
  return ProtoArguments;
}

llvm::Value *func(llvm::Value *value, std::optional<llvm::Type *> type,
                  llvm::Argument *arg, StrideCompiler &state) {
  llvm::Value *ArgsV = value;
  if (value->getType()->isPointerTy() && !arg->getType()->isPointerTy()) {
    if (!type.has_value()) {
      return state.LogErrorV(
          ("Type not provided for pointer: " + std::string(value->getName()))
              .c_str());
    }
    ArgsV = state.Builder->CreateLoad(type.value(), value, "");
  }
  if (!value->getType()->isPointerTy() && arg->getType()->isPointerTy()) {
    ArgsV = state.Builder->CreateAlloca(value->getType(), nullptr,
                                        "LiteralValTemp");
    state.Builder->CreateStore(value, ArgsV);
  }
  //    value->print(llvm::outs());
  //    arg->print(llvm::outs());
  //    ArgsV->print(llvm::outs());
  //    ArgsV->getType()->print(llvm::outs());
  //    llvm::outs() << "\n";
  return ArgsV;
};

void processArgGroup(
    StrideCompiler &state,
    const std::vector<std::unique_ptr<ExprAST>> &ArgGroup,
    llvm::Function *CalleeF,
    std::vector<std::pair<llvm::Value *, std::optional<llvm::Type *>>>
        &CallArgs) {
  for (unsigned i = 0, e = ArgGroup.size(); i != e; ++i) {
    auto [value, type] = ArgGroup[i]->codegen(state);
    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(ArgGroup[i].get());
      assert(list);
      for (const auto &expr : list->elements()) {
        auto [val, type] = expr->codegen(state);
        auto newArg = func(val, type, CalleeF->getArg(i), state);
        if (!newArg) {
          std::cerr << "Can't process argument: "
                    << std::string(value->getName()) << std::endl;
          return;
        }
        CallArgs.push_back(
            std::pair<llvm::Value *, std::optional<llvm::Type *>>{
                std::move(newArg), type});
        if (CallArgs.back().first->getType()->isPointerTy() &&
            !CalleeF->getArg(i)->getType()->isPointerTy()) {
          CallArgs.back() =
              std::pair<llvm::Value *, std::optional<llvm::Type *>>{
                  state.Builder->CreateLoad(
                      llvm::Type::getDoubleTy(*state.TheContext),
                      CallArgs.back().first, ""),
                  std::nullopt};
        }
      }
    } else {
      llvm::Value *newArgVal = nullptr;
      if (auto varExpr = dynamic_cast<VariableExprAST *>(ArgGroup[i].get())) {
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
            idxList.push_back(state.NamedValues[*strIdx].first);
          }
          if (!type.has_value()) {
            std::cerr << "No type for: " << std::string(value->getName())
                      << std::endl;
            return;
          }
          type.value()->print(llvm::outs());
          llvm::outs() << "\n";
          auto *GEP = state.Builder->CreateGEP(type.value(), value, idxList);
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
        newArgVal = func(value, type, CalleeF->getArg(i), state);
      }
      if (!newArgVal) {
        std::cerr << "Can't process argument: " << std::string(value->getName())
                  << std::endl;
        return;
      }
      CallArgs.push_back({std::move(newArgVal), std::nullopt});
      if (CallArgs.back().first->getType()->isPointerTy() &&
          !CalleeF->getArg(i)->getType()->isPointerTy()) {
        assert(type.has_value());
        CallArgs.back() = {
            state.Builder->CreateLoad(type.value(), CallArgs.back().first, ""),
            std::nullopt};
      }
    }
  }
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
CallExprAST::codegen(StrideCompiler &state) {
  // Look up the name in the global module table.
  llvm::Function *CalleeF = state.getFunctionInModule(Callee);
  if (!CalleeF) {
    return {state.LogErrorV(("Unknown function referenced: " + Callee).c_str()),
            std::nullopt};
  }
  std::cout << " == CallExprAST codegen for " << std::string(CalleeF->getName())
            << " -> " << instanceName << std::endl;

  std::vector<std::pair<llvm::Value *, std::optional<llvm::Type *>>> CallArgs;
  processArgGroup(state, OutArgs, CalleeF, CallArgs);

  if (callType == CallableType::Module || callType == CallableType::Loop) {

    int outArgCount = CallArgs.size();
    processArgGroup(state, InArgs, CalleeF, CallArgs);

    // bundle main inputs if not external.
    // Modules and Loops take a single block, so
    // multiple inputs must be bundled into a single instance

    if (CallArgs.size() > outArgCount + 1) {
      size_t arraySize = CallArgs.size() - outArgCount;
      llvm::Type *elemType = InArgsDataType[0];

      llvm::Value *arraySizeValue = state.Builder->getInt32(arraySize);
      llvm::AllocaInst *arrayAlloc = state.Builder->CreateAlloca(
          elemType, arraySizeValue, "bundle_intermediate");

      for (size_t i = 0; i < arraySize; ++i) {
        llvm::Value *index = llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(*state.TheContext), i);

        llvm::Value *gepPtr = state.Builder->CreateInBoundsGEP(
            elemType, arrayAlloc, {state.Builder->getInt32(0), index},
            "array_element_ptr");

        state.Builder->CreateStore(CallArgs[i].first, gepPtr);
      }
      llvm::Value *basePtr = state.Builder->CreateLoad(
          state.Builder->getPtrTy(), arrayAlloc, "load_base_ptr");
      CallArgs.resize(outArgCount);
      CallArgs.push_back({std::move(basePtr), elemType});
      llvm::outs() << "Bundled inputs into: ";
      basePtr->print(llvm::outs());
      llvm::outs() << "\n";
    }
    // processArgGroup(state, InternalArgs, CalleeF, CallArgs);
  } else if (callType == CallableType::Reaction) {
    processArgGroup(state, ExternalArgs, CalleeF, CallArgs);
  } else if (callType == CallableType::External) {
    processArgGroup(state, InArgs, CalleeF, CallArgs);
    // processArgGroup(state, InternalArgs, CalleeF, CallArgs);
  } else {
    //    assert(0 == 1);
  }
  llvm::outs().flush();
  // for (unsigned i = 0, e = ExternalArgs.size(); i != e; ++i) {
  //   auto [value, type] = ExternalArgs[i]->codegen(state);
  //   if (value->getType()->isTokenTy()) {
  //     auto *list = dynamic_cast<ListExprAST *>(OutArgs[i].get());
  //     //      assert(list);
  //     //      for (const auto &expr : list->elements()) {
  //     //        auto newArg = func(expr->codegen(state), CalleeF->getArg(i),
  //     //        state); if (!newArg)
  //     //          return nullptr;
  //     //        CallArgs.push_back(std::move(newArg));
  //     //        if (CallArgs.back()->getType()->isPointerTy() &&
  //     //            !CalleeF->getArg(i)->getType()->isPointerTy()) {
  //     //          CallArgs.back() = state.Builder->CreateLoad(
  //     //              llvm::Type::getDoubleTy(*state.TheContext),
  //     //              CallArgs.back(), "");
  //     //        }
  //     //      }

  //   } else {
  //     auto newArg = func(value, type, CalleeF->getArg(i), state);
  //     if (!newArg) {
  //       return {nullptr, std::nullopt};
  //     }
  //     CallArgs.push_back({std::move(newArg), type});
  //     if (CallArgs.back().first->getType()->isPointerTy() &&
  //         !CalleeF->getArg(i)->getType()->isPointerTy()) {
  //       CallArgs.back() = {
  //           state.Builder->CreateLoad(CallArgs.back().second.value(),
  //                                     CallArgs.back().first, ""),
  //           type};
  //     }
  //   }
  // }

  for (unsigned i = 0, e = PortPropArgs.size(); i != e; ++i) {
    auto [value, type] = PortPropArgs[i]->codegen(state);
    CallArgs.push_back({std::move(value), type});
  }

  llvm::outs() << "Callee:\n";
  CalleeF->print(llvm::outs());
  llvm::outs() << "\n";
  llvm::CallInst *call;

  llvm::outs() << "Call Arguments: ";
  for (const auto &arg : CallArgs) {
    arg.first->print(llvm::outs());
    llvm::outs() << "|, ";
  }
  llvm::outs() << "\n";
  llvm::outs().flush();
  if (callType == CallableType::Reaction) {
    llvm::Value *CondV = InArgs[0]->codegen(state).first;
    CondV->print(llvm::outs());
    llvm::outs() << "\n";
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

    std::vector<llvm::Value *> CallArgsValues;
    for (const auto &p : CallArgs) {
      CallArgsValues.push_back(p.first);
    }
    call =
        state.Builder->CreateCall(CalleeF, CallArgsValues, CalleeF->getName());

    state.Builder->CreateBr(MergeBB);
    // ThenBB = state.Builder->GetInsertBlock();
    state.Builder->SetInsertPoint(MergeBB);
  } else if (callType == CallableType::Loop) {
    std::vector<llvm::Value *> CallArgsValues;
    for (const auto &p : CallArgs) {
      CallArgsValues.push_back(p.first);
    }
    call =
        state.Builder->CreateCall(CalleeF, CallArgsValues, CalleeF->getName());
  } else {
    std::vector<llvm::Value *> CallArgsValues;
    for (const auto &p : CallArgs) {
      CallArgsValues.push_back(p.first);
    }
    call =
        state.Builder->CreateCall(CalleeF, CallArgsValues, CalleeF->getName());
  }

  // Write to output
  for (unsigned i = 0, e = OutArgs.size(); i != e; ++i) {
    llvm::Value *argValue = CallArgs[i].first;
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

  return {call, std::nullopt};
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
LLVMCommandAST::codegen(StrideCompiler &state) {
  std::vector<std::pair<llvm::Value *, std::optional<llvm::Type *>>> CallArgs;

  for (unsigned i = 0, e = InArgs.size(); i != e; ++i) {
    auto [value, type] = InArgs[i]->codegen(state);
    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(InArgs[i].get());
      assert(list);
      for (const auto &expr : list->elements()) {
        auto [exprValue, exprType] = expr->codegen(state);
        CallArgs.push_back({exprValue, exprType});
        if (CallArgs.back().first->getType()->isPointerTy()) {
          if (!CallArgs.back().second.has_value()) {
            return {nullptr, std::nullopt};
          }
          CallArgs.back() = {
              state.Builder->CreateLoad(CallArgs.back().second.value(),
                                        CallArgs.back().first, ""),
              CallArgs.back().second.value()};
        }
      }
    } else {
      CallArgs.push_back({value, type});
      if (CallArgs.back().first->getType()->isPointerTy()) {
        if (!CallArgs.back().second.has_value()) {
          return {nullptr, std::nullopt};
        }
        CallArgs.back() = {
            state.Builder->CreateLoad(CallArgs.back().second.value(),
                                      CallArgs.back().first, ""),
            type};
      }
    }
  }
  for (unsigned i = 0, e = OutArgs.size(); i != e; ++i) {
    auto [value, type] = OutArgs[i]->codegen(state);
    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(OutArgs[i].get());
      assert(list);
      for (const auto &expr : list->elements()) {
        CallArgs.push_back(expr->codegen(state));
        if (CallArgs.back().first->getType()->isPointerTy()) {
          CallArgs.back() = {
              state.Builder->CreateLoad(CallArgs.back().first->getType(),
                                        CallArgs.back().first, ""),
              std::nullopt};
        }
      }
    } else {
      CallArgs.push_back({value, type});
      if (CallArgs.back().first->getType()->isPointerTy()) {
        CallArgs.back() = {
            state.Builder->CreateLoad(CallArgs.back().second.value(),
                                      CallArgs.back().first, ""),
            std::nullopt};
      }
    }
  }
  for (unsigned i = 0, e = ExternalArgs.size(); i != e; ++i) {
    auto [value, type] = ExternalArgs[i]->codegen(state);
    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(OutArgs[i].get());
    } else {
      CallArgs.push_back({value, type});
      if (CallArgs.back().first->getType()->isPointerTy()) {
        CallArgs.back() = {
            state.Builder->CreateLoad(CallArgs.back().second.value(),
                                      CallArgs.back().first, ""),
            std::nullopt};
      }
    }
  }

  for (unsigned i = 0, e = PortPropArgs.size(); i != e; ++i) {
    auto [value, type] = PortPropArgs[i]->codegen(state);
    CallArgs.push_back({value, type});
  }

  llvm::Value *outval{nullptr};
  std::optional<llvm::Type *> outtype;
  if (command == "icmp gt") {
    outval = state.Builder->CreateICmpSGT(CallArgs[0].first, CallArgs[1].first);
    outtype = llvm::Type::getInt1Ty(*state.TheContext);
  } else if (command == "icmp eq") {
    outval = state.Builder->CreateICmpEQ(CallArgs[0].first, CallArgs[1].first);
    outtype = llvm::Type::getInt1Ty(*state.TheContext);
  } else if (command == "fcmp ogt") {
    outval = state.Builder->CreateFCmpOGT(CallArgs[0].first, CallArgs[1].first);
    outtype = llvm::Type::getInt1Ty(*state.TheContext);
  } else if (command == "fcmp oeq") {
    outval = state.Builder->CreateFCmpOEQ(CallArgs[0].first, CallArgs[1].first);
    outtype = llvm::Type::getInt1Ty(*state.TheContext);
  } else {
    assert(0 == 1); // FIXME implement
  }

  return {outval, outtype};
}
