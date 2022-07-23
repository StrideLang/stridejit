#include <iostream>

#include "blocknode.h"
#include "stridecompiler.hpp"

extern "C" {
__declspec(dllexport) double __stride_Greater_d_dd(double a, double b) {
  return a > b ? 1.0 : 0.0;
}
__declspec(dllexport) bool __stride_Greater_b_dd(double a, double b) {
  // TODO should be removed and llvm functions for this should be used instead
  return a > b;
}
}

StrideCompiler::StrideCompiler() {
  TheContext = std::make_unique<llvm::LLVMContext>();
  TheModule = std::make_unique<llvm::Module>("StrideJit", *this->TheContext);
  Builder = std::make_unique<llvm::IRBuilder<>>(*this->TheContext);

  BinopPrecedence['='] = 2;
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40; // highest.

  TheFPM = std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());

  // Do simple "peephole" optimizations and bit-twiddling optzns.
  TheFPM->add(llvm::createInstructionCombiningPass());
  // Reassociate expressions.
  TheFPM->add(llvm::createReassociatePass());
  // Eliminate Common SubExpressions.
  TheFPM->add(llvm::createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  TheFPM->add(llvm::createCFGSimplificationPass());

  TheFPM->doInitialization();

  // Initialize function map
  {
    llvm::FunctionType *FT =
        llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext),
                                {llvm::Type::getDoubleTy(*TheContext)}, false);
    functionMap["Sine"].push_back({"sin", FT});

    FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext),
                                 {llvm::Type::getDoubleTy(*TheContext)}, false);
    functionMap["Cos"].push_back({"cos", FT});

    FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext),
                                 {llvm::Type::getDoubleTy(*TheContext),
                                  llvm::Type::getDoubleTy(*TheContext)},
                                 false);
    functionMap["Greater"].push_back({"__stride_Greater_d_dd", FT});

    FT = llvm::FunctionType::get(llvm::Type::getInt1Ty(*TheContext),
                                 {llvm::Type::getDoubleTy(*TheContext),
                                  llvm::Type::getDoubleTy(*TheContext)},
                                 false);
    functionMap["Greater"].push_back({"__stride_Greater_b_dd", FT});
  }
  // Initialize types map
  typesMap["_RealType"] = llvm::Type::getDoubleTy(*TheContext);
  typesMap["_SwitchType"] = llvm::Type::getInt1Ty(*TheContext);
}

std::optional<ExternalFunction> StrideCompiler::getExternalFunction(
    std::string strideName, llvm::Type *returnType,
    std::vector<llvm::Type *> argTypes, bool allowConversion) {
  std::optional<ExternalFunction> out;
  for (const auto &externFunc : functionMap) {
    if (externFunc.first == strideName) {
      for (const auto &candidate : externFunc.second) {
        llvm::FunctionType *llvmFType = candidate.llvmFunctionType;
        if (llvmFType->getReturnType() == returnType) {
          if (argTypes.size() == llvmFType->getNumParams()) {
            for (int i = 0; i < argTypes.size(); i++) {
              if (argTypes[i] != llvmFType->getParamType(i)) {
                return out;
              }
            }
            out = candidate;
          }
        }
      }
    }
  }

  return out;
}

llvm::Function *StrideCompiler::getFunctionInModule(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;
  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen(*this);
  // If no existing prototype exists, return null.
  return nullptr;
}

llvm::AllocaInst *
StrideCompiler::CreateEntryBlockAlloca(llvm::Function *TheFunction,
                                       llvm::StringRef VarName) {
  llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                         TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(llvm::Type::getDoubleTy(*TheContext), nullptr,
                           VarName);
}

llvm::Type *StrideCompiler::getLLVMType(std::shared_ptr<DeclarationNode> decl) {
  auto typePropNode = decl->getPropertyValue("type");
  std::string type = "_RealType";
  if (decl->getObjectType() == "switch") {
    return typesMap["_SwitchType"];
  }
  if (typePropNode) {
    if (typePropNode->getNodeType() == AST::Block) {
      type = std::static_pointer_cast<BlockNode>(typePropNode)->getName();
    } else {
      std::cout << __FILE__ << ":" << __LINE__ << "unsupported type"
                << std::endl;
    }
  }
  return typesMap[type];
}
