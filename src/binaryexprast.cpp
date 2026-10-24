#include <iostream>
#include "stride/utils/logger.h"

#include "stride/stridejit/binaryexprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"
#include "llvm/Support/raw_ostream.h"

using namespace strd;

BinaryExprAST::BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                             std::unique_ptr<ExprAST> RHS)
    : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

static llvm::Function *getOrCreateFunction(StrideCompiler &state) {
  llvm::BasicBlock *curBB = state.Builder->GetInsertBlock();
  if (curBB && curBB->getParent()) {
    return curBB->getParent();
  }
  llvm::FunctionType *fnTy = llvm::FunctionType::get(
      llvm::Type::getVoidTy(*state.TheContext), false);
  llvm::Function *fn = llvm::Function::Create(
      fnTy, llvm::Function::ExternalLinkage, "__anon_expr",
      state.TheModule.get());
  llvm::BasicBlock *bb =
      llvm::BasicBlock::Create(*state.TheContext, "entry", fn);
  state.Builder->SetInsertPoint(bb);
  return fn;
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
BinaryExprAST::codegen(StrideCompiler &state) {
  LOG_INFO() << " == BinaryExprAST codegen " << std::string{Op} << std::endl;

  if (state.debugEmitter && state.debugEmitter->isInitialized() && m_line > 0) {
    state.debugEmitter->emitLocation(*state.Builder, m_line, m_column);
  }

  // Special case '=' because we don't want to emit the LHS as an expression.
  if (Op == '=') {
    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    VariableExprAST *LHSE = dynamic_cast<VariableExprAST *>(LHS.get());
    if (!LHSE) {
      return {state.LogErrorV("destination of '=' must be a variable"),
              std::nullopt};
    }
    // Codegen the RHS.
    auto [Val, TypePtr] = RHS->codegen(state);
    if (!Val || Val->getType()->isVoidTy() || !Val->getType()->isSized())
      return {Val, std::nullopt};
    // Look up the name.
    llvm::Value *Variable = nullptr;
    std::optional<llvm::Type *> VariableType;
    if (state.NamedValues.find(LHSE->getName()) != state.NamedValues.end()) {

      std::tie(Variable, VariableType) = state.NamedValues[LHSE->getName()];

    } else {
      if (state.NamedValues.find(LHSE->getName()) != state.NamedValues.end()) {
        Variable = state.NamedValues[LHSE->getName()].first;
        assert(state.NamedValues[LHSE->getName()].second.has_value());
        VariableType = state.NamedValues[LHSE->getName()].second;
      } else if (state.globalExists(LHSE->getName())) {
        auto global = state.getGlobal(LHSE->getName());
        Variable = global.first;
        VariableType = global.second;
        assert(VariableType.has_value());
      }
      if (!Variable) {
        return {state.LogErrorV(
                    ("Unknown variable name: " + LHSE->getName()).c_str()),
                std::nullopt};
      }
      // Variable = state.Builder->CreateLoad(
      //     VariableType.value(), Variable
      //     //"global_val"          // Optional: debugging name for the
      //     register
      // );
    }
    if (Logger::isDebugEnabled()) {
      std::string s;
      llvm::raw_string_ostream rso(s);
      Variable->print(rso);
      rso << " = ";
      Val->print(rso);
      LOG_DEBUG() << rso.str() << "\n";
    }
    if (Val->getType()->isPointerTy()) {
      llvm::Type *Type;
      if (!TypePtr.has_value()) {
        return {state.LogErrorV(
                    ("Got pointer without type: " + std::string(Val->getName()))
                        .c_str()),
                std::nullopt};
      }
      Type = TypePtr.value();
      if (auto *varExpr = dynamic_cast<VariableExprAST *>(RHS.get())) {
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
                indexVal = state.Builder->CreateLoad(
                    it->second.second.value(), indexVal, *strIdx);
              }
              idxList.push_back(indexVal);
            } else {
              assert(0 == 1);
            }
          }
          llvm::Type *gepElemType = Type;
          if (gepElemType->isArrayTy()) {
            gepElemType = static_cast<llvm::ArrayType *>(gepElemType)->getElementType();
          }
          auto *GEP = state.Builder->CreateGEP(gepElemType, Val, idxList);
          Val = state.Builder->CreateLoad(gepElemType, GEP, varExpr->getName());
        } else {
          Val = state.Builder->CreateLoad(Type, Val);
        }
      } else {
        Val = state.Builder->CreateLoad(Type, Val);
      }
    }
    // Assigning
    if (Variable->getType()->isPointerTy()) {
      llvm::Type *Type;
      if (VariableType.has_value()) {
        Type = VariableType.value();
      } else {
        return {state.LogErrorV(
                    ("Got pointer without type: " + std::string(Val->getName()))
                        .c_str()),
                std::nullopt};
      }
      if (auto *varExpr = dynamic_cast<VariableExprAST *>(LHS.get())) {
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
                indexVal = state.Builder->CreateLoad(
                    it->second.second.value(), indexVal, *strIdx);
              }
              idxList.push_back(indexVal);
            } else {
              assert(0 == 1);
            }
          }
          if (Type->isArrayTy()) {
            Type = static_cast<llvm::ArrayType *>(Type)->getElementType();
          }
          Variable = state.Builder->CreateGEP(Type, Variable, idxList);
          //          Variable = state.Builder->CreateLoad(
          //              Variable->getType()->getNonOpaquePointerElementType(),
          //              GEP, varExpr->getName());
          if (Logger::isDebugEnabled()) {
            std::string s;
            llvm::raw_string_ostream rso(s);
            Variable->print(rso);
            LOG_DEBUG() << rso.str() << "\n";
          }
        } /*else {
  Variable = state.Builder->CreateLoad(
      Variable->getType()->getNonOpaquePointerElementType(), Val);
}*/
      }
    }

    if (Val->getType()->isPointerTy()) {
      if (!TypePtr.has_value()) {
        return {state.LogErrorV(
                    ("Got pointer without type: " + std::string(Val->getName()))
                        .c_str()),
                std::nullopt};
      }
      auto Type = TypePtr.value();
      auto loadInst = state.Builder->CreateLoad(Type, Val);

      if (loadInst->getType()->isDoubleTy() && Type->isIntegerTy()) {
      } else
        state.Builder->CreateStore(loadInst, Variable);
    } else {
      if (VariableType.has_value()) {
        if (VariableType.value()->isIntegerTy(1) && Val->getType()->isDoubleTy()) {
          Val = state.Builder->CreateFPToUI(
              Val, llvm::Type::getInt1Ty(*state.TheContext), "fptoui");
        } else if (VariableType.value()->isIntegerTy(32) && Val->getType()->isDoubleTy()) {
          Val = state.Builder->CreateFPToSI(
              Val, llvm::Type::getInt32Ty(*state.TheContext), "fptosi");
        }
      }
      if (state.NamedValues.find(LHSE->getName()) != state.NamedValues.end()) {
        if (state.NamedValues[LHSE->getName()]
                .first->getType()
                ->isPointerTy()) {
          state.Builder->CreateStore(Val, Variable);
        }
      } else {
        // GLobal
        state.Builder->CreateStore(Val, Variable);
      }
    }

    if (typecast.size() > 0) {
      if (Val->getType()->isIntegerTy() && typecast == "_RealType") {
        Val = state.Builder->CreateSIToFP(
            Val, llvm::Type::getDoubleTy(*state.TheContext));
      } else if (Val->getType()->isDoubleTy() && typecast == "_IntType") {
        Val = state.Builder->CreateFPToSI(
            Val, llvm::Type::getInt32Ty(*state.TheContext));
      }
    }
    if (state.NamedValues.find(LHSE->getName()) != state.NamedValues.end()) {
      if (!state.NamedValues[LHSE->getName()].first->getType()->isPointerTy()) {
        state.NamedValues[LHSE->getName()] = {Val, Val->getType()};
      }
    }
    if (Logger::isDebugEnabled()) {
      std::string s;
      llvm::raw_string_ostream rso(s);
      Val->print(rso);
      LOG_DEBUG() << rso.str() << "\n";
    }
    return {Val, std::nullopt};
  } else {

    auto [L, LType] = LHS->codegen(state);
    auto [R, RType] = RHS->codegen(state);
    if (!L || !R)
      return {nullptr, std::nullopt};
    if (Logger::isDebugEnabled()) {
      std::string s;
      llvm::raw_string_ostream rso(s);
      L->print(rso);
      rso << " " << Op << " ";
      R->print(rso);
      LOG_DEBUG() << rso.str() << "\n";
    }
    if (L->getType()->isPointerTy()) {
      if (!LType.has_value()) {
        return {nullptr, std::nullopt};
      }
      if (auto *varExpr = dynamic_cast<VariableExprAST *>(LHS.get())) {
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
                indexVal = state.Builder->CreateLoad(
                    it->second.second.value(), indexVal, *strIdx);
              }
              idxList.push_back(indexVal);
            } else {
              assert(0 == 1); // we shouldn't get here
            }
          }
          llvm::Type *gepElemType = LType.value();
          if (gepElemType->isArrayTy()) {
            gepElemType = static_cast<llvm::ArrayType *>(gepElemType)->getElementType();
          }
          // TODO explore using CreateInBoundsGEP when possible
          auto GEP = state.Builder->CreateGEP(gepElemType, L, idxList);
          L = state.Builder->CreateLoad(gepElemType, GEP, varExpr->getName());
          LType = gepElemType;
        } else {
          L = state.Builder->CreateLoad(LType.value(), L);
        }
      } else {
        L = state.Builder->CreateLoad(LType.value(), L);
      }
    }

    if (R->getType()->isPointerTy()) {
      if (!RType.has_value()) {
        return {nullptr, std::nullopt};
      }
      if (auto *varExpr = dynamic_cast<VariableExprAST *>(RHS.get())) {
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
                indexVal = state.Builder->CreateLoad(
                    it->second.second.value(), indexVal, *strIdx);
              }
              idxList.push_back(indexVal);
            } else {
              assert(0 == 1);
            }
          }
          llvm::Type *gepElemType = RType.value();
          if (gepElemType->isArrayTy()) {
            gepElemType = static_cast<llvm::ArrayType *>(gepElemType)->getElementType();
          }
          auto GEP = state.Builder->CreateGEP(gepElemType, R, idxList);
          R = state.Builder->CreateLoad(gepElemType, GEP, varExpr->getName());
          RType = gepElemType;
          //          Variable = state.Builder->CreateLoad(
          //              Variable->getType()->getNonOpaquePointerElementType(),
          //              GEP, varExpr->getName());
        } else {
          R = state.Builder->CreateLoad(RType.value(), R);
        }
      } else {
        R = state.Builder->CreateLoad(RType.value(), R);
      }
    }
    if (!LType.has_value()) {
      LType = L->getType();
    }
    if (!RType.has_value()) {
      RType = R->getType();
    }

    if ((*LType)->isDoubleTy() && (*RType)->isIntegerTy()) {
      R = state.Builder->CreateSIToFP(
          R, llvm::Type::getDoubleTy(*state.TheContext), "sitofp");
      RType = llvm::Type::getDoubleTy(*state.TheContext);
    } else if ((*LType)->isIntegerTy() && (*RType)->isDoubleTy()) {
      L = state.Builder->CreateSIToFP(
          L, llvm::Type::getDoubleTy(*state.TheContext), "sitofp");
      LType = llvm::Type::getDoubleTy(*state.TheContext);
    } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
      unsigned lBits = (*LType)->getIntegerBitWidth();
      unsigned rBits = (*RType)->getIntegerBitWidth();
      if (lBits < rBits) {
        L = state.Builder->CreateIntCast(L, *RType, true, "intcast");
        LType = RType;
      } else if (rBits < lBits) {
        R = state.Builder->CreateIntCast(R, *LType, true, "intcast");
        RType = LType;
      }
    }

    llvm::Value *Val{nullptr};
    std::optional<llvm::Type *> Type;
    switch (Op) {
    case '+': {
      if (*LType == state.typesMap["_StringType"] &&
          *RType == state.typesMap["_StringType"]) {
        llvm::Function *TheFunction = getOrCreateFunction(state);
        llvm::Type *stringTy = state.typesMap["_StringType"];
        llvm::ArrayType *arrayTy = llvm::ArrayType::get(stringTy, 2);
        llvm::AllocaInst *arrayAlloc =
            state.CreateEntryBlockAlloca(TheFunction, "concat_arr", arrayTy);
        llvm::AllocaInst *allocOut =
            state.CreateEntryBlockAlloca(TheFunction, "concat_out", stringTy);

        llvm::Value *zero = llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(*state.TheContext), 0);
        llvm::Value *one = llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(*state.TheContext), 1);
        llvm::Value *elem0 = state.Builder->CreateInBoundsGEP(
            arrayTy, arrayAlloc, {zero, zero}, "elem0");
        llvm::Value *elem1 = state.Builder->CreateInBoundsGEP(
            arrayTy, arrayAlloc, {zero, one}, "elem1");
        state.Builder->CreateStore(L, elem0);
        state.Builder->CreateStore(R, elem1);

        llvm::Type *ptrTy = llvm::PointerType::get(*state.TheContext, 0);
        llvm::Type *i32Ty = llvm::Type::getInt32Ty(*state.TheContext);
        llvm::Function *concatFunc =
            state.getFunctionInModule("__stride_jit_string_concat_n");
        if (!concatFunc) {
          llvm::FunctionType *fnTy = llvm::FunctionType::get(
              llvm::Type::getVoidTy(*state.TheContext), {ptrTy, i32Ty, ptrTy},
              false);
          concatFunc = llvm::Function::Create(
              fnTy, llvm::Function::ExternalLinkage,
              "__stride_jit_string_concat_n", state.TheModule.get());
        }
        state.Builder->CreateCall(
            concatFunc, {arrayAlloc, state.Builder->getInt32(2), allocOut});
        Val = state.Builder->CreateLoad(stringTy, allocOut, "concat_res");
        Type = stringTy;
        break;
      }
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFAdd(L, R, "addtmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateAdd(L, R, "addtmp");
        Type = *LType;
      }
      break;
    }
    case '-': {
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFSub(L, R, "subtmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateSub(L, R, "subtmp");
        Type = *LType;
      }
      break;
    }
    case '*': {
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFMul(L, R, "multmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateMul(L, R, "multmp");
        Type = *LType;
      }
      break;
    }
    case '/': {
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFDiv(L, R, "divtmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateSDiv(L, R, "divtmp");
        Type = *LType;
      }
      break;
    }
    case ':': { // equal
      if (*LType == state.typesMap["_StringType"] &&
          *RType == state.typesMap["_StringType"]) {
        llvm::Function *TheFunction = getOrCreateFunction(state);
        llvm::Type *stringTy = state.typesMap["_StringType"];
        llvm::AllocaInst *allocA =
            state.CreateEntryBlockAlloca(TheFunction, "streq_a", stringTy);
        llvm::AllocaInst *allocB =
            state.CreateEntryBlockAlloca(TheFunction, "streq_b", stringTy);
        state.Builder->CreateStore(L, allocA);
        state.Builder->CreateStore(R, allocB);

        llvm::Type *ptrTy = llvm::PointerType::get(*state.TheContext, 0);
        llvm::Function *eqFunc =
            state.getFunctionInModule("__stride_jit_string_equal");
        if (!eqFunc) {
          llvm::FunctionType *fnTy = llvm::FunctionType::get(
              llvm::Type::getInt1Ty(*state.TheContext),
              {ptrTy, ptrTy}, false);
          eqFunc = llvm::Function::Create(
              fnTy, llvm::Function::ExternalLinkage,
              "__stride_jit_string_equal", state.TheModule.get());
        }
        L = state.Builder->CreateCall(eqFunc, {allocA, allocB}, "streq");
        Val = state.Builder->CreateUIToFP(
            L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");
        Type = llvm::Type::getInt1Ty(*state.TheContext);
        break;
      }
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        L = state.Builder->CreateFCmpUEQ(L, R, "cmptmp");
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        L = state.Builder->CreateICmpEQ(L, R, "cmptmp");
      }
      // Convert bool 0/1 to double 0.0 or 1.0
      Val = state.Builder->CreateUIToFP(
          L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");
      Type = llvm::Type::getInt1Ty(*state.TheContext);

      break;
    }
    case '!': { // not equal
      if (*LType == state.typesMap["_StringType"] &&
          *RType == state.typesMap["_StringType"]) {
        llvm::Function *TheFunction = getOrCreateFunction(state);
        llvm::Type *stringTy = state.typesMap["_StringType"];
        llvm::AllocaInst *allocA =
            state.CreateEntryBlockAlloca(TheFunction, "strne_a", stringTy);
        llvm::AllocaInst *allocB =
            state.CreateEntryBlockAlloca(TheFunction, "strne_b", stringTy);
        state.Builder->CreateStore(L, allocA);
        state.Builder->CreateStore(R, allocB);

        llvm::Type *ptrTy = llvm::PointerType::get(*state.TheContext, 0);
        llvm::Function *eqFunc =
            state.getFunctionInModule("__stride_jit_string_equal");
        if (!eqFunc) {
          llvm::FunctionType *fnTy = llvm::FunctionType::get(
              llvm::Type::getInt1Ty(*state.TheContext),
              {ptrTy, ptrTy}, false);
          eqFunc = llvm::Function::Create(
              fnTy, llvm::Function::ExternalLinkage,
              "__stride_jit_string_equal", state.TheModule.get());
        }
        llvm::Value *eqVal =
            state.Builder->CreateCall(eqFunc, {allocA, allocB}, "streq");
        L = state.Builder->CreateNot(eqVal, "strne");
        Val = state.Builder->CreateUIToFP(
            L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");
        Type = llvm::Type::getInt1Ty(*state.TheContext);
        break;
      }
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        L = state.Builder->CreateFCmpUNE(L, R, "cmptmp");
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        L = state.Builder->CreateICmpNE(L, R, "cmptmp");
      }
      // Convert bool 0/1 to double 0.0 or 1.0
      Val = state.Builder->CreateUIToFP(
          L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");
      Type = llvm::Type::getInt1Ty(*state.TheContext);

      break;
    }
    case '<': {
      if (*LType == state.typesMap["_StringType"] &&
          *RType == state.typesMap["_StringType"]) {
        llvm::Function *TheFunction = getOrCreateFunction(state);
        llvm::Type *stringTy = state.typesMap["_StringType"];
        llvm::AllocaInst *allocA =
            state.CreateEntryBlockAlloca(TheFunction, "strlt_a", stringTy);
        llvm::AllocaInst *allocB =
            state.CreateEntryBlockAlloca(TheFunction, "strlt_b", stringTy);
        state.Builder->CreateStore(L, allocA);
        state.Builder->CreateStore(R, allocB);

        llvm::Type *ptrTy = llvm::PointerType::get(*state.TheContext, 0);
        llvm::Function *cmpFunc =
            state.getFunctionInModule("__stride_jit_string_compare");
        if (!cmpFunc) {
          llvm::FunctionType *fnTy = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(*state.TheContext),
              {ptrTy, ptrTy}, false);
          cmpFunc = llvm::Function::Create(
              fnTy, llvm::Function::ExternalLinkage,
              "__stride_jit_string_compare", state.TheModule.get());
        }
        llvm::Value *cmpVal =
            state.Builder->CreateCall(cmpFunc, {allocA, allocB}, "strcmp");
        L = state.Builder->CreateICmpSLT(
            cmpVal, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*state.TheContext), 0),
            "cmptmp");
        Val = state.Builder->CreateUIToFP(
            L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");
        Type = llvm::Type::getInt1Ty(*state.TheContext);
        break;
      }
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        L = state.Builder->CreateFCmpULT(L, R, "cmptmp");
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        L = state.Builder->CreateICmpULT(L, R, "cmptmp");
      }
      // Convert bool 0/1 to double 0.0 or 1.0
      Val = state.Builder->CreateUIToFP(
          L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");
      Type = llvm::Type::getInt1Ty(*state.TheContext);

      break;
    }
    case '>': {
      if (*LType == state.typesMap["_StringType"] &&
          *RType == state.typesMap["_StringType"]) {
        llvm::Function *TheFunction = getOrCreateFunction(state);
        llvm::Type *stringTy = state.typesMap["_StringType"];
        llvm::AllocaInst *allocA =
            state.CreateEntryBlockAlloca(TheFunction, "strgt_a", stringTy);
        llvm::AllocaInst *allocB =
            state.CreateEntryBlockAlloca(TheFunction, "strgt_b", stringTy);
        state.Builder->CreateStore(L, allocA);
        state.Builder->CreateStore(R, allocB);

        llvm::Type *ptrTy = llvm::PointerType::get(*state.TheContext, 0);
        llvm::Function *cmpFunc =
            state.getFunctionInModule("__stride_jit_string_compare");
        if (!cmpFunc) {
          llvm::FunctionType *fnTy = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(*state.TheContext),
              {ptrTy, ptrTy}, false);
          cmpFunc = llvm::Function::Create(
              fnTy, llvm::Function::ExternalLinkage,
              "__stride_jit_string_compare", state.TheModule.get());
        }
        llvm::Value *cmpVal =
            state.Builder->CreateCall(cmpFunc, {allocA, allocB}, "strcmp");
        L = state.Builder->CreateICmpSGT(
            cmpVal, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*state.TheContext), 0),
            "cmptmp");
        Val = state.Builder->CreateUIToFP(
            L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");
        Type = llvm::Type::getInt1Ty(*state.TheContext);
        break;
      }
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        L = state.Builder->CreateFCmpUGT(L, R, "cmptmp");
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        L = state.Builder->CreateICmpUGT(L, R, "cmptmp");
      }
      // Convert bool 0/1 to double 0.0 or 1.0
      Val = state.Builder->CreateUIToFP(
          L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");
      Type = llvm::Type::getInt1Ty(*state.TheContext);

      break;
    }
    default:
      break;
    }
    if (Val) {
      if (typecast.size() > 0) {
        if (Val->getType()->isIntegerTy() && typecast == "_RealType") {
          Val = state.Builder->CreateSIToFP(
              Val, llvm::Type::getDoubleTy(*state.TheContext));
          Type = llvm::Type::getDoubleTy(*state.TheContext);
        } else if (Val->getType()->isDoubleTy() && typecast == "_IntType") {
          Val = state.Builder->CreateFPToSI(
              Val, llvm::Type::getInt32Ty(*state.TheContext));
        }
      }
      return {Val, Type};
    }
  }

  return {state.LogErrorV("invalid binary operator"), std::nullopt};
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
BoolExprAST::codegen(StrideCompiler &state) {
  return {
      llvm::ConstantInt::get(*state.TheContext, llvm::APInt(1, Val ? 1 : 0)),
      llvm::Type::getInt1Ty(*state.TheContext)};
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
MemberAccessAST::codegen(StrideCompiler &state) {
  // TODO mangle port property names to avoid clashes
  std::string portToken = Name.empty() ? Property : (Name + "_" + Property);
  if (state.NamedValues.find(portToken) == state.NamedValues.end()) {
    if (!Name.empty() && state.NamedValues.find(Property) != state.NamedValues.end()) {
      portToken = Property;
    } else {
      bool found = false;
      for (const auto &kv : state.NamedValues) {
        if (kv.first.size() > Property.size() + 1 &&
            kv.first.rfind("_" + Property) == kv.first.size() - Property.size() - 1) {
          portToken = kv.first;
          found = true;
          break;
        }
      }
      if (!found) {
        return {
            state.LogErrorV(("Unknown port property name: " + portToken).c_str()),
            std::nullopt};
      }
    }
  }
  auto [V, VType] = state.NamedValues[portToken];
  if (V && V->getType()->isPointerTy()) {
    llvm::Type *elemTy = VType.has_value() ? VType.value()
                                           : llvm::Type::getInt32Ty(*state.TheContext);
    V = state.Builder->CreateLoad(elemTy, V, portToken);
  }
  if (typecast.size() > 0) {
    if (V->getType()->isIntegerTy() && typecast == "_RealType") {
      V = state.Builder->CreateSIToFP(
          V, llvm::Type::getDoubleTy(*state.TheContext));
      VType = llvm::Type::getDoubleTy(*state.TheContext);
    } else if (V->getType()->isDoubleTy() && typecast == "_IntType") {
      V = state.Builder->CreateFPToSI(
          V, llvm::Type::getInt32Ty(*state.TheContext));
      VType = llvm::Type::getInt32Ty(*state.TheContext);
    }
  }
  return {V, VType};
}

ResetExprAST::ResetExprAST(std::string Name, std::unique_ptr<ExprAST> Condition,
                           std::vector<std::unique_ptr<ExprAST>> Expressions)
    : Name(Name), Condition(std::move(Condition)),
      Expressions(std::move(Expressions)) {}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
ResetExprAST::codegen(StrideCompiler &state) {
  llvm::Function *TheFunction = state.Builder->GetInsertBlock()->getParent();
  llvm::Value *V = Condition->codegen(state).first;
  if (!V)
    return {nullptr, std::nullopt};
  if (V->getType()->isPointerTy()) {
    V = state.Builder->CreateLoad(
        llvm::Type::getInt1Ty(*state.TheContext), V, "cond");
  }
  if (V->getType()->isDoubleTy()) {
    V = state.Builder->CreateFCmpONE(
        V, llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(0.0)),
        "ifcond");
  } else if (V->getType()->isIntegerTy() &&
             !V->getType()->isIntegerTy(1)) {
    V = state.Builder->CreateICmpNE(
        V, llvm::ConstantInt::get(V->getType(), 0), "ifcond");
  }
  llvm::BasicBlock *ThenBB =
      llvm::BasicBlock::Create(*state.TheContext, "then", TheFunction);
  llvm::BasicBlock *MergeBB =
      llvm::BasicBlock::Create(*state.TheContext, "ifcont", TheFunction);

  state.Builder->CreateCondBr(V, ThenBB, MergeBB);
  state.Builder->SetInsertPoint(ThenBB);

  for (auto &expr : Expressions) {
    expr->codegen(state);
  }

  state.Builder->CreateBr(MergeBB);
  state.Builder->SetInsertPoint(MergeBB);

  return {nullptr, std::nullopt};
}

bool BinaryExprAST::writesTo(const std::string &varName) const {
  if (Op == '=') {
    if (auto *varExpr = dynamic_cast<VariableExprAST *>(LHS.get())) {
      return varExpr->getName() == varName;
    }
  }
  return false;
}

bool ResetExprAST::writesTo(const std::string &varName) const {
  for (const auto &expr : Expressions) {
    if (expr->writesTo(varName))
      return true;
  }
  return false;
}
