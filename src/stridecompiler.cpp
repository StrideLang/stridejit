#include <iostream>

#include "stride/parser/blocknode.h"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/utils/astquery.h"

// #include "llvm/ADT/APFloat.h"
// #include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
// #include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
// #include "llvm/IR/Verifier.h"

extern "C" {
EXPORT double __stride_Greater_d_dd(double a, double b) {
  return a > b ? 1.0 : 0.0;
}
}

using namespace strd;

StrideCompiler::StrideCompiler() {
  TheContext = std::make_unique<llvm::LLVMContext>();
  TheModule = std::make_unique<llvm::Module>("StrideJit", *this->TheContext);
  Builder = std::make_unique<llvm::IRBuilder<>>(*this->TheContext);

  // TODO these should not be hardcoded here, but defined in the platform stride
  // files
  BinopPrecedence['='] = 2;
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40; // highest.

  // Initialize types map
  typesMap["_RealType"] = llvm::Type::getDoubleTy(*TheContext);
  typesMap["_DoubleType"] = llvm::Type::getDoubleTy(*TheContext);
  typesMap["_FloatType"] = llvm::Type::getFloatTy(*TheContext);
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
              std::cout << "Found external candidate for " << strideName
                        << std::endl;
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
  auto *Alloca = TmpB.CreateAlloca(dataType, nullptr, VarName);
  pointerElementTypes[Alloca] = dataType;
  return Alloca;
}

void StrideCompiler::createGlobal(std::shared_ptr<DeclarationNode> globalDecl) {
  auto namePrefix = getName();
  std::string fullName;
  if (namePrefix.size() > 0) {
    fullName = getName() + "_";
  }
  fullName += globalDecl->getName();
  llvm::Type *Type = getLLVMType(globalDecl);
  // TODO initialize
  llvm::Constant *Initializer = llvm::UndefValue::get(Type);
  // llvm::ConstantInt::get(Int32Ty, 42);
  llvm::GlobalVariable *MyGlobal = new llvm::GlobalVariable(
      *TheModule, Type,
      false, // Is it constant (read-only)? false = mutable
      llvm::GlobalValue::ExternalLinkage, // Linkage type (External makes it
      // visible to the JIT)
      Initializer, fullName);

  // Avoid optimization to force memory location
  MyGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::None);
  // Optional: Set data alignment for optimal CPU access
  MyGlobal->setAlignment(llvm::MaybeAlign(4));
  std::cout << "Created global: " << fullName << std::endl;

  m_globals[globalDecl->getName()] = {MyGlobal, Type};
}

llvm::Type *StrideCompiler::getElementType(llvm::Value *V) {
  if (!V)
    return nullptr;
  if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(V)) {
    return alloca->getAllocatedType();
  }
  if (auto *global = llvm::dyn_cast<llvm::GlobalVariable>(V)) {
    return global->getValueType();
  }
  if (auto *arg = llvm::dyn_cast<llvm::Argument>(V)) {
    if (auto *PF = arg->getParent()) {
      return PF->getFunctionType()->getParamType(arg->getArgNo());
      // Wait, for pointers, we might need the pointee type.
      // But arguments are often pointers to types.
    }
  }
  auto it = pointerElementTypes.find(V);
  if (it != pointerElementTypes.end()) {
    return it->second;
  }
#if LLVM_VERSION_MAJOR < 17
  if (V->getType()->isPointerTy()) {
    // This will emit a warning but work in older LLVMs
    return V->getType()->getPointerElementType();
  }
#endif
  return nullptr;
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

llvm::Type *
StrideCompiler::getLLVMType(std::shared_ptr<strd::DeclarationNode> decl) {
  if (!decl) {
    return typesMap[""];
  }
  if (decl->getObjectType() == "switch" || decl->getObjectType() == "trigger") {
    return typesMap["_SwitchType"];
  }
  auto typePropNode = decl->getPropertyValue("type");
  std::string type = "_RealType";
  if (typePropNode) {
    if (typePropNode->getNodeType() == strd::AST::Block) {
      type = std::static_pointer_cast<strd::BlockNode>(typePropNode)->getName();
    } else {
      std::cout << __FILE__ << ":" << __LINE__ << " : unsupported type"
                << std::endl;
    }
  }
  if (decl->getObjectType() == "reaction") {
    return typesMap["_SwitchType"];
  }
  return typesMap[type];
}

llvm::Type *StrideCompiler::getLLVMTypeForCodegenBlock(
    std::shared_ptr<DeclarationNode> decl,
    std::shared_ptr<DeclarationNode> funcDecl,
    std::shared_ptr<FunctionNode> functionInstance) {
  if (!decl || !functionInstance) {
    return typesMap[""];
  }
  if (decl->getObjectType() == "switch" || decl->getObjectType() == "trigger") {
    return typesMap["_SwitchType"];
  }
  auto typePropNode = decl->getPropertyValue("type");
  std::string type = "_RealType";
  if (typePropNode) {
    if (typePropNode->getNodeType() == strd::AST::Block) {
      type = std::static_pointer_cast<strd::BlockNode>(typePropNode)->getName();
    } else if (typePropNode->getNodeType() == strd::AST::PortProperty) {
      auto typeProp =
          std::static_pointer_cast<strd::PortPropertyNode>(typePropNode);
      if (funcDecl->getObjectType() == "platformModule") {
        auto inputBlock = funcDecl->getCompilerProperty("inputBlock");
        if (inputBlock) {
        }
      }
      auto inputPortBlock =
          strd::ASTQuery::getModuleMainInputPortBlock(funcDecl);
      if (inputPortBlock && typeProp &&
          inputPortBlock->getName() == typeProp->getName()) {
        if (typeProp->getPortName() != "type") {
          std::cerr << "ERROR invalid port for type for " << decl->toText()
                    << std::endl;
          return typesMap[type];
        }
        auto portConnection = functionInstance->getPropertyValue("inputBlock");
        // auto type = portConnection->getCompilerProperty("declaration");
        // if (type) {
        // }
      }
    } else {
      std::cout << __FILE__ << ":" << __LINE__ << " : unsupported type"
                << std::endl;
    }
  }
  if (decl->getObjectType() == "reaction") {
    return typesMap["_SwitchType"];
  }
  return typesMap[type];
}

void StrideCompiler::pushName(std::string name) {
  char buf[4]; // 3 chars + 1 null terminator
  std::snprintf(buf, sizeof(buf), "%03d", m_idCounter++);
  m_nameStack.push_back(name + "_" + std::string(buf));
}

void StrideCompiler::popName() { m_nameStack.pop_back(); }

std::string StrideCompiler::getName() {
  std::string result = "";
  for (const auto &name : m_nameStack) {
    result += name + "_";
  }
  if (result.size() > 0) {
    result.pop_back();
  }
  return result;
}

std::pair<llvm::Value *, std::optional<llvm::Type *>>
strd::StrideCompiler::getGlobal(std::string globalName) {
  if (globalExists(globalName)) {
    auto global = m_globals[globalName];
    return global;
  }
  return {nullptr, std::nullopt};
}

bool StrideCompiler::globalExists(std::string globalName) {
  return m_globals.find(globalName) != m_globals.end();
}
