#include <iostream>

#include "blocknode.h"
#include "stridecompiler.hpp"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

extern "C" {
EXPORT double __stride_Greater_d_dd(double a, double b) {
  return a > b ? 1.0 : 0.0;
}
EXPORT bool __stride_Greater_b_dd(double a, double b) {
  // TODO should be removed and llvm functions for this should be used instead
  return a > b;
}

EXPORT bool __stride_Greater_b_ii(int32_t a, int32_t b) {
  // TODO should be removed and llvm functions for this should be used instead
  return a > b;
}
EXPORT bool __stride_Equal_b_dd(double a, double b) {
  // TODO should be removed and llvm functions for this should be used instead
  return a == b;
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

  // Initialize types map
  typesMap["_RealType"] = llvm::Type::getDoubleTy(*TheContext);
  typesMap["_DoubleType"] = llvm::Type::getDoubleTy(*TheContext);
  typesMap["_IntType"] = llvm::Type::getInt32Ty(*TheContext);
  typesMap["_SwitchType"] = llvm::Type::getInt1Ty(*TheContext);

  typesMap[""] = llvm::Type::getVoidTy(*TheContext);
}

std::optional<ExternalFunction> StrideCompiler::getExternalFunction(
    std::string strideName, llvm::Type *returnType,
    std::vector<llvm::Type *> argTypes, bool allowConversion) {
  std::optional<ExternalFunction> out;
  for (const auto &externFunc : functionMap) {
    if (externFunc.first == strideName) {
      for (const auto &candidate : externFunc.second) {
        llvm::FunctionType *llvmFType = candidate.llvmFunctionType;
        //        std::cout << llvmFType->getReturnType() << std::endl;
        if (llvmFType->getReturnType() == returnType) {
          if (argTypes.size() == llvmFType->getNumParams()) {
            bool allTypesMatch = true;
            for (int i = 0; i < argTypes.size(); i++) {
              if (argTypes[i] != llvmFType->getParamType(i)) {
                allTypesMatch = false;
                break;
              }
            }
            if (allTypesMatch) {
              return candidate;
            }
            if (!out) {
              // for now remember first function that matches output type
              // This should be evaluated to find the best match according to
              // type casting
              out = candidate;
            }
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
                                       llvm::StringRef VarName,
                                       llvm::Type *dataType) {
  llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                         TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(dataType, nullptr, VarName);
}

void StrideCompiler::setConfiguration(StrideConfig option, bool enable) {
  if (enable) {
    m_configuration |= option;
  } else {
    m_configuration &= (UINT64_MAX & ~option);
  }
}

bool StrideCompiler::hasConfiguration(StrideConfig option) {
  return (m_configuration & option) == option;
}

llvm::Type *StrideCompiler::getLLVMType(std::shared_ptr<DeclarationNode> decl) {
  if (!decl) {
    return typesMap[""];
  }
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
  if (decl->getObjectType() == "reaction") {
    return typesMap["_SwitchType"];
  }
  return typesMap[type];
}
