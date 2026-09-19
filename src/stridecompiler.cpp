#include <functional>
#include <iostream>

#include "stride/parser/blocknode.h"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/utils/astquery.h"

// #include "llvm/ADT/APFloat.h"
// #include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
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
                std::cout << "Type mismatch for " << strideName << " arg " << i
                          << std::endl;
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
              out = candidate;
            }
          } else {
            std::cout << "Param count mismatch for " << strideName
                      << " expected " << llvmFType->getNumParams() << " got "
                      << argTypes.size() << std::endl;
          }
        } else {
          std::cout << "Return type mismatch for " << strideName << std::endl;
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

llvm::AllocaInst *
StrideCompiler::CreateEntryBlockAllocaArray(llvm::Function *TheFunction,
                                            llvm::StringRef VarName,
                                            llvm::Type *dataType, size_t size) {

  llvm::ArrayType *arrayTy = llvm::ArrayType::get(dataType, size);
  llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                         TheFunction->getEntryBlock().begin());

  llvm::AllocaInst *arrayPtr = TmpB.CreateAlloca(arrayTy, nullptr, VarName);
  // auto *Alloca = TmpB.CreateAlloca(dataType, nullptr, VarName);
  pointerElementTypes[arrayPtr] = dataType;
  return arrayPtr;
}

llvm::AllocaInst *StrideCompiler::CreateEntryBlockAllocaArrayConst(
    llvm::Function *TheFunction, llvm::StringRef VarName, llvm::Type *dataType,
    size_t size) {

  llvm::ArrayType *arrayTy = llvm::ArrayType::get(dataType, size);
  llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                         TheFunction->getEntryBlock().begin());

  llvm::AllocaInst *arrayPtr = TmpB.CreateAlloca(arrayTy, nullptr, VarName);
  // auto *Alloca = TmpB.CreateAlloca(dataType, nullptr, VarName);
  pointerElementTypes[arrayPtr] = dataType;
  return arrayPtr;
}

void StrideCompiler::createGlobal(std::shared_ptr<DeclarationNode> globalDecl) {
  auto namePrefix = getName();
  std::string fullName;
  if (namePrefix.size() > 0) {
    fullName = getName() + "_";
  }
  fullName += globalDecl->getName();
  llvm::Type *Type = getLLVMType(globalDecl);
  if (globalDecl->getNodeType() == AST::BundleDeclaration) {
    int size = ASTQuery::getBlockDeclaredSize(globalDecl, {}, nullptr);
    if (size > 0) {
      Type = llvm::ArrayType::get(Type, size);
    } else {
      std::cout << " Error: Undefined size for global not possible"
                << std::endl;
      return;
    }
  }
  // TODO initialize
  llvm::Constant *Initializer = llvm::UndefValue::get(Type);

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
  std::cout << "global: " << fullName << std::endl;

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
        if (functionInstance) {
          auto portConnection = functionInstance->getPropertyValue("inputBlock");
          // auto type = portConnection->getCompilerProperty("declaration");
          // if (type) {
          // }
        }
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

bool StrideCompiler::isModuleNode(ASTNode node) const {
  if (!node) {
    return false;
  }
  if (node->getNodeType() == AST::Declaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    return decl->getObjectType() == "module";
  }
  if (m_tree) {
    auto decl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node), {},
                                                m_tree);
    if (decl) {
      return decl->getObjectType() == "module";
    }
  }
  return false;
}

bool StrideCompiler::doesNodeNeedState(const CodeAnalysis::TypeTree *node) {
  if (!node) {
    return false;
  }
  // Only modules can have persistent state of their own, and only if they have persistent variables.
  // Reactions and loops do not have state of their own.
  if (isModuleNode(node->instance) && !node->persistent.empty()) {
    return true;
  }
  // Any callable needs state if any nested child needs state
  for (const auto &child : node->nodes) {
    if (doesNodeNeedState(&child)) {
      return true;
    }
  }
  return false;
}

const StrideCompiler::StateStructInfo *
StrideCompiler::getStateStructInfo(ASTNode instance) const {
  if (!instance) {
    return nullptr;
  }
  auto it = stateStructMap.find(instance);
  if (it != stateStructMap.end()) {
    return &it->second;
  }
  // Fallback by name
  return getStateStructInfo(ASTQuery::getNodeName(instance));
}

const StrideCompiler::StateStructInfo *
StrideCompiler::getStateStructInfo(const std::string &name) const {
  if (name.empty()) {
    return nullptr;
  }
  for (const auto &pair : stateStructMap) {
    if (pair.first && ASTQuery::getNodeName(pair.first) == name) {
      return &pair.second;
    }
  }
  return nullptr;
}

const CodeAnalysis::TypeTree *
StrideCompiler::findTypeTreeNode(ASTNode node,
                                 const CodeAnalysis::TypeTree *tree) const {
  if (!node) {
    return nullptr;
  }
  if (!tree) {
    tree = &m_intanceTree;
  }
  if (tree->instance == node) {
    return tree;
  }
  for (const auto &child : tree->nodes) {
    auto *found = findTypeTreeNode(node, &child);
    if (found) {
      return found;
    }
  }
  return nullptr;
}

llvm::Constant *
StrideCompiler::StateStructInfo::getDefaultValue(const std::string &varName) const {
  auto it = varIndices.find(varName);
  if (it != varIndices.end() && defaultConstant) {
    return llvm::cast<llvm::Constant>(defaultConstant->getOperand(it->second));
  }
  return nullptr;
}

llvm::Constant *
StrideCompiler::StateStructInfo::getChildDefaultConstant(ASTNode childInstance) const {
  auto it = childIndices.find(childInstance);
  if (it != childIndices.end() && defaultConstant) {
    return llvm::cast<llvm::Constant>(defaultConstant->getOperand(it->second));
  }
  return nullptr;
}

void StrideCompiler::buildStateStructTypes(const CodeAnalysis::TypeTree &tree) {
  stateStructMap.clear();

  std::function<void(const CodeAnalysis::TypeTree &)> buildNodeState =
      [&](const CodeAnalysis::TypeTree &nodeTree) {
    if (!doesNodeNeedState(&nodeTree)) {
      return;
    }

    // First recursively build child state structs
    for (const auto &child : nodeTree.nodes) {
      buildNodeState(child);
    }

    StateStructInfo info;
    std::vector<llvm::Type *> fieldTypes;
    std::vector<llvm::Constant *> defaultFieldValues;

    // 1. Persistent fields for this node's own state (only modules have state of their own)
    if (isModuleNode(nodeTree.instance)) {
      for (const auto &var : nodeTree.persistent) {
        std::string varName = ASTQuery::getNodeName(var.first);
        llvm::Type *varType = nullptr;
        if (typesMap.find(var.second) != typesMap.end()) {
          varType = typesMap[var.second];
        } else if (var.first->getNodeType() == AST::Declaration) {
          varType =
              getLLVMType(std::static_pointer_cast<DeclarationNode>(var.first));
        }
        if (!varType) {
          varType = llvm::Type::getDoubleTy(*TheContext);
        }
        info.varIndices[varName] = static_cast<unsigned>(fieldTypes.size());
        fieldTypes.push_back(varType);

        llvm::Constant *defaultVal = nullptr;
        if (var.first->getNodeType() == AST::Declaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
          auto defaultNode = decl->getPropertyValue("default");
          if (defaultNode) {
            if (defaultNode->getNodeType() == AST::Int) {
              int64_t val =
                  std::static_pointer_cast<ValueNode>(defaultNode)->getIntValue();
              if (varType->isIntegerTy(64)) {
                defaultVal = llvm::ConstantInt::get(varType, val);
              } else {
                defaultVal =
                    llvm::ConstantInt::get(varType, static_cast<int32_t>(val));
              }
            } else if (defaultNode->getNodeType() == AST::Real) {
              double val =
                  std::static_pointer_cast<ValueNode>(defaultNode)->getRealValue();
              defaultVal = llvm::ConstantFP::get(varType, val);
            } else if (defaultNode->getNodeType() == AST::Switch) {
              bool val =
                  std::static_pointer_cast<ValueNode>(defaultNode)->getSwitchValue();
              defaultVal = llvm::ConstantInt::get(varType, val ? 1 : 0);
            }
          }
        }
        if (!defaultVal) {
          defaultVal = llvm::Constant::getNullValue(varType);
        }
        defaultFieldValues.push_back(defaultVal);
      }
    }

    // 2. Sub-struct fields for each child node requiring state
    for (const auto &child : nodeTree.nodes) {
      if (doesNodeNeedState(&child)) {
        auto childIt = stateStructMap.find(child.instance);
        if (childIt == stateStructMap.end()) {
          // Fallback by name
          std::string childName =
              child.instance ? ASTQuery::getNodeName(child.instance) : "";
          for (auto it = stateStructMap.begin(); it != stateStructMap.end();
               ++it) {
            if (it->first && ASTQuery::getNodeName(it->first) == childName) {
              childIt = it;
              break;
            }
          }
        }
        if (childIt != stateStructMap.end() && childIt->second.structType) {
          info.childIndices[child.instance] =
              static_cast<unsigned>(fieldTypes.size());
          fieldTypes.push_back(childIt->second.structType);
          defaultFieldValues.push_back(
              childIt->second.defaultConstant
                  ? childIt->second.defaultConstant
                  : llvm::Constant::getNullValue(childIt->second.structType));
        }
      }
    }

    if (!fieldTypes.empty()) {
      std::string name = nodeTree.instance
                             ? ASTQuery::getNodeName(nodeTree.instance)
                             : "anon";
      info.structType =
          llvm::StructType::create(*TheContext, "struct." + name + "_state");
      info.structType->setBody(fieldTypes);
      info.defaultConstant =
          llvm::ConstantStruct::get(info.structType, defaultFieldValues);
      stateStructMap[nodeTree.instance] = info;
    }
  };

  if (tree.instance && tree.instance->getNodeType() == AST::Declaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(tree.instance);
    if (decl->getObjectType() != "_domainDefinition") {
      buildNodeState(tree);
      return;
    }
  } else if (tree.instance) {
    buildNodeState(tree);
    return;
  }

  for (const auto &node : tree.nodes) {
    if (node.instance && node.instance->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node.instance);
      if (decl->getObjectType() == "_domainDefinition") {
        for (const auto &funcTree : node.nodes) {
          buildNodeState(funcTree);
        }
        continue;
      }
    }
    buildNodeState(node);
  }
}

