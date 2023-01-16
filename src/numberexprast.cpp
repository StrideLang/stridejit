#include "numberexprast.hpp"

#include "strideenvironment.hpp"

// ----------------------

llvm::Value *RealExprAST::codegen(StrideCompiler &state) {
  return llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(Val));
}

llvm::Value *IntExprAST::codegen(StrideCompiler &state) {
  return llvm::ConstantInt::get(*state.TheContext,
                                llvm::APInt(NumBits, double(Val)));
}

std::vector<size_t> VariableExprAST::getIndeces() const { return Indeces; }

llvm::Value *VariableExprAST::codegen(StrideCompiler &state) {
  // Look this variable up in the function.
  llvm::Value *V = state.NamedValues[Name];
  if (!V)
    return state.LogErrorV(("Unknown variable name: " + Name).c_str());
  //  return
  //  state.Builder->CreateLoad(llvm::Type::getDoubleTy(*state.TheContext),
  //                                   V, Name.c_str());
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
          idxList.push_back(llvm::ConstantInt::get(
              *state.TheContext, llvm::APInt(64, indeces[0])));
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

    if (Val->getType()->isPointerTy()) {
      auto loadInst = state.Builder->CreateLoad(
          Val->getType()->getNonOpaquePointerElementType(), Val);
      state.Builder->CreateStore(loadInst, Variable);
    } else {
      state.Builder->CreateStore(Val, Variable);
    }
    // This might be unnecessary if variable is not read further, but should
    // be removed by optimization pass
    //    state.NamedValues[LHSE->getName()] =
    //        state.Builder->CreateLoad(Val->getType(), Variable);

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
    L = state.Builder->CreateLoad(
        L->getType()->getNonOpaquePointerElementType(), L);
  }
  if (R->getType()->isPointerTy()) {
    R = state.Builder->CreateLoad(
        R->getType()->getNonOpaquePointerElementType(), R);
  }
  switch (Op) {
  case '+': {
    if (L->getType()->isDoubleTy() && R->getType()->isDoubleTy()) {
      return state.Builder->CreateFAdd(L, R, "addtmp");
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
      return state.Builder->CreateAdd(L, R, "addtmp");
    }
  }
  case '-': {
    if (L->getType()->isDoubleTy() && R->getType()->isDoubleTy()) {
      return state.Builder->CreateFSub(L, R, "subtmp");
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
      return state.Builder->CreateSub(L, R, "subtmp");
    }
  }
  case '*': {
    if (L->getType()->isDoubleTy() && R->getType()->isDoubleTy()) {
      return state.Builder->CreateFMul(L, R, "multmp");
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
      return state.Builder->CreateMul(L, R, "multmp");
    }
  }
  case '<': {
    if (L->getType()->isDoubleTy() && R->getType()->isDoubleTy()) {
      L = state.Builder->CreateFCmpULT(L, R, "cmptmp");
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
      L = state.Builder->CreateICmpULT(L, R, "cmptmp");
    }
    // Convert bool 0/1 to double 0.0 or 1.0
    return state.Builder->CreateUIToFP(
        L, llvm::Type::getDoubleTy(*state.TheContext), "booltmp");
  }
  default:
    return state.LogErrorV("invalid binary operator");
  }
}

void ListExprAST::addElement(std::unique_ptr<ExprAST> elem) {
  members.emplace_back(std::move(elem));
}

llvm::Value *ListExprAST::codegen(StrideCompiler &state) {
  return llvm::ConstantTokenNone::get(*state.TheContext);
}

llvm::Value *BoolExprAST::codegen(StrideCompiler &state) {
  return llvm::ConstantInt::get(*state.TheContext, llvm::APInt(1, Val ? 1 : 0));
}
