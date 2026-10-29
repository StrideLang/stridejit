// #include "llvm/ADT/StringRef.h"
#include "stride/utils/logger.h"
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
#include "stride/stridejit/stridedebugemitter.hpp"
#include "stride/stridejit/listexprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include "stride/parser/ast.h"
#include "stride/parser/entitynode.h"
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
    if (state.currentFunctionStateInfo &&
        state.currentFunctionStateInfo->varIndices.find(varName) !=
            state.currentFunctionStateInfo->varIndices.end()) {
      // Variable is allocated in state struct; skip local stack alloca
      continue;
    }
    auto defaultNode = decl->getPropertyValue("default");
    if (!defaultNode) {
      defaultNode = decl->getPropertyValue("value");
    }
    if (defaultNode) {
      if (decl->getNodeType() == AST::Declaration) {
        llvm::Type *type = state.getLLVMType(decl);
        // Create entry block stack allocation
        llvm::AllocaInst *alloca =
            state.CreateEntryBlockAlloca(TheFunction, varName, type);
        // Register in NamedValues so VariableExprAST and assignments locate the
        // alloca
        state.NamedValues[varName] = {alloca, type};
        if (state.debugEmitter && state.debugEmitter->isInitialized()) {
          state.debugEmitter->emitLocalVariable(
              *state.Builder, alloca, varName, decl->getRelativeFilename(),
              decl->getAbsoluteFilename(), decl->getLine(), decl->getColumn(),
              state.debugEmitter->getOrCreateTypeForLLVMType(type));
        }

        // Set default values. Should be initialized on every function call as
        // they are not persistent
        // Should we optmize here if values are not modified, or just defer to
        // the compiler?
        if (defaultNode->getNodeType() == AST::Int) {
          int64_t intVal =
              std::static_pointer_cast<ValueNode>(defaultNode)->getIntValue();
          llvm::Value *val = nullptr;
          if (type->isFloatingPointTy()) {
            val = llvm::ConstantFP::get(type, static_cast<double>(intVal));
          } else {
            val = llvm::ConstantInt::get(type, intVal);
          }
          state.Builder->CreateStore(val, alloca);
        } else if (defaultNode->getNodeType() == AST::Real) {
          double realVal =
              std::static_pointer_cast<ValueNode>(defaultNode)->getRealValue();
          llvm::Value *val = nullptr;
          if (type->isIntegerTy()) {
            val = llvm::ConstantInt::get(type, static_cast<int64_t>(realVal));
          } else {
            val = llvm::ConstantFP::get(type, realVal);
          }
          state.Builder->CreateStore(val, alloca);
        } else if (defaultNode->getNodeType() == AST::Switch) {
          bool switchVal = std::static_pointer_cast<ValueNode>(defaultNode)
                               ->getSwitchValue();
          llvm::Value *val = nullptr;
          if (type->isFloatingPointTy()) {
            val = llvm::ConstantFP::get(type, switchVal ? 1.0 : 0.0);
          } else {
            val = llvm::ConstantInt::get(type, switchVal ? 1 : 0);
          }
          state.Builder->CreateStore(val, alloca);
        } else {
          LOG_ERROR() << "ERROR: type not supported for default" << std::endl;
          assert(0 == 1);
        }
      } else if (decl->getNodeType() == AST::ArrayDeclaration) {
        llvm::Type *Int32Ty = state.Builder->getInt32Ty();
        int size = ASTQuery::getEntityDeclaredSize(decl, {}, nullptr);
        llvm::Type *type = state.getLLVMType(decl);

        // TODO support non-literal array sizes. We need to have the tree here,
        // or have previously passed the size so that we know it here.
        llvm::AllocaInst *alloca =
            state.CreateEntryBlockAllocaArray(TheFunction, varName, type, size);
        // TODO optmize when possible for const arrays.
        // llvm::AllocaInst *alloca =
        //     state.CreateEntryBlockAllocaArrayConst(TheFunction, varName,
        //     type, size);
        if (defaultNode->getNodeType() == AST::Int ||
            defaultNode->getNodeType() == AST::Real ||
            defaultNode->getNodeType() == AST::Switch) {
          llvm::Value *Val = nullptr;
          if (defaultNode->getNodeType() == AST::Int) {
            int64_t intVal =
                std::static_pointer_cast<ValueNode>(defaultNode)->getIntValue();
            if (type->isFloatingPointTy()) {
              Val = llvm::ConstantFP::get(type, static_cast<double>(intVal));
            } else {
              Val = llvm::ConstantInt::get(type, intVal);
            }
          } else if (defaultNode->getNodeType() == AST::Real) {
            double realVal = std::static_pointer_cast<ValueNode>(defaultNode)
                                 ->getRealValue();
            if (type->isIntegerTy()) {
              Val = llvm::ConstantInt::get(type, static_cast<int64_t>(realVal));
            } else {
              Val = llvm::ConstantFP::get(type, realVal);
            }
          } else {
            bool switchVal = std::static_pointer_cast<ValueNode>(defaultNode)
                                 ->getSwitchValue();
            if (type->isFloatingPointTy()) {
              Val = llvm::ConstantFP::get(type, switchVal ? 1.0 : 0.0);
            } else {
              Val = llvm::ConstantInt::get(type, switchVal ? 1 : 0);
            }
          }

          auto functionName = TheFunction->getName();
          llvm::BasicBlock *PreLoopBB = state.Builder->GetInsertBlock();

          // Create Basic Blocks for the Loop
          llvm::BasicBlock *CondBB = llvm::BasicBlock::Create(
              *state.TheContext, functionName + "_" + varName + "_loop.cond",
              TheFunction);
          llvm::BasicBlock *BodyBB = llvm::BasicBlock::Create(
              *state.TheContext, functionName + "_" + varName + "_loop.body",
              TheFunction);
          llvm::BasicBlock *IncBB = llvm::BasicBlock::Create(
              *state.TheContext, functionName + "_" + varName + "_loop.inc",
              TheFunction);
          llvm::BasicBlock *EndBB = llvm::BasicBlock::Create(
              *state.TheContext, functionName + "_" + varName + "_loop.end",
              TheFunction);

          // Jump from preceding block into condition block
          state.Builder->CreateBr(CondBB);

          // --- CONDITION BLOCK (Host for the PHI Node) ---
          state.Builder->SetInsertPoint(CondBB);

          // Create the PHI node for loop index 'i'
          llvm::PHINode *CurrI = state.Builder->CreatePHI(Int32Ty, 2, "i");
          CurrI->addIncoming(state.Builder->getInt32(0), PreLoopBB);

          // TODO handle dynamic sizes
          // Condition check:
          llvm::Value *Cmp = state.Builder->CreateICmpSLT(
              CurrI, state.Builder->getInt32(size), "cmp");
          state.Builder->CreateCondBr(Cmp, BodyBB, EndBB);

          // --- BODY BLOCK ---
          state.Builder->SetInsertPoint(BodyBB);

          // Compute pointer to arr[i] using two indices {0, i}
          llvm::Value *IdxList[] = {state.Builder->getInt32(0), CurrI};
          llvm::ArrayType *arrayTy = llvm::ArrayType::get(type, size);
          llvm::Value *ElemPtr = state.Builder->CreateInBoundsGEP(
              arrayTy, alloca, IdxList, "elem.ptr");

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

        } else if (defaultNode->getNodeType() == AST::List) {

          for (int i = 0; i < size; i++) {
            llvm::Value *Val = nullptr;
            auto child = defaultNode->getChildren()[i];
            if (child->getNodeType() == AST::Int) {
              int64_t v =
                  std::static_pointer_cast<ValueNode>(child)->getIntValue();
              if (type->isFloatingPointTy()) {
                Val = llvm::ConstantFP::get(type, static_cast<double>(v));
              } else {
                Val = llvm::ConstantInt::get(type, v);
              }
            } else if (child->getNodeType() == AST::Real) {
              double v =
                  std::static_pointer_cast<ValueNode>(child)->getRealValue();
              if (type->isIntegerTy()) {
                Val = llvm::ConstantInt::get(type, static_cast<int64_t>(v));
              } else {
                Val = llvm::ConstantFP::get(type, v);
              }
            } else if (child->getNodeType() == AST::Switch) {
              bool v =
                  std::static_pointer_cast<ValueNode>(child)->getSwitchValue();
              if (type->isFloatingPointTy()) {
                Val = llvm::ConstantFP::get(type, v ? 1.0 : 0.0);
              } else {
                Val = llvm::ConstantInt::get(type, v ? 1 : 0);
              }
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
          LOG_ERROR() << "ERROR: type not supported for default" << std::endl;
          assert(0 == 1);
        }

        // Register in NamedValues so VariableExprAST and assignments locate the
        // alloca
        state.NamedValues[varName] = {alloca, type};
        if (state.debugEmitter && state.debugEmitter->isInitialized()) {
          state.debugEmitter->emitLocalVariable(
              *state.Builder, alloca, varName, decl->getRelativeFilename(),
              decl->getAbsoluteFilename(), decl->getLine(), decl->getColumn(),
              state.debugEmitter->getOrCreateTypeForLLVMType(alloca->getAllocatedType()));
        }
      }
    }
  }
}

llvm::Function *FunctionAST::codegen(StrideCompiler &state) {

  LOG_INFO() << " == FunctionAST codegen : " << Proto->getName() << std::endl;
  // Record the function arguments in the NamedValues map.
  state.NamedValues.clear();
  state.PortBlockMap.clear();
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto &P = *Proto;
  std::string protoName = P.getName();
  llvm::Function *TheFunction = state.getFunctionInModule(protoName);
  if (!TheFunction) {
    P.callType = callType;
    TheFunction = P.codegen(state);
    if (Logger::isDebugEnabled() && TheFunction) {
      std::string s;
      llvm::raw_string_ostream rso(s);
      TheFunction->print(rso);
      LOG_DEBUG() << rso.str() << "\n";
    }
  }
  if (Proto &&
      state.FunctionProtos.find(protoName) == state.FunctionProtos.end()) {
    state.FunctionProtos[protoName] = std::move(Proto);
  }
  // If this is an operator, install it.
  if (P.isBinaryOp())
    state.BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();
  // Create a new basic block to start insertion into.
  llvm::BasicBlock *BB =
      llvm::BasicBlock::Create(*state.TheContext, "entry", TheFunction);
  state.Builder->SetInsertPoint(BB);

  llvm::DISubprogram *subprogram = nullptr;
  if (state.debugEmitter && state.debugEmitter->isInitialized() && TheFunction) {
    std::string dispName = !displayName.empty() ? displayName : protoName;
    int fnLine = line > 0 ? line : (funcInstance ? funcInstance->getLine() : 1);
    int fnCol = column > 0 ? column : (funcInstance ? funcInstance->getColumn() : 1);
    std::string relFile = !relativeFilename.empty() ? relativeFilename
        : (funcInstance ? funcInstance->getRelativeFilename() : "");
    std::string absFile = !absoluteFilename.empty() ? absoluteFilename
        : (funcInstance ? funcInstance->getAbsoluteFilename() : "");

    subprogram = state.debugEmitter->createSubprogram(
        TheFunction, dispName, protoName, relFile, absFile, fnLine, fnLine);
    state.debugEmitter->pushScope(subprogram);
    state.debugEmitter->clearLocation(*state.Builder);
  }

  auto argTypes = P.getUsedArgsTypes();

  int i = 0;
  for (auto &Arg : TheFunction->args()) {
    auto argType = argTypes[i];
    // TODO verify how this interacts with the other NamedValues setting in
    // allocateInternalVariables and other places
    if (i != 0) {
      LOG_INFO() << ", ";
    }
    state.NamedValues[std::string(Arg.getName())] = {&Arg, argType};
    i++;
    LOG_INFO() << std::string(Arg.getName());
  }
  LOG_INFO() << std::endl;

  llvm::Value *prevFunctionStatePtr = state.currentFunctionStatePtr;
  const StrideCompiler::StateStructInfo *prevFunctionStateInfo =
      state.currentFunctionStateInfo;

  if (hasState) {
    auto stateArgIt = state.NamedValues.find("__state");
    if (stateArgIt != state.NamedValues.end()) {
      state.currentFunctionStatePtr = stateArgIt->second.first;

      state.currentFunctionStateInfo = state.getStateStructInfo(funcInstance);
      if (state.currentFunctionStateInfo) {
        LOG_DEBUG() << "DEBUG: currentFunctionStateInfo is valid!\n";
      } else {
        LOG_DEBUG() << "DEBUG: currentFunctionStateInfo is NULL!\n";
      }

      if (!state.currentFunctionStateInfo && state.m_tree) {
        std::string searchName = P.getName();
        size_t pos = searchName.find("_process");
        if (pos != std::string::npos) {
          searchName = searchName.substr(0, pos);
        } else {
          pos = searchName.find("_init");
          if (pos != std::string::npos) {
            searchName = searchName.substr(0, pos);
          }
        }

        state.currentFunctionStateInfo = state.getStateStructInfo(searchName);

        if (!state.currentFunctionStateInfo) {
          state.currentFunctionStateInfo = state.getStateStructInfo(
              ASTQuery::findDeclarationByName(searchName, {}, state.m_tree));
        }
      }

      if (state.currentFunctionStateInfo &&
          state.currentFunctionStateInfo->structType) {
        for (const auto &[varName, idx] :
             state.currentFunctionStateInfo->varIndices) {
          llvm::Type *elemTy =
              state.currentFunctionStateInfo->structType->getElementType(idx);
          llvm::Value *fieldPtr = state.Builder->CreateStructGEP(
              state.currentFunctionStateInfo->structType,
              state.currentFunctionStatePtr, idx, varName + "_ptr");
          state.NamedValues[varName] = {fieldPtr, elemTy};
          if (state.debugEmitter && subprogram) {
            std::string relFile = !relativeFilename.empty()
                ? relativeFilename
                : (funcInstance ? funcInstance->getRelativeFilename() : "");
            std::string absFile = !absoluteFilename.empty()
                ? absoluteFilename
                : (funcInstance ? funcInstance->getAbsoluteFilename() : "");
            state.debugEmitter->emitLocalVariable(
                *state.Builder, fieldPtr, varName, relFile, absFile,
                line > 0 ? line : 1, column > 0 ? column : 1,
                state.debugEmitter->getOrCreateTypeForLLVMType(elemTy), subprogram);
          }
          LOG_DEBUG() << "DEBUG: Added " << varName << " to NamedValues!\n";
        }
      }
    }
  } else {
    state.currentFunctionStatePtr = nullptr;
    state.currentFunctionStateInfo = nullptr;
  }

  // Pre Body
  // Before actual function code there is some work done to loops to manage the
  // iteration and to functions that have packed arguments, to unpack them. For
  // everything else, local variables need to be allocated

  // For loops
  std::map<std::string, llvm::Value *> OldVals;
  std::map<std::string, llvm::PHINode *> PHIVariables;
  llvm::BasicBlock *LoopBB = nullptr;
  llvm::BasicBlock *ReactionExitBB = nullptr;
  int64_t itStart = 0, itLimit = 0, itIncrement = 0;
  std::string itName;

  if (callType == CallableType::DomainFunction) {
    allocateInternalVariables(state, TheFunction);
    if (state.hasConfiguration(StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL)) {

      llvm::BasicBlock *EntryBB = &TheFunction->getEntryBlock();
      state.Builder->SetInsertPoint(EntryBB);
      auto arg = TheFunction->getArg(TheFunction->arg_size() - 1);
      if (Logger::isDebugEnabled()) {
        std::string s;
        llvm::raw_string_ostream rso(s);
        EntryBB->print(rso);
        arg->print(rso);
        arg->getType()->print(rso);
        LOG_DEBUG() << rso.str() << "\n";
      }
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

    // Output ports are caller-allocated pointers, so they don't go through the
    // internal variable default initialization. A loop is reset on every
    // execution, so write the declared default of each output entity into its
    // pointer before entering the loop body.
    {
      std::shared_ptr<DeclarationNode> loopDecl;
      if (funcInstance && funcInstance->getNodeType() == AST::Declaration) {
        loopDecl = std::static_pointer_cast<DeclarationNode>(funcInstance);
      } else if (state.m_tree) {
        loopDecl = ASTQuery::findDeclarationByName(P.getName(), {},
                                                   state.m_tree);
      }
      auto blocksNode = loopDecl ? loopDecl->getPropertyValue("blocks")
                                 : nullptr;
      if (blocksNode) {
        for (const auto &outArg : P.getOutArgs()) {
          auto outIt = state.NamedValues.find(outArg.name);
          if (outIt == state.NamedValues.end() ||
              !outIt->second.first->getType()->isPointerTy()) {
            continue;
          }
          auto outDecl = ASTQuery::findDeclarationByName(
              outArg.name, {{loopDecl, blocksNode->getChildren()}}, nullptr);
          auto defaultNode =
              outDecl ? outDecl->getPropertyValue("default") : nullptr;
          llvm::Type *outType = outArg.llvmType;
          if (!defaultNode || !outType || outType->isArrayTy()) {
            continue;
          }
          llvm::Constant *defaultVal = nullptr;
          if (defaultNode->getNodeType() == AST::Switch) {
            bool v = std::static_pointer_cast<ValueNode>(defaultNode)
                         ->getSwitchValue();
            if (outType->isFloatingPointTy()) {
              defaultVal = llvm::ConstantFP::get(outType, v ? 1.0 : 0.0);
            } else if (outType->isIntegerTy()) {
              defaultVal = llvm::ConstantInt::get(outType, v ? 1 : 0);
            }
          } else if (defaultNode->getNodeType() == AST::Int) {
            int64_t v = std::static_pointer_cast<ValueNode>(defaultNode)
                            ->getIntValue();
            if (outType->isFloatingPointTy()) {
              defaultVal = llvm::ConstantFP::get(outType, double(v));
            } else if (outType->isIntegerTy()) {
              defaultVal = llvm::ConstantInt::get(outType, v, true);
            }
          } else if (defaultNode->getNodeType() == AST::Real) {
            double v = std::static_pointer_cast<ValueNode>(defaultNode)
                           ->getRealValue();
            if (outType->isFloatingPointTy()) {
              defaultVal = llvm::ConstantFP::get(outType, v);
            } else if (outType->isIntegerTy()) {
              defaultVal = llvm::ConstantInt::get(outType, int64_t(v), true);
            }
          }
          if (defaultVal) {
            state.Builder->CreateStore(defaultVal, outIt->second.first);
          }
        }
      }
    }

    // Insert an explicit fall through from the current block to the LoopBB.
    state.Builder->CreateBr(LoopBB);
    // Start insertion in LoopBB.
    state.Builder->SetInsertPoint(LoopBB);
    { // Define internal vars as PHI, needed for SSA (single static assignment)
      // when having branching code.
      for (const auto &decl : internalVariables) {
        llvm::Type *type;
        llvm::Value *defaultValue = nullptr;
        if (decl->getEntityType() == "signal") {
          //  Local signals in loops are reset on every trigger, so they are
          //  allocated and set to their default value
          auto defaultNode = decl->getPropertyValue("default");
          if (!defaultNode) {
            LOG_ERROR() << "No default provided for internal variable."
                        << std::endl;
            continue;
          }
          auto typeNode = decl->getPropertyValue("type");
          if (typeNode && typeNode->getNodeType() == AST::Entity) {
            auto typeBlockName =
                std::static_pointer_cast<EntityNode>(typeNode)->getName();
            if (state.typesMap.find(typeBlockName) != state.typesMap.end()) {
              type = state.typesMap[typeBlockName];
            } else {
              LOG_ERROR() << "Unknown type " << typeBlockName << std::endl;
            }
            auto typeInfo = state.getDataTypeInfo(typeBlockName);
            if (typeInfo.isFloat()) {
              double val =
                  std::static_pointer_cast<ValueNode>(defaultNode)->toReal();
              defaultValue = llvm::ConstantFP::get(type, val);

            } else if (typeInfo.isInteger()) {
              if (defaultNode->getNodeType() == AST::Int) {
                int64_t val = std::static_pointer_cast<ValueNode>(defaultNode)
                                  ->getIntValue();
                defaultValue = llvm::ConstantInt::get(
                    type, static_cast<uint64_t>(val), typeInfo.isSigned);
              } else if (defaultNode->getNodeType() == AST::Real) {
                double val = std::static_pointer_cast<ValueNode>(defaultNode)
                                 ->getRealValue();

                defaultValue = llvm::ConstantInt::get(
                    type, static_cast<uint64_t>(static_cast<int64_t>(val)),
                    typeInfo.isSigned);
              } else {
                LOG_ERROR()
                    << "Unsupported type. Falling back on double" << std::endl;
                assert(0 == 1);
                type = llvm::Type::getDoubleTy(*state.TheContext);
                defaultValue = llvm::ConstantFP::get(
                    llvm::Type::getDoubleTy(*state.TheContext),
                    llvm::APFloat(0.0));
              }
            } else {
              LOG_ERROR() << "Unsupported type. Falling back on double"
                          << std::endl;
              assert(0 == 1);
              // type = llvm::Type::getDoubleTy(*state.TheContext);
              // defaultValue = llvm::ConstantFP::get(
              //     llvm::Type::getDoubleTy(*state.TheContext),
              //     llvm::APFloat(0.0));
            }
          } else {
            LOG_ERROR() << "Unsupported type. Falling back on double"
                        << std::endl;
            assert(0 == 1);
            type = llvm::Type::getDoubleTy(*state.TheContext);
            defaultValue = llvm::ConstantFP::get(
                llvm::Type::getDoubleTy(*state.TheContext), llvm::APFloat(0.0));
          }
          assert(defaultValue);
        } else if (decl->getEntityType() == "switch" ||
                   decl->getEntityType() == "trigger") {
          //  Local switches in loops are reset on every trigger, so they are
          //  allocated and set to their default value
          type = state.typesMap[DataTypes::Switch];
          auto defaultNode = decl->getPropertyValue("default");
          if (defaultNode && defaultNode->getNodeType() == AST::Switch) {
            bool val = std::static_pointer_cast<ValueNode>(defaultNode)
                           ->getSwitchValue();
            defaultValue = llvm::ConstantInt::get(
                llvm::Type::getInt1Ty(*state.TheContext),
                llvm::APInt(1, val ? 1 : 0));
          } else {
            defaultValue = llvm::ConstantInt::get(
                llvm::Type::getInt1Ty(*state.TheContext), llvm::APInt(1, 0));
          }
        } else if (decl->getEntityType() == "iterator") {
          LOG_INFO() << "iterator" << std::endl;
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
          LOG_ERROR() << "Invalid declaration for entity '" << decl->getName()
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

        if (state.debugEmitter && subprogram && decl) {
          ElementContext ctx;
          ctx.kind = ElementContext::Kind::Generator;
          ctx.varName = VarName;
          ctx.currentVal = Variable;
          state.debugEmitter->pushElementContext(ctx);
          state.debugEmitter->emitElementContextVariable(
              *state.Builder, decl->getRelativeFilename(),
              decl->getAbsoluteFilename(), decl->getLine(), decl->getColumn());
        }
      }

      for (const auto &portPropArg : P.getUsedMemberAccesses()) {
        // state.NamedValues[externalArg.name] = {externalArg,
        // externalArg.llvmType};
      }
    }
  } else {
    allocateInternalVariables(state, TheFunction);
  }

  // A terminateWhen switch ends the callable as soon as a statement turns it
  // on: a reaction returns, and a loop leaves without finishing the iteration.
  // Loop variables only need values on the back edge, so leaving from the
  // middle of the body keeps the loop's PHI nodes valid.
  llvm::BasicBlock *LoopAfterBB = nullptr;
  if (!terminateWhenName.empty()) {
    if (callType == CallableType::Loop) {
      LoopAfterBB =
          llvm::BasicBlock::Create(*state.TheContext, "afterloop", TheFunction);
    } else {
      ReactionExitBB = llvm::BasicBlock::Create(
          *state.TheContext, TheFunction->getName() + ".exit", TheFunction);
    }
  }
  llvm::BasicBlock *TerminateBB = LoopAfterBB ? LoopAfterBB : ReactionExitBB;

  // Generate the function body
  for (size_t statementIndex = 0; statementIndex < Body.size();
       ++statementIndex) {
    const auto &statement = Body[statementIndex];
    if (!statement) {
      return nullptr;
    }
    if (state.debugEmitter && subprogram && statement->getLine() > 0) {
      state.debugEmitter->emitLocation(*state.Builder, statement->getLine(),
                                       statement->getColumn(), subprogram);
    }
    auto [RetVal, retType] = statement->codegen(state);

    // The end-of-iteration check already covers a loop's last statement.
    bool isLastLoopStatement =
        LoopAfterBB && statementIndex + 1 == Body.size();
    if (TerminateBB && !isLastLoopStatement &&
        statement->writesTo(terminateWhenName)) {
      if (!state.Builder->GetInsertBlock()->getTerminator()) {
        llvm::Value *condVal = nullptr;
        if (state.NamedValues.find(terminateWhenName) !=
            state.NamedValues.end()) {
          condVal = state.NamedValues[terminateWhenName].first;
          if (condVal->getType()->isPointerTy()) {
            auto elemType =
                state.NamedValues[terminateWhenName].second.has_value()
                    ? state.NamedValues[terminateWhenName].second.value()
                    : llvm::Type::getInt1Ty(*state.TheContext);
            condVal = state.Builder->CreateLoad(elemType, condVal, "term_cond");
          }
        } else if (state.globalExists(terminateWhenName)) {
          auto global = state.getGlobal(terminateWhenName);
          auto elemType = global.second.has_value()
                              ? global.second.value()
                              : llvm::Type::getInt1Ty(*state.TheContext);
          condVal =
              state.Builder->CreateLoad(elemType, global.first, "term_cond");
        }
        if (condVal) {
          if (condVal->getType()->isIntegerTy() &&
              !condVal->getType()->isIntegerTy(1)) {
            condVal = state.Builder->CreateICmpNE(
                condVal, llvm::ConstantInt::get(condVal->getType(), 0),
                "term_bool");
          } else if (condVal->getType()->isDoubleTy()) {
            condVal = state.Builder->CreateFCmpONE(
                condVal,
                llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(0.0)),
                "term_bool");
          }
          llvm::BasicBlock *ContBB = llvm::BasicBlock::Create(
              *state.TheContext, TheFunction->getName() + ".cont", TheFunction);
          state.Builder->CreateCondBr(condVal, TerminateBB, ContBB);
          state.Builder->SetInsertPoint(ContBB);
        }
      }
    }
  }

  // Post Body
  if (callType == CallableType::Loop) {
    llvm::Value *EndCond = nullptr;
    if (terminateWhenName.size() > 0) {
      if (state.NamedValues.find(terminateWhenName) !=
          state.NamedValues.end()) {
        EndCond = state.NamedValues[terminateWhenName].first;
        if (EndCond && EndCond->getType()->isPointerTy()) {
          auto elemType =
              state.NamedValues[terminateWhenName].second.has_value()
                  ? state.NamedValues[terminateWhenName].second.value()
                  : llvm::Type::getInt1Ty(*state.TheContext);
          EndCond = state.Builder->CreateLoad(elemType, EndCond, "term_cond");
        }
      } else if (state.globalExists(terminateWhenName)) {
        auto global = state.getGlobal(terminateWhenName);
        auto elemType = global.second.has_value()
                            ? global.second.value()
                            : llvm::Type::getInt1Ty(*state.TheContext);
        EndCond =
            state.Builder->CreateLoad(elemType, global.first, "term_cond");
      }
      if (EndCond) {
        if (EndCond->getType()->isIntegerTy() &&
            !EndCond->getType()->isIntegerTy(1)) {
          EndCond = state.Builder->CreateICmpNE(
              EndCond, llvm::ConstantInt::get(EndCond->getType(), 0),
              "term_bool");
        } else if (EndCond->getType()->isDoubleTy()) {
          EndCond = state.Builder->CreateFCmpONE(
              EndCond,
              llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(0.0)),
              "term_bool");
        }
      } else {
        LOG_ERROR() << "terminateWhen variable '" << terminateWhenName
                    << "' not found in loop NamedValues or globals"
                    << std::endl;
      }
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
    llvm::BasicBlock *AfterBB = LoopAfterBB;
    if (AfterBB) {
      AfterBB->moveAfter(LoopEndBB);
    } else {
      AfterBB =
          llvm::BasicBlock::Create(*state.TheContext, "afterloop", TheFunction);
    }

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

      if (state.debugEmitter && subprogram) {
        state.debugEmitter->popElementContext();
      }
    }
  }

  if (ReactionExitBB) {
    if (!state.Builder->GetInsertBlock()->getTerminator()) {
      state.Builder->CreateBr(ReactionExitBB);
    }
    state.Builder->SetInsertPoint(ReactionExitBB);
  }

  auto *outVal = llvm::ConstantInt::get(state.Builder->getInt32Ty(), 0, true);
  state.Builder->CreateRet(outVal);
  state.currentFunctionStatePtr = prevFunctionStatePtr;
  state.currentFunctionStateInfo = prevFunctionStateInfo;
  // Validate the generated code, checking for consistency.
  {
    std::string verifyErrors;
    llvm::raw_string_ostream verifyStream(verifyErrors);
    if (llvm::verifyFunction(*TheFunction, &verifyStream)) {
      LOG_ERROR() << "Invalid IR generated for " << TheFunction->getName().str()
                  << ": " << verifyStream.str() << std::endl;
    }
  }
  if (Logger::isDebugEnabled()) {
    std::string s;
    llvm::raw_string_ostream rso(s);
    TheFunction->print(rso);
    LOG_DEBUG() << rso.str() << "\n";
  }
  if (state.debugEmitter && subprogram) {
    state.debugEmitter->popScope();
  }
  return TheFunction;
}

std::vector<PrototypeArg> PrototypeAST::getExternalArgs() const {
  return ExternalArgs;
}

std::vector<PrototypeArg> PrototypeAST::getInternalArgs() const {
  return InternalPersistentArgs;
}

std::vector<PrototypeArg> PrototypeAST::getUsedMemberAccesses() const {
  return UsedPortProperties;
}

llvm::Function *PrototypeAST::codegen(StrideCompiler &state) {
  // Make the function type:  double(double,double) etc.

  LOG_INFO() << " == PrototypeAST codegen" << std::endl;
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
    for (const auto &arg : PropertyArgs) {
      ProtoArguments.emplace_back(llvm::PointerType::get(*state.TheContext, 0));
    }
    for (const auto &arg : InternalPersistentArgs) {
      ProtoArguments.emplace_back(llvm::PointerType::get(*state.TheContext, 0));
    }
    for (const auto &arg : ExternalArgs) {
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
    } else if (Idx < (OutArgs.size() + InArgs.size() + PropertyArgs.size())) {
      Arg.setName(PropertyArgs[Idx - OutArgs.size() - InArgs.size()].name);
    } else if (Idx < (OutArgs.size() + InArgs.size() + PropertyArgs.size() +
                      InternalPersistentArgs.size())) {
      Arg.setName(InternalPersistentArgs[Idx - (OutArgs.size() + InArgs.size() +
                                                PropertyArgs.size())]
                      .name);
    } else if (Idx < (OutArgs.size() + InArgs.size() + PropertyArgs.size() +
                      InternalPersistentArgs.size() + ExternalArgs.size())) {
      Arg.setName(ExternalArgs[Idx - (OutArgs.size() + InArgs.size() +
                                      PropertyArgs.size() +
                                      InternalPersistentArgs.size())]
                      .name);
    } else {
      Arg.setName(UsedPortProperties[Idx - (OutArgs.size() + InArgs.size() +
                                            PropertyArgs.size() +
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
  for (const auto &arg : PropertyArgs) {
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
    llvm::Type *allocTy = value->getType();
    if (!allocTy || allocTy->isVoidTy()) {
      allocTy = llvm::Type::getDoubleTy(*state.TheContext);
    }
    llvm::Function *TheFunction = state.Builder->GetInsertBlock()->getParent();
    ArgsV = state.CreateEntryBlockAlloca(TheFunction, "LiteralValTemp", allocTy);
    state.Builder->CreateStore(value, ArgsV);
  }
  //    value->print(llvm::outs());
  //    arg->print(llvm::outs());
  //    ArgsV->print(llvm::outs());
  //    ArgsV->getType()->print(llvm::outs());
  //    llvm::outs() << "\n";
  return ArgsV;
};

// Converts a numeric value to another numeric type. Other values are
// returned unchanged.
static llvm::Value *convertScalar(StrideCompiler &state, llvm::Value *value,
                                  llvm::Type *toType) {
  llvm::Type *fromType = value->getType();
  if (fromType == toType) {
    return value;
  }
  auto &B = *state.Builder;
  if (fromType->isIntegerTy() && toType->isIntegerTy()) {
    if (toType->isIntegerTy(1)) {
      return B.CreateICmpNE(value, llvm::ConstantInt::get(fromType, 0),
                            "to_switch");
    }
    // Switches are unsigned 0/1, other integers are signed.
    return B.CreateIntCast(value, toType, !fromType->isIntegerTy(1),
                           "int_cast");
  }
  if (fromType->isIntegerTy() && toType->isFloatingPointTy()) {
    return fromType->isIntegerTy(1) ? B.CreateUIToFP(value, toType, "to_fp")
                                    : B.CreateSIToFP(value, toType, "to_fp");
  }
  if (fromType->isFloatingPointTy() && toType->isIntegerTy()) {
    if (toType->isIntegerTy(1)) {
      return B.CreateFCmpONE(value, llvm::ConstantFP::get(fromType, 0.0),
                             "to_switch");
    }
    return B.CreateFPToSI(value, toType, "to_int");
  }
  if (fromType->isFloatingPointTy() && toType->isFloatingPointTy()) {
    return B.CreateFPCast(value, toType, "fp_cast");
  }
  return value;
}

// Reports a call argument whose scalar type differs from the callee's
// parameter. The callee reads and writes through the pointer with its own
// type, so a mismatch reads garbage or writes past the caller's storage.
static bool checkArgumentType(const std::string &callee,
                              const PrototypeArg &param,
                              const std::optional<llvm::Type *> &argType) {
  if (!param.llvmType || !argType.has_value() || !argType.value()) {
    return true;
  }
  llvm::Type *expected = param.llvmType;
  llvm::Type *actual = argType.value();
  auto isScalar = [](llvm::Type *t) {
    return t->isIntegerTy() || t->isFloatingPointTy();
  };
  if (!isScalar(expected) || !isScalar(actual) || expected == actual) {
    return true;
  }
  std::string expectedText, actualText;
  llvm::raw_string_ostream expectedStream(expectedText);
  llvm::raw_string_ostream actualStream(actualText);
  expected->print(expectedStream);
  actual->print(actualStream);
  LOG_ERROR() << "Type mismatch calling " << callee << ": argument '"
              << param.name << "' expects " << expectedStream.str()
              << " but is " << actualStream.str() << std::endl;
  return false;
}

void processArgGroup(
    StrideCompiler &state,
    const std::vector<std::unique_ptr<ExprAST>> &ArgGroup,
    llvm::Function *CalleeF,
    std::vector<std::pair<llvm::Value *, std::optional<llvm::Type *>>>
        &CallArgs,
    bool isOutputGroup = false,
    std::vector<std::tuple<std::string, llvm::AllocaInst *, llvm::Type *>>
        *tempOuts = nullptr,
    const std::vector<llvm::Type *> *argTypes = nullptr) {
  for (unsigned i = 0, e = ArgGroup.size(); i != e; ++i) {
    size_t paramIdx = CallArgs.size();
    llvm::Argument *calleeArg =
        (paramIdx < CalleeF->arg_size()) ? CalleeF->getArg(paramIdx) : nullptr;

    if (isOutputGroup) {
      if (auto *varExpr = dynamic_cast<VariableExprAST *>(ArgGroup[i].get())) {
        std::string varName = varExpr->getName();
        if (state.NamedValues.find(varName) == state.NamedValues.end() &&
            !state.globalExists(varName)) {
          llvm::Function *TheFunction =
              state.Builder->GetInsertBlock()->getParent();
          // Undeclared outputs are implicit intermediates between chained
          // calls. Use the callee's declared output type so e.g. a switch
          // output is not widened to i32 and stored over neighbouring state.
          llvm::Type *elemTy = llvm::Type::getInt32Ty(*state.TheContext);
          if (argTypes && i < argTypes->size() && (*argTypes)[i] &&
              !(*argTypes)[i]->isVoidTy()) {
            elemTy = (*argTypes)[i];
          }
          llvm::AllocaInst *tempAlloc = state.CreateEntryBlockAlloca(
              TheFunction, varName + "_tmp", elemTy);
          state.Builder->CreateStore(llvm::Constant::getNullValue(elemTy),
                                     tempAlloc);
          if (tempOuts) {
            tempOuts->push_back({varName, tempAlloc, elemTy});
          }
          CallArgs.push_back({tempAlloc, elemTy});
          continue;
        }
      }
    }

    auto [value, type] = ArgGroup[i]->codegen(state);

    if (isOutputGroup && !value) {
      llvm::Function *TheFunction =
          state.Builder->GetInsertBlock()->getParent();
      auto *varExpr = dynamic_cast<VariableExprAST *>(ArgGroup[i].get());
      std::string varName = varExpr ? varExpr->getName() : "call_tmp";
      llvm::Type *elemTy = type.has_value()
                               ? type.value()
                               : llvm::Type::getInt32Ty(*state.TheContext);
      llvm::AllocaInst *tempAlloc =
          state.CreateEntryBlockAlloca(TheFunction, varName + "_tmp", elemTy);
      state.Builder->CreateStore(llvm::Constant::getNullValue(elemTy),
                                 tempAlloc);
      if (tempOuts && varExpr) {
        tempOuts->push_back({varExpr->getName(), tempAlloc, elemTy});
      }
      CallArgs.push_back({tempAlloc, elemTy});
      continue;
    }

    if (!value) {
      LOG_ERROR() << "Can't process argument at index " << i << std::endl;
      return;
    }

    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(ArgGroup[i].get());
      assert(list);
      for (const auto &expr : list->elements()) {
        auto [val, elemType] = expr->codegen(state);
        if (auto *varExpr = dynamic_cast<VariableExprAST *>(expr.get())) {
          auto indeces = varExpr->getIndices();
          if (!indeces.empty() && val->getType()->isPointerTy()) {
            std::vector<llvm::Value *> idxList;
            auto idx = indeces[0];
            const size_t *intIdx = std::get_if<size_t>(&idx);
            if (intIdx) {
              idxList.push_back(llvm::ConstantInt::get(
                  *state.TheContext, llvm::APInt(64, *intIdx)));
            }
            const std::string *strIdx = std::get_if<std::string>(&idx);
            if (strIdx) {
              auto it = state.NamedValues.find(*strIdx);
              if (it != state.NamedValues.end()) {
                llvm::Value *indexVal = it->second.first;
                if (indexVal->getType()->isPointerTy()) {
                  indexVal = state.Builder->CreateLoad(
                      it->second.second.value(), indexVal, *strIdx);
                }
                idxList.push_back(indexVal);
              }
            }
            if (!idxList.empty()) {
              llvm::Type *gepElemType =
                  elemType.value_or(llvm::Type::getDoubleTy(*state.TheContext));
              if (gepElemType->isArrayTy()) {
                gepElemType = static_cast<llvm::ArrayType *>(gepElemType)
                                  ->getElementType();
              }
              val = state.Builder->CreateGEP(gepElemType, val, idxList);
              elemType = gepElemType;
            }
          }
        }
        size_t currentIdx = CallArgs.size();
        llvm::Argument *currentArg = (currentIdx < CalleeF->arg_size())
                                         ? CalleeF->getArg(currentIdx)
                                         : nullptr;
        auto newArg = currentArg ? func(val, elemType, currentArg, state) : val;
        if (!newArg) {
          LOG_ERROR() << "Can't process argument: "
                      << std::string(value->getName()) << std::endl;
          return;
        }
        CallArgs.push_back(
            std::pair<llvm::Value *, std::optional<llvm::Type *>>{
                std::move(newArg), elemType});
        if (currentArg && CallArgs.back().first->getType()->isPointerTy() &&
            !currentArg->getType()->isPointerTy()) {
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
        auto indeces = varExpr->getIndices();
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
            auto it = state.NamedValues.find(*strIdx);
            if (it != state.NamedValues.end()) {
              llvm::Value *indexVal = it->second.first;
              if (indexVal->getType()->isPointerTy()) {
                indexVal = state.Builder->CreateLoad(it->second.second.value(),
                                                     indexVal, *strIdx);
              }
              idxList.push_back(indexVal);
            } else {
              assert(0 == 1);
            }
          }
          if (!type.has_value()) {
            LOG_ERROR() << "No type for: " << std::string(value->getName())
                        << std::endl;
            return;
          }
          if (Logger::isDebugEnabled()) {
            std::string s;
            llvm::raw_string_ostream rso(s);
            type.value()->print(rso);
            LOG_DEBUG() << rso.str() << "\n";
          }
          llvm::Type *gepElemType = type.value();
          if (gepElemType->isArrayTy()) {
            gepElemType =
                static_cast<llvm::ArrayType *>(gepElemType)->getElementType();
          }
          auto *GEP = state.Builder->CreateGEP(gepElemType, value, idxList);
          value = GEP;
          // The pointer now addresses a single element, so later loads and
          // array grouping must use the element type, not the array type.
          type = gepElemType;
        }
        newArgVal = value;
      } else {
        newArgVal = calleeArg ? func(value, type, calleeArg, state) : value;
      }

      if (calleeArg && calleeArg->getType()->isPointerTy() &&
          !newArgVal->getType()->isPointerTy()) {
        llvm::Function *TheFunction =
            state.Builder->GetInsertBlock()->getParent();
        llvm::Type *elemTy =
            type.has_value() ? type.value() : newArgVal->getType();
        auto *varExpr = dynamic_cast<VariableExprAST *>(ArgGroup[i].get());
        std::string varName = varExpr ? varExpr->getName() : "call_tmp";
        llvm::AllocaInst *tempAlloc =
            state.CreateEntryBlockAlloca(TheFunction, varName + "_tmp", elemTy);

        if (isOutputGroup) {
          newArgVal = tempAlloc;
          if (tempOuts && varExpr) {
            tempOuts->push_back({varExpr->getName(), tempAlloc, elemTy});
          }
        } else {
          state.Builder->CreateStore(newArgVal, tempAlloc);
          newArgVal = tempAlloc;
        }
      } else if (calleeArg && !calleeArg->getType()->isPointerTy() &&
                 newArgVal->getType()->isPointerTy()) {
        llvm::Type *loadTy = type.has_value()
                                 ? type.value()
                                 : state.pointerElementTypes[newArgVal];
        if (!loadTy)
          loadTy = llvm::Type::getDoubleTy(*state.TheContext);
        newArgVal = state.Builder->CreateLoad(loadTy, newArgVal, "");
      }

      if (!newArgVal) {
        LOG_ERROR() << "Can't process argument: "
                    << std::string(value->getName()) << std::endl;
        return;
      }
      CallArgs.push_back({std::move(newArgVal), type});
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
  LOG_INFO() << " == CallExprAST codegen for "
             << std::string(CalleeF->getName()) << " -> " << instanceName
             << std::endl;

  if (state.debugEmitter && state.debugEmitter->isInitialized() && m_line > 0) {
    state.debugEmitter->emitLocation(*state.Builder, m_line, m_column);
  }

  std::vector<std::pair<llvm::Value *, std::optional<llvm::Type *>>> CallArgs;
  std::vector<std::tuple<std::string, llvm::AllocaInst *, llvm::Type *>>
      tempOuts;
  if (callType != CallableType::External) {
    processArgGroup(state, OutArgs, CalleeF, CallArgs, true, &tempOuts,
                    &OutArgsDataType);
  }

  auto protoIt = state.FunctionProtos.find(Callee);
  size_t expectedInArgs = 0;
  if (protoIt != state.FunctionProtos.end()) {
    expectedInArgs = protoIt->second->getInArgs().size();
  } else if (CalleeF) {
    size_t outCount = OutArgs.size();
    size_t portPropCount = PortPropArgs.size();
    size_t externalCount = ExternalArgs.size();
    size_t stateCount = calleeNeedsState ? 1 : 0;
    if (CalleeF->arg_size() >=
        outCount + portPropCount + externalCount + stateCount) {
      expectedInArgs = CalleeF->arg_size() - outCount - portPropCount -
                       externalCount - stateCount;
    }
  }

  if (callType == CallableType::Module || callType == CallableType::Loop ||
      (callType == CallableType::Reaction && expectedInArgs > 0)) {

    int outArgCount = CallArgs.size();
    processArgGroup(state, InArgs, CalleeF, CallArgs, false);

    if (protoIt != state.FunctionProtos.end()) {
      const auto &proto = *protoIt->second;
      for (size_t k = 0; k < proto.getOutArgs().size() &&
                         k < static_cast<size_t>(outArgCount);
           ++k) {
        checkArgumentType(Callee, proto.getOutArgs()[k], CallArgs[k].second);
      }
      // Inputs grouped into one array below are not checked here.
      if (CallArgs.size() <= static_cast<size_t>(outArgCount) + 1) {
        for (size_t k = 0; k < proto.getInArgs().size() &&
                           outArgCount + k < CallArgs.size();
             ++k) {
          checkArgumentType(Callee, proto.getInArgs()[k],
                            CallArgs[outArgCount + k].second);
        }
      }
    }

    // Group main inputs into array if not external.
    // Modules and Loops take a single entity, so
    // multiple inputs must be grouped into an array

    if (CallArgs.size() > outArgCount + 1) {
      size_t arraySize = CallArgs.size() - outArgCount;
      llvm::Type *elemType =
          (!InArgsDataType.empty() && InArgsDataType[0] &&
           !InArgsDataType[0]->isVoidTy())
              ? InArgsDataType[0]
              : ((outArgCount < CallArgs.size() &&
                  CallArgs[outArgCount].second.has_value() &&
                  CallArgs[outArgCount].second.value() &&
                  !CallArgs[outArgCount].second.value()->isVoidTy())
                     ? CallArgs[outArgCount].second.value()
                     : llvm::Type::getInt32Ty(*state.TheContext));

      llvm::Function *TheFunction =
          state.Builder->GetInsertBlock()->getParent();
      size_t allocCount = (arraySize > 0) ? arraySize : 1;
      llvm::ArrayType *arrayTy = llvm::ArrayType::get(elemType, allocCount);
      llvm::AllocaInst *arrayAlloc = state.CreateEntryBlockAlloca(
          TheFunction, "array_intermediate", arrayTy);
      llvm::Value *zero = state.Builder->getInt32(0);
      llvm::Value *firstElemPtr = state.Builder->CreateInBoundsGEP(
          arrayTy, arrayAlloc, {zero, zero}, "array_base");

      for (size_t i = 0; i < arraySize; ++i) {
        llvm::Value *index = llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(*state.TheContext), i);

        llvm::Value *gepPtr = state.Builder->CreateInBoundsGEP(
            elemType, firstElemPtr, index, "array_element_ptr");

        llvm::Value *val = CallArgs[outArgCount + i].first;
        if (val->getType()->isPointerTy()) {
          llvm::Type *loadType = CallArgs[outArgCount + i].second.has_value()
                                     ? CallArgs[outArgCount + i].second.value()
                                     : elemType;
          val = state.Builder->CreateLoad(loadType, val, "array_elem");
        } else if (val->getType() != elemType) {
          if (val->getType()->isIntegerTy() && elemType->isFloatingPointTy()) {
            val = state.Builder->CreateSIToFP(val, elemType, "array_cast");
          } else if (val->getType()->isFloatingPointTy() &&
                     elemType->isIntegerTy()) {
            val = state.Builder->CreateFPToSI(val, elemType, "array_cast");
          }
        }
        state.Builder->CreateStore(val, gepPtr);
      }
      CallArgs.resize(outArgCount);
      CallArgs.push_back({firstElemPtr, elemType});
      if (Logger::isDebugEnabled()) {
        std::string s;
        llvm::raw_string_ostream rso(s);
        rso << "Grouped inputs into array: ";
        arrayAlloc->print(rso);
        LOG_DEBUG() << rso.str() << "\n";
      }
    }
  } else if (callType == CallableType::External) {
    if (CalleeF->hasFnAttribute("stride.constrained_list")) {
      size_t prefixCount = 0;
      if (CalleeF->hasFnAttribute("stride.variadic_prefix")) {
        auto attr = CalleeF->getFnAttribute("stride.variadic_prefix");
        prefixCount = std::stoull(attr.getValueAsString().str());
      }
      if (prefixCount > 0 && prefixCount <= InArgs.size()) {
        std::vector<std::unique_ptr<ExprAST>> prefixArgs;
        prefixArgs.reserve(prefixCount);
        for (size_t i = 0; i < prefixCount; ++i) {
          prefixArgs.push_back(std::move(InArgs[i]));
        }
        processArgGroup(state, prefixArgs, CalleeF, CallArgs);
        for (size_t i = 0; i < prefixCount; ++i) {
          InArgs[i] = std::move(prefixArgs[i]);
        }
      }

      size_t numVarArgs =
          (InArgs.size() >= prefixCount) ? (InArgs.size() - prefixCount) : 0;
      llvm::Type *i32Ty = llvm::Type::getInt32Ty(*state.TheContext);

      if (CalleeF->hasFnAttribute("stride.variadic_heterogeneous")) {
        llvm::Type *doubleTy = llvm::Type::getDoubleTy(*state.TheContext);
        llvm::Type *strTy = state.typesMap[DataTypes::String];
        llvm::StructType *fmtArgTy = llvm::StructType::get(
            *state.TheContext, {i32Ty, i32Ty, doubleTy, strTy});

        llvm::Function *TheFunction =
            state.Builder->GetInsertBlock()->getParent();
        size_t allocCount = (numVarArgs > 0) ? numVarArgs : 1;
        llvm::ArrayType *arrayTy = llvm::ArrayType::get(fmtArgTy, allocCount);
        llvm::AllocaInst *arrayAlloc =
            state.CreateEntryBlockAlloca(TheFunction, "varg_fmt_array", arrayTy);
        llvm::Value *zero = state.Builder->getInt32(0);
        llvm::Value *firstElemPtr = state.Builder->CreateInBoundsGEP(
            arrayTy, arrayAlloc, {zero, zero}, "varg_fmt_base");

        for (size_t k = 0; k < numVarArgs; ++k) {
          size_t argIdx = prefixCount + k;
          auto [argVal, argType] = InArgs[argIdx]->codegen(state);
          llvm::Type *valType =
              (argType.has_value() && argType.value() &&
               !argType.value()->isVoidTy())
                  ? argType.value()
                  : ((argIdx < InArgsDataType.size() &&
                      InArgsDataType[argIdx] &&
                      !InArgsDataType[argIdx]->isVoidTy())
                         ? InArgsDataType[argIdx]
                         : argVal->getType());
          if (argVal->getType()->isPointerTy()) {
            if (auto *varExpr =
                    dynamic_cast<VariableExprAST *>(InArgs[argIdx].get())) {
              auto indeces = varExpr->getIndices();
              if (!indeces.empty()) {
                std::vector<llvm::Value *> idxList;
                auto idx = indeces[0];
                if (const size_t *intIdx = std::get_if<size_t>(&idx)) {
                  idxList.push_back(llvm::ConstantInt::get(
                      *state.TheContext, llvm::APInt(64, *intIdx)));
                } else if (const std::string *strIdx =
                               std::get_if<std::string>(&idx)) {
                  auto it = state.NamedValues.find(*strIdx);
                  if (it != state.NamedValues.end()) {
                    llvm::Value *indexVal = it->second.first;
                    if (indexVal->getType()->isPointerTy()) {
                      indexVal = state.Builder->CreateLoad(
                          it->second.second.value(), indexVal, *strIdx);
                    }
                    idxList.push_back(indexVal);
                  }
                }
                llvm::Type *gepElemType = valType;
                if (gepElemType->isArrayTy()) {
                  gepElemType = static_cast<llvm::ArrayType *>(gepElemType)
                                    ->getElementType();
                  valType = gepElemType;
                }
                argVal = state.Builder->CreateGEP(gepElemType, argVal, idxList);
              }
            }
            argVal = state.Builder->CreateLoad(valType, argVal, "varg_val");
          }

          llvm::Value *elemPtr = state.Builder->CreateInBoundsGEP(
              fmtArgTy, firstElemPtr, state.Builder->getInt32(k),
              "varg_elem_ptr");
          llvm::Value *tagPtr = state.Builder->CreateStructGEP(
              fmtArgTy, elemPtr, 0, "varg_tag_ptr");

          if (valType == strTy || argVal->getType() == strTy) {
            state.Builder->CreateStore(state.Builder->getInt32(3), tagPtr);
            llvm::Value *strFieldPtr = state.Builder->CreateStructGEP(
                fmtArgTy, elemPtr, 3, "varg_str_ptr");
            state.Builder->CreateStore(argVal, strFieldPtr);
          } else if (valType->isFloatingPointTy() ||
                     argVal->getType()->isFloatingPointTy()) {
            state.Builder->CreateStore(state.Builder->getInt32(1), tagPtr);
            llvm::Value *realFieldPtr = state.Builder->CreateStructGEP(
                fmtArgTy, elemPtr, 2, "varg_real_ptr");
            if (argVal->getType() != doubleTy) {
              argVal = state.Builder->CreateFPCast(argVal, doubleTy,
                                                   "varg_fpcast");
            }
            state.Builder->CreateStore(argVal, realFieldPtr);
          } else if (valType->isIntegerTy(1) ||
                     argVal->getType()->isIntegerTy(1)) {
            state.Builder->CreateStore(state.Builder->getInt32(2), tagPtr);
            llvm::Value *intFieldPtr = state.Builder->CreateStructGEP(
                fmtArgTy, elemPtr, 1, "varg_bool_ptr");
            llvm::Value *extVal =
                state.Builder->CreateZExt(argVal, i32Ty, "varg_zext");
            state.Builder->CreateStore(extVal, intFieldPtr);
          } else {
            state.Builder->CreateStore(state.Builder->getInt32(0), tagPtr);
            llvm::Value *intFieldPtr = state.Builder->CreateStructGEP(
                fmtArgTy, elemPtr, 1, "varg_int_ptr");
            if (argVal->getType() != i32Ty &&
                argVal->getType()->isIntegerTy()) {
              argVal = state.Builder->CreateIntCast(argVal, i32Ty, true,
                                                    "varg_intcast");
            }
            state.Builder->CreateStore(argVal, intFieldPtr);
          }
        }
        CallArgs.push_back({firstElemPtr, fmtArgTy});
        CallArgs.push_back({state.Builder->getInt32(numVarArgs), i32Ty});
      } else {
        llvm::Type *elemType =
            (prefixCount < InArgsDataType.size() &&
             InArgsDataType[prefixCount] &&
             !InArgsDataType[prefixCount]->isVoidTy())
                ? InArgsDataType[prefixCount]
                : state.typesMap[DataTypes::String];

        llvm::Function *TheFunction =
            state.Builder->GetInsertBlock()->getParent();
        size_t allocCount = (numVarArgs > 0) ? numVarArgs : 1;
        llvm::ArrayType *arrayTy = llvm::ArrayType::get(elemType, allocCount);
        llvm::AllocaInst *arrayAlloc =
            state.CreateEntryBlockAlloca(TheFunction, "clist_array", arrayTy);
        llvm::Value *zero = state.Builder->getInt32(0);
        llvm::Value *firstElemPtr = state.Builder->CreateInBoundsGEP(
            arrayTy, arrayAlloc, {zero, zero}, "clist_base");

        for (size_t k = 0; k < numVarArgs; ++k) {
          size_t argIdx = prefixCount + k;
          auto [argVal, argType] = InArgs[argIdx]->codegen(state);
          llvm::Type *valType =
              (argType.has_value() && argType.value() &&
               !argType.value()->isVoidTy())
                  ? argType.value()
                  : elemType;
          if (argVal->getType()->isPointerTy()) {
            argVal = state.Builder->CreateLoad(valType, argVal, "elem_val");
          }
          llvm::Value *index = llvm::ConstantInt::get(i32Ty, k);
          llvm::Value *gepPtr = state.Builder->CreateInBoundsGEP(
              elemType, firstElemPtr, index, "elem_ptr");
          state.Builder->CreateStore(argVal, gepPtr);
        }
        CallArgs.push_back({firstElemPtr, elemType});
        CallArgs.push_back({state.Builder->getInt32(numVarArgs), i32Ty});
      }
    } else {
      processArgGroup(state, InArgs, CalleeF, CallArgs);
    }
    // processArgGroup(state, InternalArgs, CalleeF, CallArgs)
    if (CalleeF->hasFnAttribute("stride.struct_out") && !OutArgs.empty()) {
      // Struct results are written through a trailing out pointer
      auto [outPtr, outTy] = OutArgs[0]->codegen(state);
      if (auto *varExpr = dynamic_cast<VariableExprAST *>(OutArgs[0].get())) {
        auto indeces = varExpr->getIndices();
        if (!indeces.empty() && outPtr && outPtr->getType()->isPointerTy()) {
          std::vector<llvm::Value *> idxList;
          auto idx = indeces[0];
          const size_t *intIdx = std::get_if<size_t>(&idx);
          if (intIdx) {
            idxList.push_back(llvm::ConstantInt::get(*state.TheContext,
                                                     llvm::APInt(64, *intIdx)));
          }
          const std::string *strIdx = std::get_if<std::string>(&idx);
          if (strIdx) {
            auto it = state.NamedValues.find(*strIdx);
            if (it != state.NamedValues.end()) {
              llvm::Value *indexVal = it->second.first;
              if (indexVal->getType()->isPointerTy()) {
                indexVal = state.Builder->CreateLoad(it->second.second.value(),
                                                     indexVal, *strIdx);
              }
              idxList.push_back(indexVal);
            }
          }
          if (!idxList.empty()) {
            llvm::Type *gepElemType =
                outTy.value_or(state.typesMap[DataTypes::String]);
            if (gepElemType->isArrayTy()) {
              gepElemType =
                  static_cast<llvm::ArrayType *>(gepElemType)->getElementType();
            }
            outPtr = state.Builder->CreateGEP(gepElemType, outPtr, idxList);
            outTy = gepElemType;
          }
        }
      }
      if (outPtr && outPtr->getType()->isPointerTy()) {
        CallArgs.push_back({outPtr, outTy});
      } else {
        LOG_ERROR() << "No destination storage for struct result of " << Callee
                    << std::endl;
      }
    }
  }
  processArgGroup(state, PropertyArgs, CalleeF, CallArgs);

  if (calleeNeedsState) {
    llvm::Value *statePtrVal = nullptr;
    llvm::Type *ptrTy = llvm::PointerType::get(*state.TheContext, 0);

    if (state.currentFunctionStatePtr && state.currentFunctionStateInfo) {
      auto childIt =
          state.currentFunctionStateInfo->childIndices.find(funcInstance);
      if (childIt == state.currentFunctionStateInfo->childIndices.end()) {
        std::string childName =
            funcInstance ? ASTQuery::getNodeName(funcInstance) : Callee;
        for (const auto &pair : state.currentFunctionStateInfo->childIndices) {
          if (pair.first && ASTQuery::getNodeName(pair.first) == childName) {
            childIt =
                state.currentFunctionStateInfo->childIndices.find(pair.first);
            break;
          }
        }
      }
      if (childIt != state.currentFunctionStateInfo->childIndices.end()) {
        statePtrVal = state.Builder->CreateStructGEP(
            state.currentFunctionStateInfo->structType,
            state.currentFunctionStatePtr, childIt->second,
            Callee + "_child_state");
      }
    }

    if (!statePtrVal) {
      const auto *calleeInfo = state.getStateStructInfo(funcInstance);
      if (!calleeInfo && state.m_tree) {
        calleeInfo = state.getStateStructInfo(
            ASTQuery::findDeclarationByName(Callee, {}, state.m_tree));
      }
      if (calleeInfo && calleeInfo->structType) {
        llvm::Function *currFunc = state.Builder->GetInsertBlock()->getParent();
        statePtrVal = state.CreateEntryBlockAlloca(currFunc, Callee + "_state",
                                                   calleeInfo->structType);
        llvm::Constant *initConst =
            calleeInfo->defaultConstant
                ? calleeInfo->defaultConstant
                : llvm::Constant::getNullValue(calleeInfo->structType);
        state.Builder->CreateStore(initConst, statePtrVal);
      }
    }

    if (statePtrVal) {
      CallArgs.push_back({statePtrVal, ptrTy});
    }
  }

  if (callType == CallableType::Reaction || !ExternalArgs.empty()) {
    processArgGroup(state, ExternalArgs, CalleeF, CallArgs);
  }

  for (unsigned i = 0, e = PortPropArgs.size(); i != e; ++i) {
    auto [value, type] = PortPropArgs[i]->codegen(state);
    if (value && value->getType()->isPointerTy()) {
      llvm::Type *elemTy = type.has_value()
                               ? type.value()
                               : llvm::Type::getInt32Ty(*state.TheContext);
      value = state.Builder->CreateLoad(elemTy, value, "port_prop_val");
    }
    CallArgs.push_back({std::move(value), type});
  }

  if (Logger::isDebugEnabled()) {
    std::string s;
    llvm::raw_string_ostream rso(s);
    rso << "Callee:\n";
    CalleeF->print(rso);
    rso << "\nCall Arguments: ";
    for (const auto &arg : CallArgs) {
      arg.first->print(rso);
      rso << "|, ";
    }
    LOG_DEBUG() << rso.str() << "\n";
  }
  llvm::CallInst *call;

  if (callType == CallableType::Reaction && expectedInArgs == 0 &&
      !InArgs.empty()) {
    llvm::Value *CondV = InArgs[0]->codegen(state).first;
    if (Logger::isDebugEnabled()) {
      std::string s;
      llvm::raw_string_ostream rso(s);
      CondV->print(rso);
      LOG_DEBUG() << "CondV: " << rso.str() << "\n";
    }
    if (CondV->getType()->isPointerTy()) {
      CondV = state.Builder->CreateLoad(
          llvm::Type::getInt1Ty(*state.TheContext), CondV, "cond");
    }
    // Convert condition to a bool by comparing non-equal to 0.0 or 0.
    if (CondV->getType()->isDoubleTy()) {
      CondV = state.Builder->CreateFCmpONE(
          CondV, llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(0.0)),
          "ifcond");
    } else if (CondV->getType()->isIntegerTy() &&
               !CondV->getType()->isIntegerTy(1)) {
      CondV = state.Builder->CreateICmpNE(
          CondV, llvm::ConstantInt::get(CondV->getType(), 0), "ifcond");
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
        state.Builder->CreateCall(CalleeF, CallArgsValues, CalleeF->getReturnType()->isVoidTy() ? "" : CalleeF->getName());

    state.Builder->CreateBr(MergeBB);
    // ThenBB = state.Builder->GetInsertBlock();
    state.Builder->SetInsertPoint(MergeBB);
  } else {
    std::vector<llvm::Value *> CallArgsValues;
    for (const auto &p : CallArgs) {
      CallArgsValues.push_back(p.first);
    }
    if (callType == CallableType::External) {
      // A polymorphic module variant may only find a platform overload for
      // another numeric type (e.g. fabs for an i32 Abs). Convert to it.
      auto *FT = CalleeF->getFunctionType();
      for (size_t i = 0; i < CallArgsValues.size() && i < FT->getNumParams();
           ++i) {
        CallArgsValues[i] =
            convertScalar(state, CallArgsValues[i], FT->getParamType(i));
      }
    }
    if (CallArgsValues.size() != CalleeF->getFunctionType()->getNumParams() &&
        !CalleeF->getFunctionType()->isVarArg()) {
      LOG_ERROR() << "CallExprAST signature mismatch for " << Callee
                  << ": expected " << CalleeF->getFunctionType()->getNumParams()
                  << " params, got " << CallArgsValues.size() << " args!"
                  << std::endl;
      std::string funcStr;
      llvm::raw_string_ostream rso(funcStr);
      CalleeF->print(rso);
      LOG_ERROR() << "CalleeF:\n" << rso.str() << std::endl;
      for (size_t i = 0; i < CallArgsValues.size(); ++i) {
        std::string argStr;
        llvm::raw_string_ostream argRso(argStr);
        if (CallArgsValues[i]) {
          CallArgsValues[i]->print(argRso);
        } else {
          argRso << "null";
        }
        LOG_ERROR() << "  arg[" << i << "]: " << argRso.str() << std::endl;
      }
    }
    call =
        state.Builder->CreateCall(CalleeF, CallArgsValues, CalleeF->getReturnType()->isVoidTy() ? "" : CalleeF->getName());
  }

  // Write to output
  if (callType == CallableType::External) {
    if (!call->getType()->isVoidTy() && OutArgs.size() > 0) {
      auto [outVal, outType] = OutArgs[0]->codegen(state);
      if (outVal->getType()->isPointerTy()) {
        llvm::Value *result = call;
        if (outType.has_value() && outType.value()) {
          result = convertScalar(state, result, outType.value());
        }
        state.Builder->CreateStore(result, outVal);
      }
    }
  } else {
    for (const auto &[varName, allocaInst, elemTy] : tempOuts) {
      llvm::Value *loadedVal =
          state.Builder->CreateLoad(elemTy, allocaInst, varName);
      state.NamedValues[varName] = {loadedVal, elemTy};
    }
  }

  return {call, std::nullopt};
}

std::string
LLVMCommandAST::substituteTokens(const std::string &processingTemplate,
                                 const std::vector<std::string> &inTokens,
                                 const std::vector<std::string> &outTokens,
                                 const std::string &arrayIndexStr) {
  std::string result = processingTemplate;

  auto replaceAll = [](std::string &str, const std::string &from,
                       const std::string &to) {
    if (from.empty())
      return;
    size_t startPos = 0;
    while ((startPos = str.find(from, startPos)) != std::string::npos) {
      str.replace(startPos, from.length(), to);
      startPos += to.length();
    }
  };

  replaceAll(result, "%%array_index%%", arrayIndexStr);
  replaceAll(result, "%array_index%", arrayIndexStr);
  replaceAll(result, "array_index", arrayIndexStr);
  replaceAll(result, "%%bundle_index%%", arrayIndexStr);
  replaceAll(result, "%bundle_index%", arrayIndexStr);
  replaceAll(result, "bundle_index", arrayIndexStr);

  for (size_t i = 0; i < outTokens.size(); ++i) {
    replaceAll(result, "%%outtokens:" + std::to_string(i) + "%%", outTokens[i]);
    replaceAll(result, "%outtokens:" + std::to_string(i) + "%", outTokens[i]);
    replaceAll(result, "outtokens:" + std::to_string(i), outTokens[i]);
  }
  if (!outTokens.empty()) {
    replaceAll(result, "%%outtokens%%", outTokens[0]);
    replaceAll(result, "%outtokens%", outTokens[0]);
    replaceAll(result, "outtokens", outTokens[0]);
  }

  for (size_t i = 0; i < inTokens.size(); ++i) {
    replaceAll(result, "%%intokens:" + std::to_string(i) + "%%", inTokens[i]);
    replaceAll(result, "%intokens:" + std::to_string(i) + "%", inTokens[i]);
    replaceAll(result, "intokens:" + std::to_string(i), inTokens[i]);
  }

  return result;
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
LLVMCommandAST::codegen(StrideCompiler &state) {
  std::vector<std::pair<llvm::Value *, std::optional<llvm::Type *>>> CallArgs;

  auto processArg = [&](ExprAST *expr, llvm::Value *val,
                        std::optional<llvm::Type *> elemType)
      -> std::pair<llvm::Value *, std::optional<llvm::Type *>> {
    if (auto *varExpr = dynamic_cast<VariableExprAST *>(expr)) {
      auto indeces = varExpr->getIndices();
      if (!indeces.empty() && val->getType()->isPointerTy()) {
        std::vector<llvm::Value *> idxList;
        auto idx = indeces[0];
        const size_t *intIdx = std::get_if<size_t>(&idx);
        if (intIdx) {
          idxList.push_back(llvm::ConstantInt::get(*state.TheContext,
                                                   llvm::APInt(64, *intIdx)));
        }
        const std::string *strIdx = std::get_if<std::string>(&idx);
        if (strIdx) {
          auto it = state.NamedValues.find(*strIdx);
          if (it != state.NamedValues.end()) {
            llvm::Value *indexVal = it->second.first;
            if (indexVal->getType()->isPointerTy()) {
              indexVal = state.Builder->CreateLoad(it->second.second.value(),
                                                   indexVal, *strIdx);
            }
            idxList.push_back(indexVal);
          }
        }
        if (!idxList.empty()) {
          llvm::Type *gepElemType =
              elemType.value_or(llvm::Type::getDoubleTy(*state.TheContext));
          if (gepElemType->isArrayTy()) {
            gepElemType =
                static_cast<llvm::ArrayType *>(gepElemType)->getElementType();
          }
          val = state.Builder->CreateGEP(gepElemType, val, idxList);
          elemType = gepElemType;
        }
      }
    }
    if (val->getType()->isPointerTy()) {
      llvm::Type *loadTy = (elemType.has_value() && elemType.value() &&
                            !elemType.value()->isVoidTy())
                               ? elemType.value()
                               : llvm::Type::getDoubleTy(*state.TheContext);
      val = state.Builder->CreateLoad(loadTy, val, "");
    }
    return {val, elemType};
  };

  std::vector<std::string> inArgSourceTypes;
  for (unsigned i = 0, e = InArgs.size(); i != e; ++i) {
    auto [value, type] = InArgs[i]->codegen(state);
    if (!value) {
      return {nullptr, std::nullopt};
    }
    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(InArgs[i].get());
      assert(list);
      for (const auto &expr : list->elements()) {
        auto [exprValue, exprType] = expr->codegen(state);
        auto processed = processArg(expr.get(), exprValue, exprType);
        if (!processed.first) {
          return {nullptr, std::nullopt};
        }
        CallArgs.push_back(processed);
        inArgSourceTypes.push_back(expr->dataType);
      }
    } else {
      auto processed = processArg(InArgs[i].get(), value, type);
      if (!processed.first) {
        return {nullptr, std::nullopt};
      }
      CallArgs.push_back(processed);
      inArgSourceTypes.push_back(InArgs[i]->dataType);
    }
  }

  size_t inArgCount = CallArgs.size();
  // e.g. a UInt8 into an Int input is zero extended
  for (size_t i = 0; i < inArgCount && i < InArgDataTypes.size(); ++i) {
    auto *converted = state.convertToDataType(
        CallArgs[i].first, inArgSourceTypes[i], InArgDataTypes[i]);
    if (converted != CallArgs[i].first) {
      CallArgs[i] = {converted, converted->getType()};
    }
  }

  for (unsigned i = 0, e = OutArgs.size(); i != e; ++i) {
    auto [value, type] = OutArgs[i]->codegen(state);
    if (!value) {
      return {nullptr, std::nullopt};
    }
    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(OutArgs[i].get());
      assert(list);
      for (const auto &expr : list->elements()) {
        auto [exprValue, exprType] = expr->codegen(state);
        auto processed = processArg(expr.get(), exprValue, exprType);
        if (!processed.first) {
          return {nullptr, std::nullopt};
        }
        CallArgs.push_back(processed);
      }
    } else {
      auto processed = processArg(OutArgs[i].get(), value, type);
      if (!processed.first) {
        return {nullptr, std::nullopt};
      }
      CallArgs.push_back(processed);
    }
  }

  size_t outArgStart = inArgCount;
  size_t outArgCount = CallArgs.size() - inArgCount;

  for (unsigned i = 0, e = ExternalArgs.size(); i != e; ++i) {
    auto [value, type] = ExternalArgs[i]->codegen(state);
    if (!value) {
      return {nullptr, std::nullopt};
    }
    if (value->getType()->isTokenTy()) {
      auto *list = dynamic_cast<ListExprAST *>(ExternalArgs[i].get());
      if (list) {
        for (const auto &expr : list->elements()) {
          auto [exprValue, exprType] = expr->codegen(state);
          auto processed = processArg(expr.get(), exprValue, exprType);
          if (!processed.first) {
            return {nullptr, std::nullopt};
          }
          CallArgs.push_back(processed);
        }
      }
    } else {
      auto processed = processArg(ExternalArgs[i].get(), value, type);
      if (!processed.first) {
        return {nullptr, std::nullopt};
      }
      CallArgs.push_back(processed);
    }
  }

  for (unsigned i = 0, e = PortPropArgs.size(); i != e; ++i) {
    auto [value, type] = PortPropArgs[i]->codegen(state);
    CallArgs.push_back({value, type});
  }

  llvm::Value *outval{nullptr};
  std::optional<llvm::Type *> outtype;

  std::vector<std::string> inTokens;
  std::vector<std::string> outTokens;

  for (size_t i = 0; i < inArgCount; ++i) {
    llvm::Value *val = CallArgs[i].first;
    if (val->hasName() && !val->getName().empty()) {
      inTokens.push_back("%" + val->getName().str());
    } else if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(val)) {
      inTokens.push_back(std::to_string(ci->getSExtValue()));
    } else if (auto *fp = llvm::dyn_cast<llvm::ConstantFP>(val)) {
      inTokens.push_back(std::to_string(fp->getValueAPF().convertToDouble()));
    } else {
      std::string varName = "in_arg_" + std::to_string(i);
      val->setName(varName);
      inTokens.push_back("%" + varName);
    }
  }

  for (size_t i = 0; i < outArgCount; ++i) {
    outTokens.push_back("%out_" + std::to_string(i));
  }
  if (outTokens.empty()) {
    outTokens.push_back("%res");
  }

  std::string arrayIndexStr = "0";
  std::string substituted =
      substituteTokens(command, inTokens, outTokens, arrayIndexStr);
  LOG_INFO() << "Substituted LLVM IR text: " << substituted << std::endl;

  // Dispatch: llvm:: prefix means a native LLVM IR instruction.
  // Format: "llvm::<mnemonic> [operands...]"
  if (substituted == " = ") {
    outval = CallArgs.size() > 0 ? CallArgs[0].first : nullptr;
    outtype = CallArgs.size() > 0 ? CallArgs[0].second : std::nullopt;
  } else if (substituted.find("llvm::") != std::string::npos) {
    size_t llvmPos = substituted.find("llvm::");
    std::string instr =
        substituted.substr(llvmPos + 6); // strip up to and including "llvm::"
    llvm::Value *lhs = nullptr;
    llvm::Value *rhs = nullptr;

    auto resolveOperand = [&](const std::string &opStr) -> llvm::Value * {
      for (size_t i = 0; i < inTokens.size(); ++i) {
        if (opStr == inTokens[i]) {
          return CallArgs[i].first;
        }
      }
      for (size_t i = 0; i < outTokens.size(); ++i) {
        if (opStr == outTokens[i]) {
          if (outArgStart + i < CallArgs.size()) {
            return CallArgs[outArgStart + i].first;
          }
        }
      }
      return nullptr;
    };

    auto parseLiteral = [&](const std::string &str,
                            llvm::Type *type) -> llvm::Value * {
      try {
        if (type->isDoubleTy()) {
          return llvm::ConstantFP::get(*state.TheContext,
                                       llvm::APFloat(std::stod(str)));
        } else if (type->isFloatTy()) {
          return llvm::ConstantFP::get(*state.TheContext,
                                       llvm::APFloat((float)std::stof(str)));
        } else if (type->isIntegerTy(32)) {
          return llvm::ConstantInt::get(*state.TheContext,
                                        llvm::APInt(32, std::stoi(str)));
        } else if (type->isIntegerTy(64)) {
          return llvm::ConstantInt::get(*state.TheContext,
                                        llvm::APInt(64, std::stoll(str)));
        } else if (type->isIntegerTy(1)) {
          if (str == "true" || str == "1") {
            return llvm::ConstantInt::getTrue(*state.TheContext);
          } else if (str == "false" || str == "0") {
            return llvm::ConstantInt::getFalse(*state.TheContext);
          }
          return llvm::ConstantInt::get(*state.TheContext,
                                        llvm::APInt(1, std::stoi(str)));
        }
      } catch (...) {
      }
      return nullptr;
    };

    if (instr.rfind("select", 0) == 0) {
      size_t firstComma = instr.find(',');
      size_t secondComma =
          (firstComma != std::string::npos) ? instr.find(',', firstComma + 1) : std::string::npos;
      std::string op0Str, op1Str, op2Str;
      if (firstComma != std::string::npos && secondComma != std::string::npos) {
        op0Str = instr.substr(0, firstComma);
        size_t lastSpace = op0Str.find_last_of(" \t");
        if (lastSpace != std::string::npos) {
          op0Str = op0Str.substr(lastSpace + 1);
        }
        op1Str = instr.substr(firstComma + 1, secondComma - firstComma - 1);
        op1Str.erase(0, op1Str.find_first_not_of(" \t"));
        op1Str.erase(op1Str.find_last_not_of(" \t\r\n") + 1);

        op2Str = instr.substr(secondComma + 1);
        op2Str.erase(0, op2Str.find_first_not_of(" \t"));
        op2Str.erase(op2Str.find_last_not_of(" \t\r\n") + 1);
      }

      llvm::Value *cond = resolveOperand(op0Str);
      llvm::Value *trueVal = resolveOperand(op1Str);
      llvm::Value *falseVal = resolveOperand(op2Str);

      if (!cond && CallArgs.size() > 0)
        cond = CallArgs[0].first;
      if (!trueVal && CallArgs.size() > 1)
        trueVal = CallArgs[1].first;
      if (!falseVal && CallArgs.size() > 2)
        falseVal = CallArgs[2].first;

      if (!trueVal && falseVal)
        trueVal = parseLiteral(op1Str, falseVal->getType());
      if (!falseVal && trueVal)
        falseVal = parseLiteral(op2Str, trueVal->getType());

      if (cond && trueVal && falseVal) {
        if (cond->getType()->isIntegerTy() && !cond->getType()->isIntegerTy(1)) {
          cond = state.Builder->CreateICmpNE(
              cond, llvm::ConstantInt::get(cond->getType(), 0), "tobool");
        } else if (cond->getType()->isDoubleTy()) {
          cond = state.Builder->CreateFCmpONE(
              cond, llvm::ConstantFP::get(cond->getType(), 0.0), "tobool");
        }

        if (trueVal->getType() != falseVal->getType()) {
          if (trueVal->getType()->isDoubleTy() &&
              falseVal->getType()->isIntegerTy()) {
            falseVal = state.Builder->CreateSIToFP(
                falseVal, llvm::Type::getDoubleTy(*state.TheContext), "sitofp");
          } else if (trueVal->getType()->isIntegerTy() &&
                     falseVal->getType()->isDoubleTy()) {
            trueVal = state.Builder->CreateSIToFP(
                trueVal, llvm::Type::getDoubleTy(*state.TheContext), "sitofp");
          } else if (trueVal->getType()->isIntegerTy() &&
                     falseVal->getType()->isIntegerTy()) {
            unsigned tBits = trueVal->getType()->getIntegerBitWidth();
            unsigned fBits = falseVal->getType()->getIntegerBitWidth();
            if (tBits < fBits) {
              trueVal = state.Builder->CreateIntCast(
                  trueVal, falseVal->getType(), true, "intcast");
            } else if (fBits < tBits) {
              falseVal = state.Builder->CreateIntCast(
                  falseVal, trueVal->getType(), true, "intcast");
            }
          }
        }

        outval = state.Builder->CreateSelect(cond, trueVal, falseVal, "seltmp");
        outtype = trueVal->getType();
      }
    } else {
      size_t comma = instr.find(',');
      if (comma != std::string::npos) {
        std::string op1Str = instr.substr(0, comma);
        size_t lastSpace = op1Str.find_last_of(" \t");
        if (lastSpace != std::string::npos) {
          op1Str = op1Str.substr(lastSpace + 1);
        }

        std::string op2Str = instr.substr(comma + 1);
        op2Str.erase(0, op2Str.find_first_not_of(" \t"));
        op2Str.erase(op2Str.find_last_not_of(" \t\r\n") + 1);

        lhs = resolveOperand(op1Str);
        rhs = resolveOperand(op2Str);

        if (!rhs && lhs) {
          rhs = parseLiteral(op2Str, lhs->getType());
        }
        if (!lhs && rhs) {
          lhs = parseLiteral(op1Str, rhs->getType());
        }
      } else {
        size_t lastSpace = instr.find_last_of(" \t");
        if (lastSpace != std::string::npos) {
          std::string opStr = instr.substr(lastSpace + 1);
          lhs = resolveOperand(opStr);
        }
        if (!lhs) {
          lhs = CallArgs.size() > 0 ? CallArgs[0].first : nullptr;
        }
        rhs = CallArgs.size() > 1 ? CallArgs[1].first : nullptr;
      }

      if (lhs && rhs) {
        if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
          // If both are integers, match bitwidths and use the integer version of the platform instruction
          unsigned lBits = lhs->getType()->getIntegerBitWidth();
          unsigned rBits = rhs->getType()->getIntegerBitWidth();
          if (lBits < rBits) {
            lhs = state.Builder->CreateIntCast(lhs, rhs->getType(), true, "intcast");
          } else if (rBits < lBits) {
            rhs = state.Builder->CreateIntCast(rhs, lhs->getType(), true, "intcast");
          }

          if (instr.rfind("fcmp ogt", 0) == 0 || instr.rfind("fcmp ugt", 0) == 0) {
            instr = "icmp sgt" + instr.substr(8);
          } else if (instr.rfind("fcmp oge", 0) == 0 || instr.rfind("fcmp uge", 0) == 0) {
            instr = "icmp sge" + instr.substr(8);
          } else if (instr.rfind("fcmp olt", 0) == 0 || instr.rfind("fcmp ult", 0) == 0) {
            instr = "icmp slt" + instr.substr(8);
          } else if (instr.rfind("fcmp ole", 0) == 0 || instr.rfind("fcmp ule", 0) == 0) {
            instr = "icmp sle" + instr.substr(8);
          } else if (instr.rfind("fcmp oeq", 0) == 0 || instr.rfind("fcmp ueq", 0) == 0) {
            instr = "icmp eq" + instr.substr(8);
          } else if (instr.rfind("fcmp one", 0) == 0 || instr.rfind("fcmp une", 0) == 0) {
            instr = "icmp ne" + instr.substr(8);
          } else if (instr.rfind("fadd", 0) == 0) {
            instr = "add" + instr.substr(4);
          } else if (instr.rfind("fsub", 0) == 0) {
            instr = "sub" + instr.substr(4);
          } else if (instr.rfind("fmul", 0) == 0) {
            instr = "mul" + instr.substr(4);
          } else if (instr.rfind("fdiv", 0) == 0) {
            instr = "sdiv" + instr.substr(4);
          } else if (instr.rfind("frem", 0) == 0) {
            instr = "srem" + instr.substr(4);
          }
        } else {
          bool isFloatOp = (instr.rfind("fcmp", 0) == 0 ||
                            instr.rfind("fadd", 0) == 0 ||
                            instr.rfind("fsub", 0) == 0 ||
                            instr.rfind("fmul", 0) == 0 ||
                            instr.rfind("fdiv", 0) == 0 ||
                            instr.rfind("frem", 0) == 0);
          if (isFloatOp) {
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isFloatingPointTy()) {
              lhs = state.Builder->CreateSIToFP(lhs, rhs->getType(), "sitofp");
            } else if (rhs->getType()->isIntegerTy() && lhs->getType()->isFloatingPointTy()) {
              rhs = state.Builder->CreateSIToFP(rhs, lhs->getType(), "sitofp");
            } else if (lhs->getType() != rhs->getType()) {
              if (lhs->getType()->isFloatTy() && rhs->getType()->isDoubleTy()) {
                lhs = state.Builder->CreateFPExt(lhs, rhs->getType(), "fpext");
              } else if (rhs->getType()->isFloatTy() && lhs->getType()->isDoubleTy()) {
                rhs = state.Builder->CreateFPExt(rhs, lhs->getType(), "fpext");
              }
            }
          } else {
            if (lhs->getType()->isFloatingPointTy() && rhs->getType()->isIntegerTy()) {
              lhs = state.Builder->CreateFPToSI(lhs, rhs->getType(), "fptosi");
            } else if (rhs->getType()->isFloatingPointTy() && lhs->getType()->isIntegerTy()) {
              rhs = state.Builder->CreateFPToSI(rhs, lhs->getType(), "fptosi");
            }
          }
        }
      }

      if (instr.rfind("icmp sgt", 0) == 0) {
      outval = state.Builder->CreateICmpSGT(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("icmp sge", 0) == 0) {
      outval = state.Builder->CreateICmpSGE(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("icmp slt", 0) == 0) {
      outval = state.Builder->CreateICmpSLT(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("icmp sle", 0) == 0) {
      outval = state.Builder->CreateICmpSLE(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("icmp eq", 0) == 0) {
      outval = state.Builder->CreateICmpEQ(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("icmp ne", 0) == 0) {
      outval = state.Builder->CreateICmpNE(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("icmp ugt", 0) == 0) {
      outval = state.Builder->CreateICmpUGT(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("icmp uge", 0) == 0) {
      outval = state.Builder->CreateICmpUGE(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("icmp ult", 0) == 0) {
      outval = state.Builder->CreateICmpULT(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("icmp ule", 0) == 0) {
      outval = state.Builder->CreateICmpULE(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("fcmp ogt", 0) == 0) {
      outval = state.Builder->CreateFCmpOGT(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("fcmp oge", 0) == 0) {
      outval = state.Builder->CreateFCmpOGE(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("fcmp olt", 0) == 0) {
      outval = state.Builder->CreateFCmpOLT(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("fcmp ole", 0) == 0) {
      outval = state.Builder->CreateFCmpOLE(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("fcmp oeq", 0) == 0) {
      outval = state.Builder->CreateFCmpOEQ(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("fcmp one", 0) == 0) {
      outval = state.Builder->CreateFCmpONE(lhs, rhs);
      outtype = llvm::Type::getInt1Ty(*state.TheContext);
    } else if (instr.rfind("add", 0) == 0) {
      outval = state.Builder->CreateAdd(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("fadd", 0) == 0) {
      outval = state.Builder->CreateFAdd(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("sub", 0) == 0) {
      outval = state.Builder->CreateSub(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("fsub", 0) == 0) {
      outval = state.Builder->CreateFSub(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("mul", 0) == 0) {
      outval = state.Builder->CreateMul(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("fmul", 0) == 0) {
      outval = state.Builder->CreateFMul(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("sdiv", 0) == 0) {
      outval = state.Builder->CreateSDiv(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("udiv", 0) == 0) {
      outval = state.Builder->CreateUDiv(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("fdiv", 0) == 0) {
      outval = state.Builder->CreateFDiv(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("srem", 0) == 0) {
      outval = state.Builder->CreateSRem(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("urem", 0) == 0) {
      outval = state.Builder->CreateURem(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("frem", 0) == 0) {
      outval = state.Builder->CreateFRem(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("shl", 0) == 0) {
      outval = state.Builder->CreateShl(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("lshr", 0) == 0) {
      outval = state.Builder->CreateLShr(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("ashr", 0) == 0) {
      outval = state.Builder->CreateAShr(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("not", 0) == 0) {
      outval = state.Builder->CreateNot(lhs);
      outtype = lhs->getType();
    } else if (instr.rfind("nand", 0) == 0) {
      outval = state.Builder->CreateNot(state.Builder->CreateAnd(lhs, rhs));
      outtype = lhs->getType();
    } else if (instr.rfind("nor", 0) == 0) {
      outval = state.Builder->CreateNot(state.Builder->CreateOr(lhs, rhs));
      outtype = lhs->getType();
    } else if (instr.rfind("and", 0) == 0) {
      outval = state.Builder->CreateAnd(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("or", 0) == 0) {
      outval = state.Builder->CreateOr(lhs, rhs);
      outtype = lhs->getType();
    } else if (instr.rfind("xor", 0) == 0) {
      outval = state.Builder->CreateXor(lhs, rhs);
      outtype = lhs->getType();
    } else {
      LOG_ERROR() << "ERROR: unknown llvm:: instruction: " << instr
                  << std::endl;
      assert(0 == 1);
    }
  }

  } else if (substituted.find('@') != std::string::npos) {
    // External C function call: "@funcName"
    size_t atPos = substituted.find('@');
    size_t endPos = substituted.find_first_of("( \t\r\n", atPos);
    std::string funcName = substituted.substr(
        atPos + 1, (endPos == std::string::npos) ? std::string::npos
                                                 : endPos - (atPos + 1));

    std::vector<llvm::Type *> argTypes;
    std::vector<llvm::Value *> argVals;

    size_t openParen = substituted.find('(', atPos);
    size_t closeParen = substituted.find(')', openParen);

    if (openParen != std::string::npos && closeParen != std::string::npos) {
      std::string argsStr =
          substituted.substr(openParen + 1, closeParen - openParen - 1);

      auto resolveOperand = [&](const std::string &opStr) -> llvm::Value * {
        std::string cleanOp = opStr;
        cleanOp.erase(0, cleanOp.find_first_not_of(" \t"));
        cleanOp.erase(cleanOp.find_last_not_of(" \t\r\n") + 1);
        size_t remainingSpace;
        do {
          remainingSpace = cleanOp.find_first_of(" ");
          // Remove type name to isolate variable if type present
          cleanOp.erase(0, remainingSpace + 1);
        } while (remainingSpace != std::string::npos);
        if (cleanOp.empty())
          return nullptr;

        for (size_t i = 0; i < inTokens.size(); ++i) {
          if (cleanOp == inTokens[i]) {
            return CallArgs[i].first;
          }
        }
        for (size_t i = 0; i < outTokens.size(); ++i) {
          if (cleanOp == outTokens[i]) {
            if (outArgStart + i < CallArgs.size()) {
              return CallArgs[outArgStart + i].first;
            }
          }
        }

        try {
          if (cleanOp.find('.') != std::string::npos) {
            return llvm::ConstantFP::get(*state.TheContext,
                                         llvm::APFloat(std::stod(cleanOp)));
          } else {
            return llvm::ConstantInt::get(*state.TheContext,
                                          llvm::APInt(32, std::stoi(cleanOp)));
          }
        } catch (...) {
        }

        return nullptr;
      };

      size_t start = 0;
      while (start < argsStr.length()) {
        size_t comma = argsStr.find(',', start);
        if (comma == std::string::npos) {
          comma = argsStr.length();
        }
        std::string argStr = argsStr.substr(start, comma - start);
        llvm::Value *val = resolveOperand(argStr);
        if (val) {
          argVals.push_back(val);
          argTypes.push_back(val->getType());
        }
        start = comma + 1;
      }
    } else {
      for (unsigned i = 0; i < inArgCount && i < CallArgs.size(); ++i) {
        argVals.push_back(CallArgs[i].first);
        argTypes.push_back(CallArgs[i].first->getType());
      }
    }

    llvm::Type *retType = llvm::Type::getInt32Ty(*state.TheContext);
    if (substituted.find("void") != std::string::npos) {
      retType = llvm::Type::getVoidTy(*state.TheContext);
    }

    llvm::Function *CalleeF = state.TheModule->getFunction(funcName);
    if (!CalleeF) {
      llvm::FunctionType *FT =
          llvm::FunctionType::get(retType, argTypes, false);
      CalleeF = llvm::Function::Create(FT, llvm::Function::ExternalLinkage,
                                       funcName, *state.TheModule);
    }

    outval = state.Builder->CreateCall(CalleeF, argVals,
                                       retType->isVoidTy() ? "" : funcName);
    if (!retType->isVoidTy()) {
      outtype = retType;
    }

  } else {
    LOG_ERROR() << "ERROR: unrecognised command format: " << substituted
                << std::endl;
    assert(0 == 1);
  }

  return {outval, outtype};
}

bool CallExprAST::writesTo(const std::string &varName) const {
  for (const auto &out : OutArgs) {
    if (auto *varExpr = dynamic_cast<VariableExprAST *>(out.get())) {
      if (varExpr->getName() == varName)
        return true;
    }
  }
  for (const auto &ext : ExternalArgs) {
    if (auto *varExpr = dynamic_cast<VariableExprAST *>(ext.get())) {
      if (varExpr->getName() == varName)
        return true;
    }
  }
  return false;
}

bool LLVMCommandAST::writesTo(const std::string &varName) const {
  for (const auto &out : OutArgs) {
    if (auto *varExpr = dynamic_cast<VariableExprAST *>(out.get())) {
      if (varExpr->getName() == varName)
        return true;
    }
  }
  return false;
}
