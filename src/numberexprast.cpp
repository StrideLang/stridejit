
#include "llvm/IR/Module.h"

#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"

using namespace strd;
// ----------------------

std::pair<llvm::Value *, std::optional<llvm::Type *>>
RealExprAST::codegen(StrideCompiler &state) {

  if (typecast.size() > 0) {
    if (typecast == "_IntType") {
      return {llvm::ConstantInt::get(*state.TheContext, llvm::APInt(32, Val)),
              llvm::Type::getInt32Ty(*state.TheContext)};
    }
  }
  return {llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(Val)),
          llvm::Type::getDoubleTy(*state.TheContext)};
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
IntExprAST::codegen(StrideCompiler &state) {
  if (typecast.size() > 0) {
    if (typecast == "_RealType") {
      return {
          llvm::ConstantFP::get(*state.TheContext, llvm::APFloat(double(Val))),
          llvm::Type::getDoubleTy(*state.TheContext)};
    }
  }
  return {llvm::ConstantInt::get(*state.TheContext, llvm::APInt(NumBits, Val)),
          llvm::Type::getInt32Ty(*state.TheContext)};
}

std::vector<std::variant<size_t, std::string>>
VariableExprAST::getIndeces() const {
  return Indeces;
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
VariableExprAST::codegen(StrideCompiler &state) {
  // Look this variable up in the function.
  llvm::Value *V{nullptr};
  std::optional<llvm::Type *> T;
  if (state.NamedValues.find(Name) != state.NamedValues.end()) {
    V = state.NamedValues[Name].first;
    assert(state.NamedValues[Name].second.has_value());
    T = state.NamedValues[Name].second;
  } else if (state.globalExists(Name)) {
    auto global = state.getGlobal(Name);
    V = global.first;
    T = global.second;
  }
  if (!V)
    return {state.LogErrorV(("Unknown variable name: " + Name).c_str()),
            std::nullopt};
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
  return {V, T};
}

BinaryExprAST::BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                             std::unique_ptr<ExprAST> RHS)
    : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
BinaryExprAST::codegen(StrideCompiler &state) {

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
    if (!Val)
      return {nullptr, std::nullopt};
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
    Val->dump();
    Variable->dump();
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
          auto *GEP = state.Builder->CreateGEP(Type, Val, idxList);
          Val = state.Builder->CreateLoad(Type, GEP, varExpr->getName());
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
      if (!TypePtr.has_value()) {
        if (VariableType.has_value()) {
          Type = VariableType.value();
        } else {
          return {state.LogErrorV(("Got pointer without type: " +
                                   std::string(Val->getName()))
                                      .c_str()),
                  std::nullopt};
        }
      } else {
        Type = TypePtr.value();
      }
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
            idxList.push_back(state.NamedValues[*strIdx].first);
          }

          Variable = state.Builder->CreateGEP(Type, Variable, idxList);
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
    Val->dump();
    return {Val, std::nullopt};
  } else {

    auto [L, LType] = LHS->codegen(state);
    auto [R, RType] = RHS->codegen(state);
    //  L->dump();
    //  R->dump();
    if (!L || !R)
      return {nullptr, std::nullopt};
    L->dump();
    R->dump();
    if (L->getType()->isPointerTy()) {
      if (!LType.has_value()) {
        return {nullptr, std::nullopt};
      }
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
            idxList.push_back(state.NamedValues[*strIdx].first);
          }
          auto GEP = state.Builder->CreateGEP(LType.value(), L, idxList);
          L = state.Builder->CreateLoad(LType.value(), GEP, varExpr->getName());
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
          auto GEP = state.Builder->CreateGEP(RType.value(), R, idxList);
          R = state.Builder->CreateLoad(RType.value(), GEP, varExpr->getName());
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
    llvm::Value *Val{nullptr};
    std::optional<llvm::Type *> Type;
    switch (Op) {
    case '+': {
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFAdd(L, R, "addtmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateAdd(L, R, "addtmp");
        Type = llvm::Type::getInt32Ty(*state.TheContext);
      }
      break;
    }
    case '-': {
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFSub(L, R, "subtmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateSub(L, R, "subtmp");
        Type = llvm::Type::getInt32Ty(*state.TheContext);
      }
      break;
    }
    case '*': {
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFMul(L, R, "multmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateMul(L, R, "multmp");
        Type = llvm::Type::getInt32Ty(*state.TheContext);
      }
      break;
    }
    case '<': {
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
      std::nullopt};
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
PortPropertyAST::codegen(StrideCompiler &state) {

  std::string portToken = Name + "_" + Property;
  if (state.NamedValues.find(portToken) == state.NamedValues.end()) {
    return {
        state.LogErrorV(("Unknown port property name: " + portToken).c_str()),
        std::nullopt};
  }
  auto [V, VType] = state.NamedValues[portToken];
  if (typecast.size() > 0) {
    if (V->getType()->isIntegerTy() && typecast == "_RealType") {
      V = state.Builder->CreateSIToFP(
          V, llvm::Type::getDoubleTy(*state.TheContext));
    } else if (V->getType()->isDoubleTy() && typecast == "_IntType") {
      V = state.Builder->CreateFPToSI(
          V, llvm::Type::getInt32Ty(*state.TheContext));
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
  llvm::BasicBlock *ThenBB =
      llvm::BasicBlock::Create(*state.TheContext, "then", TheFunction);
  //  llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(*state.TheContext,
  //  "else");
  llvm::BasicBlock *MergeBB =
      llvm::BasicBlock::Create(*state.TheContext, "ifcont", TheFunction);

  state.Builder->CreateCondBr(V, ThenBB, MergeBB);
  state.Builder->SetInsertPoint(ThenBB);

  //  llvm::Value *ThenV = Then->codegen();
  //  if (!ThenV)
  //    return nullptr;
  for (auto &expr : Expressions) {
    expr->codegen(state);
  }

  state.Builder->CreateBr(MergeBB);
  //  ThenBB = state.Builder->GetInsertBlock();
  state.Builder->SetInsertPoint(MergeBB);

  return {nullptr, std::nullopt};
}
