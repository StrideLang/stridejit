#include "stride/utils/logger.h"
#include <functional>
#include <iostream>

#include "stride/parser/arraynode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/stridejit/bufferspec.hpp"
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
#if LLVM_VERSION_MAJOR < 15
  TheContext->enableOpaquePointers();
#endif
  TheModule = std::make_unique<llvm::Module>("StrideJit", *this->TheContext);
  Builder = std::make_unique<llvm::IRBuilder<>>(*this->TheContext);

  // TODO these should not be hardcoded here, but defined in the platform stride
  // files
  BinopPrecedence['='] = 2;
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40; // highest.

  // Builtin data types. registerDataTypes() replaces them with the
  // declarations of the framework.
  for (const auto &name :
       {"Int8", "Int16", "Int32", "Int64", "UInt8", "UInt16", "UInt32",
        "UInt64", "Float32", "Float64", "Switch", "String", "Int", "UInt",
        "Real", "Size"}) {
    auto info = DataTypes::builtin(name);
    dataTypes[name] = info;
    typesMap[name] = getLLVMTypeForDataType(info);
  }

  typesMap[""] = llvm::Type::getVoidTy(*TheContext);
}

StrideCompiler::~StrideCompiler() {
  // The debug emitter's DIBuilder untracks metadata owned by TheContext, so
  // it must go first. Members are destroyed in reverse order of declaration,
  // which would destroy it after the context.
  debugEmitter.reset();
}

llvm::Type *
StrideCompiler::getLLVMTypeForDataType(const DataTypes::Info &info) {
  switch (info.kind) {
  case DataTypes::Kind::Integer:
    return llvm::Type::getIntNTy(*TheContext, info.bits);
  case DataTypes::Kind::Float:
    return info.bits == 32 ? llvm::Type::getFloatTy(*TheContext)
                           : llvm::Type::getDoubleTy(*TheContext);
  case DataTypes::Kind::Switch:
    return llvm::Type::getInt1Ty(*TheContext);
  case DataTypes::Kind::String:
    return llvm::StructType::get(*TheContext,
                                 {llvm::PointerType::get(*TheContext, 0),
                                  llvm::Type::getInt32Ty(*TheContext)});
  case DataTypes::Kind::Unknown:
    break;
  }
  return nullptr;
}

void StrideCompiler::registerDataTypes(ASTNode tree) {
  if (!tree) {
    return;
  }
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    // Aliases of data types (alias Int { entity: Int32 }) get the LLVM type
    // of the type they stand for
    if (decl->getEntityType() != "frameworkDataType" &&
        decl->getEntityType() != "alias") {
      continue;
    }
    auto info = DataTypes::resolve(decl->getName(), tree);
    if (auto *type = getLLVMTypeForDataType(info)) {
      dataTypes[decl->getName()] = info;
      typesMap[decl->getName()] = type;
    }
  }
}

DataTypes::Info StrideCompiler::getDataTypeInfo(const std::string &name) {
  auto it = dataTypes.find(name);
  if (it != dataTypes.end()) {
    return it->second;
  }
  DataTypes::Info info;
  info.name = name;
  return info;
}

std::string
StrideCompiler::getDataTypeName(std::shared_ptr<strd::DeclarationNode> decl) {
  if (!decl) {
    return std::string();
  }
  if (decl->getEntityType() == "switch" || decl->getEntityType() == "trigger" ||
      decl->getEntityType() == "reaction") {
    return DataTypes::Switch;
  }
  if (decl->getEntityType() == "string") {
    return DataTypes::String;
  }
  auto typeNode = decl->getPropertyValue("type");
  if (typeNode && typeNode->getNodeType() == AST::Entity) {
    return ASTQuery::getNodeName(typeNode);
  }
  return std::string();
}

llvm::Value *StrideCompiler::convertValue(llvm::Value *V, llvm::Type *toType,
                                          bool fromSigned, bool toSigned) {
  if (!V || !toType) {
    return V;
  }
  llvm::Type *fromType = V->getType();
  if (fromType == toType) {
    return V;
  }
  if (fromType->isIntegerTy() && toType->isIntegerTy()) {
    if (toType->isIntegerTy(1)) {
      return Builder->CreateICmpNE(V, llvm::ConstantInt::get(fromType, 0),
                                   "tobool");
    }
    if (fromType->isIntegerTy(1)) {
      return Builder->CreateZExt(V, toType, "frombool");
    }
    return Builder->CreateIntCast(V, toType, fromSigned, "intcast");
  }
  if (fromType->isIntegerTy() && toType->isFloatingPointTy()) {
    if (fromSigned && !fromType->isIntegerTy(1)) {
      return Builder->CreateSIToFP(V, toType, "sitofp");
    }
    return Builder->CreateUIToFP(V, toType, "uitofp");
  }
  if (fromType->isFloatingPointTy() && toType->isIntegerTy()) {
    if (toType->isIntegerTy(1)) {
      return Builder->CreateFCmpUNE(V, llvm::ConstantFP::get(fromType, 0.0),
                                    "tobool");
    }
    if (toSigned) {
      return Builder->CreateFPToSI(V, toType, "fptosi");
    }
    return Builder->CreateFPToUI(V, toType, "fptoui");
  }
  if (fromType->isFloatingPointTy() && toType->isFloatingPointTy()) {
    return Builder->CreateFPCast(V, toType, "fpcast");
  }
  return V;
}

llvm::Value *StrideCompiler::convertToDataType(llvm::Value *V,
                                               const std::string &fromDataType,
                                               const std::string &toDataType) {
  if (!V || toDataType.empty()) {
    return V;
  }
  auto toInfo = getDataTypeInfo(toDataType);
  if (!toInfo.isNumeric() && toInfo.kind != DataTypes::Kind::Switch) {
    return V;
  }
  bool fromSigned = true;
  if (!fromDataType.empty()) {
    auto fromInfo = getDataTypeInfo(fromDataType);
    if (fromInfo.valid()) {
      fromSigned = fromInfo.isSigned;
    }
  }
  return convertValue(V, getLLVMTypeForDataType(toInfo), fromSigned,
                      toInfo.isSigned);
}

void StrideCompiler::addDynamicDomainField(ASTNode domainNode,
                                           const std::string &name,
                                           llvm::Type *type,
                                           llvm::Constant *defaultVal) {
  if (domainNode) {
    dynamicDomainFields[domainNode].push_back({name, type, defaultVal});
  }
}

std::optional<ExternalFunction> StrideCompiler::getExternalFunction(
    std::string strideName, llvm::Type *returnType,
    std::vector<llvm::Type *> argTypes, bool allowConversion) {
  std::optional<ExternalFunction> out;
  for (const auto &externFunc : functionMap) {
    if (externFunc.first == strideName) {
      for (const auto &candidate : externFunc.second) {
        if (candidate.isConstrainedList) {
          if (!returnType ||
              candidate.llvmFunctionType->getReturnType() == returnType) {
            size_t prefixCount = candidate.prefixTypes.size();
            if (argTypes.size() >= prefixCount) {
              bool prefixMatch = true;
              for (size_t p = 0; p < prefixCount; ++p) {
                if (argTypes[p] != candidate.prefixTypes[p]) {
                  prefixMatch = false;
                  break;
                }
              }
              int nVarArgs = static_cast<int>(argTypes.size() - prefixCount);
              if (prefixMatch && nVarArgs >= candidate.minItems &&
                  (candidate.maxItems < 0 || nVarArgs <= candidate.maxItems)) {
                bool allTypesMatch = true;
                if (!candidate.variadicTypes.empty()) {
                  for (size_t i = prefixCount; i < argTypes.size(); ++i) {
                    bool matchedOne = false;
                    for (auto *allowedTy : candidate.variadicTypes) {
                      if (argTypes[i] == allowedTy) {
                        matchedOne = true;
                        break;
                      }
                    }
                    if (!matchedOne) {
                      allTypesMatch = false;
                      break;
                    }
                  }
                } else if (candidate.elemType) {
                  for (size_t i = prefixCount; i < argTypes.size(); ++i) {
                    if (argTypes[i] != candidate.elemType) {
                      allTypesMatch = false;
                      break;
                    }
                  }
                }
                if (allTypesMatch) {
                  LOG_DEBUG()
                      << "Found variadic external candidate for " << strideName
                      << " with " << nVarArgs << " varargs" << std::endl;
                  return candidate;
                }
              }
            }
          }
          continue;
        }
        llvm::FunctionType *llvmFType = candidate.llvmFunctionType;
        //        LOG_INFO() << llvmFType->getReturnType() << std::endl;
        if (llvmFType->getReturnType() == returnType) {
          if (argTypes.size() == llvmFType->getNumParams()) {
            bool allTypesMatch = true;
            for (int i = 0; i < argTypes.size(); i++) {
              if (argTypes[i] != llvmFType->getParamType(i)) {
                LOG_DEBUG() << "Type mismatch for " << strideName << " arg "
                            << i << std::endl;
                allTypesMatch = false;
                break;
              }
            }
            if (allTypesMatch) {
              LOG_DEBUG() << "Found external candidate for " << strideName
                          << std::endl;
              return candidate;
            }
            if (!out) {
              out = candidate;
            }
          } else {
            LOG_DEBUG() << "Param count mismatch for " << strideName
                        << " expected " << llvmFType->getNumParams() << " got "
                        << argTypes.size() << std::endl;
          }
        } else {
          LOG_DEBUG() << "Return type mismatch for " << strideName << std::endl;
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
  if (!dataType || dataType->isVoidTy()) {
    dataType = llvm::Type::getDoubleTy(*TheContext);
  }
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
  if (!dataType || dataType->isVoidTy()) {
    dataType = llvm::Type::getDoubleTy(*TheContext);
  }
  llvm::ArrayType *arrayTy = llvm::ArrayType::get(dataType, size);
  llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                         TheFunction->getEntryBlock().begin());

  llvm::AllocaInst *arrayPtr = TmpB.CreateAlloca(arrayTy, nullptr, VarName);
  pointerElementTypes[arrayPtr] = dataType;
  return arrayPtr;
}

llvm::AllocaInst *StrideCompiler::CreateEntryBlockAllocaArrayConst(
    llvm::Function *TheFunction, llvm::StringRef VarName, llvm::Type *dataType,
    size_t size) {
  if (!dataType || dataType->isVoidTy()) {
    dataType = llvm::Type::getDoubleTy(*TheContext);
  }
  llvm::ArrayType *arrayTy = llvm::ArrayType::get(dataType, size);
  llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                         TheFunction->getEntryBlock().begin());

  llvm::AllocaInst *arrayPtr = TmpB.CreateAlloca(arrayTy, nullptr, VarName);
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
  if (globalDecl->getNodeType() == AST::ArrayDeclaration ||
      globalDecl->getEntityType() == "buffer") {
    int size = 0;
    if (globalDecl->getEntityType() == "buffer") {
      // getLLVMType already sized the storage
      size = Type->isArrayTy() ? static_cast<int>(Type->getArrayNumElements())
                               : 0;
    } else {
      size = ASTQuery::getEntityDeclaredSize(globalDecl, {}, nullptr);
    }
    if (size > 0) {
      if (!Type->isArrayTy()) {
        Type = llvm::ArrayType::get(Type, size);
      }
    } else {
      LOG_INFO() << " Error: Undefined size for global not possible"
                 << std::endl;
      return;
    }
  }
  llvm::Constant *Initializer = nullptr;
  auto valProp = globalDecl->getPropertyValue("value");
  if (!valProp) {
    valProp = globalDecl->getPropertyValue("default");
  }
  if (!Initializer && Type->isArrayTy()) {
    Initializer = llvm::ConstantAggregateZero::get(Type);
  }
  if (valProp) {
    if (valProp->getNodeType() == AST::Int) {
      int64_t v = std::static_pointer_cast<ValueNode>(valProp)->getIntValue();
      if (Type->isIntegerTy(32)) {
        Initializer = llvm::ConstantInt::get(Type, static_cast<int32_t>(v));
      } else if (Type->isIntegerTy()) {
        Initializer = llvm::ConstantInt::get(Type, v);
      } else if (Type->isFloatingPointTy()) {
        Initializer = llvm::ConstantFP::get(Type, static_cast<double>(v));
      }
    } else if (valProp->getNodeType() == AST::Real) {
      double v = std::static_pointer_cast<ValueNode>(valProp)->getRealValue();
      if (Type->isFloatingPointTy()) {
        Initializer = llvm::ConstantFP::get(Type, v);
      } else if (Type->isIntegerTy()) {
        Initializer = llvm::ConstantInt::get(Type, static_cast<int64_t>(v));
      }
    } else if (valProp->getNodeType() == AST::Switch) {
      bool v = std::static_pointer_cast<ValueNode>(valProp)->getSwitchValue();
      Initializer = llvm::ConstantInt::get(Type, v ? 1 : 0);
    } else if (valProp->getNodeType() == AST::String && Type->isStructTy()) {
      std::string s =
          std::static_pointer_cast<ValueNode>(valProp)->getStringValue();
      auto *strConst = llvm::ConstantDataArray::getString(*TheContext, s, true);
      auto *strGlobal = new llvm::GlobalVariable(
          *TheModule, strConst->getType(), true,
          llvm::GlobalValue::PrivateLinkage, strConst, ".str.const");
      llvm::Constant *zero =
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 0);
      llvm::Constant *indices[] = {zero, zero};
      llvm::Constant *ptrConst = llvm::ConstantExpr::getInBoundsGetElementPtr(
          strConst->getType(), strGlobal, indices);
      llvm::Constant *lenConst = llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(*TheContext), static_cast<int32_t>(s.size()));
      Initializer = llvm::ConstantStruct::get(
          llvm::cast<llvm::StructType>(Type), {ptrConst, lenConst});
    } else if (valProp->getNodeType() == AST::List && Type->isArrayTy()) {
      Initializer = makeDefaultConstant(valProp, Type);
      if (!Initializer) {
        LOG_ERROR() << "Initial values of '" << globalDecl->getName()
                    << "' do not match its size of "
                    << Type->getArrayNumElements() << std::endl;
      }
    }
  }
  if (!Initializer) {
    Initializer = llvm::Constant::getNullValue(Type);
  }

  // A constant is read-only data, private to the module. Lookups with a
  // constant index are folded, and LLVM removes the global when nothing reads
  // it at run time. A constant without a value is written once at run time
  // (a table filled at setup), so it stays private but writable.
  bool isConstant = ASTQuery::isConstant(globalDecl, {}, m_tree);
  auto constantValue = globalDecl->getPropertyValue("value");
  bool isReadOnly = isConstant && constantValue &&
                    constantValue->getNodeType() != AST::None;
  llvm::GlobalVariable *MyGlobal = new llvm::GlobalVariable(
      *TheModule, Type, isReadOnly,
      // External linkage makes signals visible to the JIT
      isConstant ? llvm::GlobalValue::InternalLinkage
                 : llvm::GlobalValue::ExternalLinkage,
      Initializer, fullName);

  // Avoid optimization to force memory location
  MyGlobal->setUnnamedAddr(isReadOnly ? llvm::GlobalValue::UnnamedAddr::Global
                                      : llvm::GlobalValue::UnnamedAddr::None);
  // Optional: Set data alignment for optimal CPU access
  MyGlobal->setAlignment(llvm::MaybeAlign(4));
  LOG_INFO() << "global: " << fullName << std::endl;

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
  if (decl->getEntityType() == "switch" || decl->getEntityType() == "trigger") {
    return typesMap[DataTypes::Switch];
  }
  if (decl->getEntityType() == "string") {
    return typesMap[DataTypes::String];
  }
  if (decl->getEntityType() == "constant") {
    if (auto valProp = decl->getPropertyValue("value")) {
      if (valProp->getNodeType() == strd::AST::String) {
        return typesMap[DataTypes::String];
      } else if (valProp->getNodeType() == strd::AST::Int) {
        return typesMap[DataTypes::Int];
      } else if (valProp->getNodeType() == strd::AST::Switch) {
        return typesMap[DataTypes::Switch];
      } else if (valProp->getNodeType() == strd::AST::Real) {
        return typesMap[DataTypes::Real];
      } else if (valProp->getNodeType() == strd::AST::List) {
        // A constant array takes the type of its values
        auto listType =
            CodeAnalysis::getDataTypeForDeclaration(decl, {}, m_tree);
        if (typesMap.find(listType) != typesMap.end() &&
            !typesMap[listType]->isVoidTy()) {
          return typesMap[listType];
        }
      }
    }
  }
  auto typePropNode = decl->getPropertyValue("type");
  std::string type = DataTypes::Real;
  if (typePropNode) {
    if (typePropNode->getNodeType() == strd::AST::Entity) {
      type =
          std::static_pointer_cast<strd::EntityNode>(typePropNode)->getName();
    } else if (typePropNode->getNodeType() == strd::AST::MemberAccess) {
      type = DataTypes::Real;
    } else {
      LOG_INFO() << " : unsupported type" << std::endl;
    }
  }
  if (decl->getEntityType() == "reaction") {
    return typesMap[DataTypes::Switch];
  }
  if (decl->getEntityType() == "buffer") {
    auto spec = BufferSpec::fromDeclaration(decl, m_tree);
    llvm::Type *elemTy = (!type.empty() && typesMap.find(type) != typesMap.end() &&
                          !typesMap[type]->isVoidTy())
                             ? typesMap[type]
                             : typesMap[DataTypes::Real];
    if (spec.valid()) {
      return llvm::ArrayType::get(elemTy, spec.storageSize());
    }
    return elemTy;
  }
  if (!type.empty() && typesMap.find(type) != typesMap.end() &&
      !typesMap[type]->isVoidTy()) {
    return typesMap[type];
  }
  return typesMap[DataTypes::Real];
}

llvm::Type *StrideCompiler::getLLVMTypeForCodegenEntity(
    std::shared_ptr<DeclarationNode> decl,
    std::shared_ptr<DeclarationNode> funcDecl,
    std::shared_ptr<FunctionNode> functionInstance) {
  if (!decl) {
    return typesMap[""];
  }
  if (decl->getEntityType() == "switch" || decl->getEntityType() == "trigger") {
    return typesMap[DataTypes::Switch];
  }
  if (decl->getEntityType() == "string") {
    return typesMap[DataTypes::String];
  }
  if (decl->getEntityType() == "constant") {
    return getLLVMType(decl);
  }
  auto typePropNode = decl->getPropertyValue("type");
  std::string type = DataTypes::Real;
  if (typePropNode) {
    if (typePropNode->getNodeType() == strd::AST::Entity) {
      type =
          std::static_pointer_cast<strd::EntityNode>(typePropNode)->getName();
    } else if (typePropNode->getNodeType() == strd::AST::MemberAccess) {
      auto typeProp =
          std::static_pointer_cast<strd::MemberAccessNode>(typePropNode);
      if (typeProp->getPropertyName() != "type") {
        LOG_ERROR() << "ERROR invalid port for type for " << decl->toText()
                    << std::endl;
        return typesMap[type];
      }
      if (llvm::Type *resolved =
              resolvePortMemberType(typeProp, funcDecl, functionInstance, 0)) {
        return resolved;
      }
    } else {
      LOG_INFO() << " : unsupported type" << std::endl;
    }
  }
  if (decl->getEntityType() == "reaction") {
    return typesMap[DataTypes::Switch];
  }
  if (!type.empty() && typesMap.find(type) != typesMap.end() &&
      !typesMap[type]->isVoidTy()) {
    return typesMap[type];
  }
  return typesMap[DataTypes::Real];
}

// Port types can chain through several functions (a >> Abs() >> Abs() >> b).
// Bounds the recursion if declarations are cyclic.
static constexpr int kMaxPortTypeDepth = 32;

llvm::Type *StrideCompiler::resolvePortMemberType(
    std::shared_ptr<MemberAccessNode> typeProp,
    std::shared_ptr<DeclarationNode> funcDecl,
    std::shared_ptr<FunctionNode> instance, int depth) {
  if (!typeProp || !funcDecl || !instance || depth > kMaxPortTypeDepth) {
    return nullptr;
  }
  auto outputPort = ASTQuery::getModuleMainOutputPort(funcDecl);
  if (outputPort && outputPort->getName() == typeProp->getEntity()) {
    return getConnectedNodeType(instance->getCompilerProperty("outputBlock"),
                                true, depth + 1);
  }
  auto inputPort = ASTQuery::getModuleMainInputPort(funcDecl);
  if (inputPort && inputPort->getName() == typeProp->getEntity()) {
    return getConnectedNodeType(instance->getCompilerProperty("inputBlock"),
                                false, depth + 1);
  }
  return nullptr;
}

llvm::Type *
StrideCompiler::getPortBlockType(std::shared_ptr<DeclarationNode> portDecl,
                                 std::shared_ptr<DeclarationNode> funcDecl,
                                 std::shared_ptr<FunctionNode> instance,
                                 int depth) {
  if (!portDecl || !funcDecl || depth > kMaxPortTypeDepth) {
    return nullptr;
  }
  auto blocks = funcDecl->getPropertyValue("blocks");
  if (!blocks) {
    return nullptr;
  }
  std::string blockName = ASTQuery::getNodeName(portDecl->getPropertyValue("block"));
  for (const auto &node : blocks->getChildren()) {
    if (node->getNodeType() != AST::Declaration &&
        node->getNodeType() != AST::ArrayDeclaration) {
      continue;
    }
    auto blockDecl = std::static_pointer_cast<DeclarationNode>(node);
    if (blockDecl->getName() != blockName) {
      continue;
    }
    auto typeProp = blockDecl->getPropertyValue("type");
    if (typeProp && typeProp->getNodeType() == AST::MemberAccess) {
      return resolvePortMemberType(
          std::static_pointer_cast<MemberAccessNode>(typeProp), funcDecl,
          instance, depth + 1);
    }
    return getLLVMType(blockDecl);
  }
  return nullptr;
}

std::shared_ptr<DeclarationNode>
StrideCompiler::findConnectedDeclaration(ASTNode node) const {
  auto decl = std::static_pointer_cast<DeclarationNode>(
      node->getCompilerProperty("declaration"));
  // The resolver does not attach declarations to every stream node (e.g. in a
  // state's updateGuard), so look them up in the instance's scope.
  if (!decl && !connectionScopes.empty()) {
    const auto &context = connectionScopes.back();
    if (context.scope) {
      decl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node),
                                             *context.scope, context.tree);
    }
  }
  if (decl && decl->getEntityType() == "platformModule") {
    return nullptr;
  }
  return decl;
}

llvm::Type *StrideCompiler::getConnectedNodeType(ASTNode node,
                                                 bool isDownstream, int depth) {
  if (!node || depth > kMaxPortTypeDepth) {
    return nullptr;
  }
  switch (node->getNodeType()) {
  case AST::Int:
    return typesMap[DataTypes::Int];
  case AST::Real:
    return typesMap[DataTypes::Real];
  case AST::Switch:
    return typesMap[DataTypes::Switch];
  case AST::String:
    return typesMap[DataTypes::String];
  case AST::Entity:
  case AST::Array: {
    auto decl = findConnectedDeclaration(node);
    return decl ? getLLVMType(decl) : nullptr;
  }
  case AST::Function: {
    auto decl = findConnectedDeclaration(node);
    if (!decl) {
      return nullptr;
    }
    auto port = isDownstream ? ASTQuery::getModuleMainInputPort(decl)
                             : ASTQuery::getModuleMainOutputPort(decl);
    return getPortBlockType(port, decl,
                            std::static_pointer_cast<FunctionNode>(node),
                            depth + 1);
  }
  case AST::List: {
    // Elements of a list feed one array, so a real element makes it real.
    llvm::Type *result = nullptr;
    for (const auto &child : node->getChildren()) {
      llvm::Type *elemType = getConnectedNodeType(child, isDownstream, depth + 1);
      if (!elemType) {
        return nullptr;
      }
      if (!result || elemType->isFloatingPointTy()) {
        result = elemType;
      }
    }
    return result;
  }
  default:
    return nullptr;
  }
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

const std::vector<StrideCompiler::DynamicDomainField> *
StrideCompiler::findDynamicDomainFields(ASTNode domainDecl) const {
  if (!domainDecl) {
    return nullptr;
  }
  auto it = dynamicDomainFields.find(domainDecl);
  if (it != dynamicDomainFields.end()) {
    return &it->second;
  }
  // Fallback by name (instance pointers can differ between tree passes)
  std::string name = ASTQuery::getNodeName(domainDecl);
  for (const auto &pair : dynamicDomainFields) {
    if (pair.first && ASTQuery::getNodeName(pair.first) == name) {
      return &pair.second;
    }
  }
  return nullptr;
}

bool StrideCompiler::doesDomainNeedState(ASTNode domainDecl) const {
  const auto *fields = findDynamicDomainFields(domainDecl);
  return fields && !fields->empty();
}

llvm::Constant *StrideCompiler::makeDefaultConstant(ASTNode defaultNode,
                                                    llvm::Type *varType) {
  if (!defaultNode || !varType) {
    return nullptr;
  }
  llvm::Type *scalarType =
      varType->isArrayTy() ? varType->getArrayElementType() : varType;

  auto getScalarConst = [&](ASTNode valNode) -> llvm::Constant * {
    if (!valNode) {
      return nullptr;
    }
    if (valNode->getNodeType() == AST::Int) {
      int64_t val = std::static_pointer_cast<ValueNode>(valNode)->getIntValue();
      if (scalarType->isFloatingPointTy()) {
        return llvm::ConstantFP::get(scalarType, static_cast<double>(val));
      } else if (scalarType->isIntegerTy(64)) {
        return llvm::ConstantInt::get(scalarType, val);
      } else if (scalarType->isIntegerTy(32)) {
        return llvm::ConstantInt::get(scalarType, static_cast<int32_t>(val));
      } else if (scalarType->isIntegerTy(1)) {
        return llvm::ConstantInt::get(scalarType, val != 0 ? 1 : 0);
      } else if (scalarType->isIntegerTy()) {
        return llvm::ConstantInt::get(scalarType, val);
      }
    } else if (valNode->getNodeType() == AST::Real) {
      double val = std::static_pointer_cast<ValueNode>(valNode)->getRealValue();
      if (scalarType->isFloatingPointTy()) {
        return llvm::ConstantFP::get(scalarType, val);
      } else if (scalarType->isIntegerTy(1)) {
        return llvm::ConstantInt::get(scalarType, val != 0.0 ? 1 : 0);
      } else if (scalarType->isIntegerTy(32)) {
        return llvm::ConstantInt::get(scalarType, static_cast<int32_t>(val));
      } else if (scalarType->isIntegerTy()) {
        return llvm::ConstantInt::get(scalarType, static_cast<int64_t>(val));
      }
    } else if (valNode->getNodeType() == AST::Switch) {
      bool val = std::static_pointer_cast<ValueNode>(valNode)->getSwitchValue();
      if (scalarType->isFloatingPointTy()) {
        return llvm::ConstantFP::get(scalarType, val ? 1.0 : 0.0);
      } else if (scalarType->isIntegerTy()) {
        return llvm::ConstantInt::get(scalarType, val ? 1 : 0);
      }
    } else if (valNode->getNodeType() == AST::String) {
      if (scalarType->isStructTy()) {
        std::string val =
            std::static_pointer_cast<ValueNode>(valNode)->getStringValue();
        llvm::Constant *strPtr =
            llvm::cast<llvm::Constant>(Builder->CreateGlobalStringPtr(
                val, "str_lit", 0, TheModule.get()));
        llvm::Constant *lenVal =
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext),
                                   static_cast<int32_t>(val.size()));
        return llvm::ConstantStruct::get(
            llvm::cast<llvm::StructType>(scalarType), {strPtr, lenVal});
      }
    }
    return nullptr;
  };

  if (varType->isArrayTy()) {
    auto *arrayType = llvm::cast<llvm::ArrayType>(varType);
    uint64_t numElems = arrayType->getNumElements();
    if (defaultNode->getNodeType() == AST::List) {
      auto children = defaultNode->getChildren();
      if (children.size() != numElems) {
        return nullptr;
      }
      std::vector<llvm::Constant *> initVals;
      initVals.reserve(numElems);
      for (const auto &child : children) {
        llvm::Constant *c = getScalarConst(child);
        initVals.push_back(c ? c : llvm::Constant::getNullValue(scalarType));
      }
      return llvm::ConstantArray::get(arrayType, initVals);
    }
    if (llvm::Constant *scalar = getScalarConst(defaultNode)) {
      std::vector<llvm::Constant *> initVals(numElems, scalar);
      return llvm::ConstantArray::get(arrayType, initVals);
    }
    return nullptr;
  }
  return getScalarConst(defaultNode);
}

bool StrideCompiler::doesNodeNeedState(const CodeAnalysis::TypeTree *node) {
  if (!node) {
    return false;
  }
  // If it has dynamically added domain fields, it needs state!
  if (doesDomainNeedState(node->instance)) {
    return true;
  }
  // Only modules can have persistent state of their own, and only if they have
  // persistent variables. Reactions and loops do not have state of their own.
  if (ASTQuery::isModuleNode(node->instance, {}, m_tree) &&
      !node->persistent.empty()) {
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
    if (pair.second.structType) {
      std::string stName = pair.second.structType->getName().str();
      if (stName == "struct." + name + "_state" || stName == name ||
          stName.find(name) != std::string::npos) {
        return &pair.second;
      }
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

llvm::Constant *StrideCompiler::StateStructInfo::getDefaultValue(
    const std::string &varName) const {
  auto it = varIndices.find(varName);
  if (it != varIndices.end() && defaultConstant) {
    return llvm::cast<llvm::Constant>(defaultConstant->getOperand(it->second));
  }
  return nullptr;
}

llvm::Constant *StrideCompiler::StateStructInfo::getChildDefaultConstant(
    ASTNode childInstance) const {
  auto it = childIndices.find(childInstance);
  if (it != childIndices.end() && defaultConstant) {
    return llvm::cast<llvm::Constant>(defaultConstant->getOperand(it->second));
  }
  return nullptr;
}

void StrideCompiler::buildNodeState(const CodeAnalysis::TypeTree &nodeTree) {
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

  // 1. Persistent fields for this node's own state (only modules have
  // state of their own)
  if (ASTQuery::isModuleNode(nodeTree.instance, {}, m_tree)) {
    for (const auto &var : nodeTree.persistent) {
      std::string varName = ASTQuery::getNodeName(var.first);
      llvm::Type *varType = nullptr;
      if (!var.second.empty() && typesMap.find(var.second) != typesMap.end() &&
          !typesMap[var.second]->isVoidTy()) {
        varType = typesMap[var.second];
      } else if (var.first &&
                 (var.first->getNodeType() == AST::Declaration ||
                  var.first->getNodeType() == AST::ArrayDeclaration)) {
        varType =
            getLLVMType(std::static_pointer_cast<DeclarationNode>(var.first));
      }
      if (!varType || varType->isVoidTy()) {
        varType = llvm::Type::getDoubleTy(*TheContext);
      }
      info.varIndices[varName] = static_cast<unsigned>(fieldTypes.size());
      fieldTypes.push_back(varType);

      llvm::Constant *defaultVal = nullptr;
      if (var.first->getNodeType() == AST::Declaration ||
          var.first->getNodeType() == AST::ArrayDeclaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(var.first);
        defaultVal =
            makeDefaultConstant(decl->getPropertyValue("default"), varType);
      }
      if (!defaultVal) {
        defaultVal = llvm::Constant::getNullValue(varType);
      }
      defaultFieldValues.push_back(defaultVal);
    }
  }

  // 1.5 Dynamic Domain Fields
  if (const auto *dynFields = findDynamicDomainFields(nodeTree.instance)) {
    for (const auto &field : *dynFields) {
      info.varIndices[field.name] = static_cast<unsigned>(fieldTypes.size());
      fieldTypes.push_back(field.type);
      defaultFieldValues.push_back(field.defaultVal);
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
      if (childIt != stateStructMap.end() && childIt->second.structType &&
          childIt->second.structType->isSized()) {
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
    std::string name =
        nodeTree.instance ? ASTQuery::getNodeName(nodeTree.instance) : "anon";

    // Validate all fieldTypes are valid and sized
    for (size_t i = 0; i < fieldTypes.size(); ++i) {
      if (!fieldTypes[i] || fieldTypes[i]->isVoidTy() ||
          !fieldTypes[i]->isSized()) {
        LOG_ERROR() << "buildNodeState: fieldType at index " << i
                    << " in struct." << name
                    << "_state is invalid or unsized! Type ptr: "
                    << fieldTypes[i] << " Replacing with double." << std::endl;
        fieldTypes[i] = llvm::Type::getDoubleTy(*TheContext);
        defaultFieldValues[i] = llvm::Constant::getNullValue(fieldTypes[i]);
      }
    }

    info.structType =
        llvm::StructType::create(*TheContext, "struct." + name + "_state");
    info.structType->setBody(fieldTypes);

    info.defaultConstant =
        llvm::ConstantStruct::get(info.structType, defaultFieldValues);

    const auto &DL = TheModule->getDataLayout();
    if (info.structType->isSized()) {
      const auto *structLayout = DL.getStructLayout(info.structType);
      info.allocSize = DL.getTypeAllocSize(info.structType);
      info.fieldOffsets.resize(fieldTypes.size());
      info.fieldAllocSizes.resize(fieldTypes.size());
      for (size_t i = 0; i < fieldTypes.size(); ++i) {
        info.fieldOffsets[i] =
            structLayout->getElementOffset(static_cast<unsigned>(i));
        info.fieldAllocSizes[i] = DL.getTypeAllocSize(fieldTypes[i]);
      }
    } else {
      LOG_ERROR() << "buildNodeState: struct." << name
                  << "_state is unsized! Calculating fallback field offsets."
                  << std::endl;
      uint64_t currentOffset = 0;
      info.fieldOffsets.resize(fieldTypes.size());
      info.fieldAllocSizes.resize(fieldTypes.size());
      for (size_t i = 0; i < fieldTypes.size(); ++i) {
        uint64_t sz =
            fieldTypes[i]->isSized() ? DL.getTypeAllocSize(fieldTypes[i]) : 8;
        info.fieldOffsets[i] = currentOffset;
        info.fieldAllocSizes[i] = sz;
        currentOffset += sz;
      }
      info.allocSize = currentOffset;
    }

    stateStructMap[nodeTree.instance] = info;
    if (debugEmitter && debugEmitter->isInitialized()) {
      std::vector<std::pair<std::string, llvm::DIType *>> debugFields;
      for (const auto &[varName, idx] : info.varIndices) {
        if (idx < fieldTypes.size()) {
          debugFields.push_back(
              {varName, debugEmitter->getOrCreateTypeForLLVMType(fieldTypes[idx])});
        }
      }
      debugEmitter->createStateStructType("struct." + name + "_state",
                                          debugFields, info.allocSize);
    }
  }
}

void StrideCompiler::buildStateStructTypes(const CodeAnalysis::TypeTree &tree,
                                           ScopeStack &scope, ASTNode root) {
  if (tree.instance &&
      (tree.instance->getNodeType() == AST::Declaration ||
       tree.instance->getNodeType() == AST::ArrayDeclaration)) {
    auto decl = std::static_pointer_cast<DeclarationNode>(tree.instance);
    if (!ASTQuery::isDomainDefinition(
            ASTQuery::findTypeDeclaration(decl, ScopeStack(), root), scope,
            root)) {
      buildNodeState(tree);
      return;
    }
  } else if (tree.instance) {
    buildNodeState(tree);
    return;
  }

  for (const auto &node : tree.nodes) {
    if (node.instance &&
        (node.instance->getNodeType() == AST::Declaration ||
         node.instance->getNodeType() == AST::ArrayDeclaration)) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node.instance);
      if (ASTQuery::isDomainDefinition(
              ASTQuery::findTypeDeclaration(decl, ScopeStack(), root), scope,
              root)) {
        if (doesDomainNeedState(node.instance)) {
          buildNodeState(node);
        } else {
          for (const auto &funcTree : node.nodes) {
            buildNodeState(funcTree);
          }
        }
        continue;
      }
    }
    buildNodeState(node);
  }
}

void StrideCompiler::initializeDebugInfo(const std::string &primaryRelFile,
                                         const std::string &primaryAbsFile) {
  if (!debugEmitter) {
    debugEmitter = std::make_unique<StrideDebugEmitter>(*TheModule);
  }
  debugEmitter->initialize(primaryRelFile, primaryAbsFile);
  m_debugInfo = true;
}

void StrideCompiler::finalizeDebugInfo() {
  if (debugEmitter) {
    debugEmitter->finalize();
  }
}

