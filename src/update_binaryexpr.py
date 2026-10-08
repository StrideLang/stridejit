import pathlib
import re

content = pathlib.Path('C:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/src/binaryexprast.cpp').read_text(encoding='utf-8')

new_codegen = '''std::pair<llvm::Value *, std::optional<llvm::Type *>>
BinaryExprAST::codegen(StrideCompiler &state) {
  std::cout << " == BinaryExprAST codegen " << std::string{Op} << std::endl;

  // Codegen the LHS and RHS.
  auto [L, LType] = LHS->codegen(state);
  auto [R, RType] = RHS->codegen(state);
  
  if (!L || !R)
    return {nullptr, std::nullopt};

  if (Op == '=') {
    if (!L->getType()->isPointerTy()) {
      return {state.LogErrorV("destination of '=' must be a variable"), std::nullopt};
    }
    
    // Load RHS if it's a pointer
    if (R->getType()->isPointerTy()) {
      if (!RType.has_value()) {
        return {state.LogErrorV("Got pointer without type on RHS of ="), std::nullopt};
      }
      R = state.Builder->CreateLoad(RType.value(), R);
    }
    
    // Typecast logic for assignment
    if (typecast.size() > 0) {
      if (R->getType()->isIntegerTy() && typecast == "_RealType") {
        R = state.Builder->CreateSIToFP(R, llvm::Type::getDoubleTy(*state.TheContext));
      } else if (R->getType()->isDoubleTy() && typecast == "_IntType") {
        R = state.Builder->CreateFPToSI(R, llvm::Type::getInt32Ty(*state.TheContext));
      }
    }
    
    // Check type matching
    if (LType.has_value()) {
        auto type = LType.value();
        if (R->getType()->isDoubleTy() && type->isIntegerTy()) {
            R = state.Builder->CreateFPToSI(R, type);
        }
    }
    
    state.Builder->CreateStore(R, L);
    return {R, std::nullopt};
  } else {
    // For math ops, load both LHS and RHS if they are pointers
    if (L->getType()->isPointerTy()) {
      if (!LType.has_value()) {
        return {nullptr, std::nullopt};
      }
      L = state.Builder->CreateLoad(LType.value(), L);
    }
    if (R->getType()->isPointerTy()) {
      if (!RType.has_value()) {
        return {nullptr, std::nullopt};
      }
      R = state.Builder->CreateLoad(RType.value(), R);
    }
    
    if (!LType.has_value()) LType = L->getType();
    if (!RType.has_value()) RType = R->getType();
    
    llvm::Value *Val{nullptr};
    std::optional<llvm::Type *> Type;
    switch (Op) {
    case '+':
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFAdd(L, R, "addtmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateAdd(L, R, "addtmp");
        Type = llvm::Type::getInt32Ty(*state.TheContext);
      }
      break;
    case '-':
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFSub(L, R, "subtmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateSub(L, R, "subtmp");
        Type = llvm::Type::getInt32Ty(*state.TheContext);
      }
      break;
    case '*':
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFMul(L, R, "multmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateMul(L, R, "multmp");
        Type = llvm::Type::getInt32Ty(*state.TheContext);
      }
      break;
    case '/':
      if ((*LType)->isDoubleTy() && (*RType)->isDoubleTy()) {
        Val = state.Builder->CreateFDiv(L, R, "divtmp");
        Type = llvm::Type::getDoubleTy(*state.TheContext);
      } else if ((*LType)->isIntegerTy() && (*RType)->isIntegerTy()) {
        Val = state.Builder->CreateSDiv(L, R, "divtmp");
        Type = llvm::Type::getInt32Ty(*state.TheContext);
      }
      break;
    default:
      return {state.LogErrorV("invalid binary operator"), std::nullopt};
    }
    
    return {Val, Type};
  }
}'''

start_idx = content.find('std::pair<llvm::Value *, std::optional<llvm::Type *>>\\nBinaryExprAST::codegen(StrideCompiler &state) {')
end_idx = content.find('std::pair<llvm::Value *, std::optional<llvm::Type *>>\\nBoolExprAST::codegen(StrideCompiler &state) {')

if start_idx != -1 and end_idx != -1:
    new_content = content[:start_idx] + new_codegen + "\\n\\n" + content[end_idx:]
    pathlib.Path('C:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/src/binaryexprast.cpp').write_text(new_content, encoding='utf-8')
    print("binaryexprast.cpp updated!")
else:
    print("Could not find start or end index!")
