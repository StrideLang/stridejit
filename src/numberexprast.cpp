#include "numberexprast.hpp"

#include "stridecompiler.hpp"

// ----------------------

llvm::Value *RealExprAST::codegen(StrideCompiler &state) {

  if (typecast.size() > 0) {
    if (typecast == "_IntType") {
      return llvm::ConstantInt::get(*state.TheContext, llvm::APInt(32, Val));
    }
  }
  return llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(Val));
}

llvm::Value *IntExprAST::codegen(StrideCompiler &state) {
  if (typecast.size() > 0) {
    if (typecast == "_RealType") {
      return llvm::ConstantFP::get(*state.TheContext,
                                   llvm::APFloat(double(Val)));
    }
  }
  return llvm::ConstantInt::get(*state.TheContext, llvm::APInt(NumBits, Val));
}

std::vector<std::variant<size_t, std::string>>
VariableExprAST::getIndeces() const {
  return Indeces;
}

llvm::Value *VariableExprAST::codegen(StrideCompiler &state) {
  // Look this variable up in the function.
  llvm::Value *V = state.NamedValues[Name];
  if (!V)
    return state.LogErrorV(("Unknown variable name: " + Name).c_str());
  //  return
  //  state.Builder->CreateLoad(llvm::Type::getDoubleTy(*state.TheContext),
  //                                   V, Name.c_str());
  if (typecast.size() > 0) {
    if (V->getType()->isIntegerTy() && typecast == "_RealType") {
      V = state.Builder->CreateSIToFP(
          V, llvm::Type::getDoubleTy(*state.TheContext));
    } else if (V->getType()->isDoubleTy() && typecast == "_IntType") {
      V = state.Builder->CreateFPToSI(
          V, llvm::Type::getInt32Ty(*state.TheContext));
    }
  }
  return V;
}

BinaryExprAST::BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                             std::unique_ptr<ExprAST> RHS)
    : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

llvm::Value *BinaryExprAST::codegen(StrideCompiler &state) {

  // Special case '=' because we don't want to emit the LHS as an expression.
  if (Op == '=') {
    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    VariableExprAST *LHSE = dynamic_cast<VariableExprAST *>(LHS.get());
    if (!LHSE)
      return state.LogErrorV("destination of '=' must be a variable");
    // Codegen the RHS.
    llvm::Value *Val = RHS->codegen(state);
    if (!Val)
      return nullptr;
    // Look up the name.
    llvm::Value *Variable = state.NamedValues[LHSE->getName()];
    if (!Variable) {
      auto global = state.TheModule->getNamedGlobal(LHSE->getName());
      Variable = global;
      if (!Variable) {
        return state.LogErrorV(
            ("Unknown variable name: " + LHSE->getName()).c_str());
      }
    }
    Val->dump();
    Variable->dump();
    if (Val->getType()->isPointerTy()) {
      if (auto *varExpr = dynamic_cast<VariableExprAST *>(RHS.get())) {
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
              Val->getType()->getNonOpaquePointerElementType(), Val, idxList);
          Val = state.Builder->CreateLoad(
              Val->getType()->getNonOpaquePointerElementType(), GEP,
              varExpr->getName());
        } else {
          Val = state.Builder->CreateLoad(
              Val->getType()->getNonOpaquePointerElementType(), Val);
        }
      } else {
        Val = state.Builder->CreateLoad(
            Val->getType()->getNonOpaquePointerElementType(), Val);
      }
    }
    // Assigning to bundle
    if (Variable->getType()->isPointerTy()) {
      if (auto *varExpr = dynamic_cast<VariableExprAST *>(LHS.get())) {
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
          Variable = state.Builder->CreateGEP(
              Variable->getType()->getNonOpaquePointerElementType(), Variable,
              idxList);
          //          Variable = state.Builder->CreateLoad(
          //              Variable->getType()->getNonOpaquePointerElementType(),
          //              GEP, varExpr->getName());
        } /*else {
          Variable = state.Builder->CreateLoad(
              Variable->getType()->getNonOpaquePointerElementType(), Val);
        }*/
      }
    }

    if (Val->getType()->isPointerTy()) {
      auto loadInst = state.Builder->CreateLoad(
          Val->getType()->getNonOpaquePointerElementType(), Val);

      if (loadInst->getType()->isDoubleTy() &&
          Variable->getType()
              ->getNonOpaquePointerElementType()
              ->isIntegerTy()) {
      } else
        state.Builder->CreateStore(loadInst, Variable);
    } else {
      if (state.NamedValues[LHSE->getName()]->getType()->isPointerTy()) {
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
    if (!state.NamedValues[LHSE->getName()]->getType()->isPointerTy()) {
      state.NamedValues[LHSE->getName()] = Val;
    }
    return Val;
  }

  llvm::Value *L = LHS->codegen(state);
  llvm::Value *R = RHS->codegen(state);
  //  L->dump();
  //  R->dump();
  if (!L || !R)
    return nullptr;
  L->dump();
  R->dump();
  if (L->getType()->isPointerTy()) {
    if (auto *varExpr = dynamic_cast<VariableExprAST *>(LHS.get())) {
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
        auto GEP = state.Builder->CreateGEP(
            L->getType()->getNonOpaquePointerElementType(), L, idxList);
        L = state.Builder->CreateLoad(
            L->getType()->getNonOpaquePointerElementType(), GEP,
            varExpr->getName());
      } else {
        L = state.Builder->CreateLoad(
            L->getType()->getNonOpaquePointerElementType(), L);
      }
    } else {
      L = state.Builder->CreateLoad(
          L->getType()->getNonOpaquePointerElementType(), L);
    }
  }

  if (R->getType()->isPointerTy()) {
    if (auto *varExpr = dynamic_cast<VariableExprAST *>(RHS.get())) {
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
        auto GEP = state.Builder->CreateGEP(
            R->getType()->getNonOpaquePointerElementType(), R, idxList);
        R = state.Builder->CreateLoad(
            R->getType()->getNonOpaquePointerElementType(), GEP,
            varExpr->getName());
        //          Variable = state.Builder->CreateLoad(
        //              Variable->getType()->getNonOpaquePointerElementType(),
        //              GEP, varExpr->getName());
      } else {
        R = state.Builder->CreateLoad(
            R->getType()->getNonOpaquePointerElementType(), R);
      }
    } else {
      R = state.Builder->CreateLoad(
          R->getType()->getNonOpaquePointerElementType(), R);
    }
  }
  llvm::Value *Val{nullptr};
  switch (Op) {
  case '+': {
    if (L->getType()->isDoubleTy() && R->getType()->isDoubleTy()) {
      Val = state.Builder->CreateFAdd(L, R, "addtmp");
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
      Val = state.Builder->CreateAdd(L, R, "addtmp");
    }
    break;
  }
  case '-': {
    if (L->getType()->isDoubleTy() && R->getType()->isDoubleTy()) {
      Val = state.Builder->CreateFSub(L, R, "subtmp");
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
      Val = state.Builder->CreateSub(L, R, "subtmp");
    }
    break;
  }
  case '*': {
    if (L->getType()->isDoubleTy() && R->getType()->isDoubleTy()) {
      Val = state.Builder->CreateFMul(L, R, "multmp");
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
      Val = state.Builder->CreateMul(L, R, "multmp");
    }
    break;
  }
  case '<': {
    if (L->getType()->isDoubleTy() && R->getType()->isDoubleTy()) {
      L = state.Builder->CreateFCmpULT(L, R, "cmptmp");
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
      L = state.Builder->CreateICmpULT(L, R, "cmptmp");
    }
    // Convert bool 0/1 to double 0.0 or 1.0
    Val = state.Builder->CreateUIToFP(
        L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");

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
      } else if (Val->getType()->isDoubleTy() && typecast == "_IntType") {
        Val = state.Builder->CreateFPToSI(
            Val, llvm::Type::getInt32Ty(*state.TheContext));
      }
    }
    return Val;
  }

  return state.LogErrorV("invalid binary operator");
}

llvm::Value *BoolExprAST::codegen(StrideCompiler &state) {
  return llvm::ConstantInt::get(*state.TheContext, llvm::APInt(1, Val ? 1 : 0));
}

llvm::Value *PortPropertyAST::codegen(StrideCompiler &state) {

  std::string portToken = Name + "_" + Property;
  if (state.NamedValues.find(portToken) == state.NamedValues.end()) {
    return state.LogErrorV(
        ("Unknown port property name: " + portToken).c_str());
  }
  llvm::Value *V = state.NamedValues[portToken];
  if (typecast.size() > 0) {
    if (V->getType()->isIntegerTy() && typecast == "_RealType") {
      V = state.Builder->CreateSIToFP(
          V, llvm::Type::getDoubleTy(*state.TheContext));
    } else if (V->getType()->isDoubleTy() && typecast == "_IntType") {
      V = state.Builder->CreateFPToSI(
          V, llvm::Type::getInt32Ty(*state.TheContext));
    }
  }
  return V;
}
