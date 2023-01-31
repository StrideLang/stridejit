#include <filesystem>
#include <fstream>
#include <iostream>

#include "astfunctions.h"
#include "astquery.h"
#include "codeanalysis.hpp"
#include "coderesolver.h"
#include "strideenvironment.hpp"

StrideEnvironment::StrideEnvironment(std::string strideRoot)
    : m_strideRoot(strideRoot) {
  if (m_strideRoot.size() == 0) {
    m_strideRoot = ASTFunctions::getDefaultStrideRoot();
  }
  std::cout << "Using STRIDEROOT = " << m_strideRoot << std::endl;
}

bool StrideEnvironment::generateIr(std::string path) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  ASTNode tree;
  tree = ASTFunctions::parseFile(path.c_str());
  auto systemNodes = ASTQuery::getSystemNodes(tree);
  if (systemNodes.size() == 0) {
    auto systemNode =
        std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__);
    tree->addChild(systemNode);
  }

  CodeResolver resolver(tree, ASTFunctions::getDefaultStrideRoot());
  resolver.process();
  ScopeStack globalScope;
  {
    //    StrideLibrary library;
    //    library.initializeLibrary(m_strideRoot);

    globalScope.push_back({nullptr, {}});
    std::vector<ASTNode> platformlib = ASTFunctions::loadAllInDirectory(
        m_strideRoot + "/frameworks/JIT/1.0/platformlib");
    auto &frameworkScope = globalScope.back().second;

    for (const auto &member : platformlib) {
      if (member->getNodeType() == AST::Declaration ||
          member->getNodeType() == AST::BundleDeclaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(member);
        if (decl->getObjectType() == "platformModule") {
          std::vector<llvm::Type *> parameters;
          llvm::Type *retType = nullptr;
          auto inputList = decl->getPropertyValue("inputs");
          auto outputList = decl->getPropertyValue("outputs");
          auto functionNameNode = decl->getPropertyValue("processing");
          if (inputList && outputList && functionNameNode) {
            std::cout << "Loaded: " << decl->getName() << std::endl;
            frameworkScope.push_back(decl);
            for (const auto &input : inputList->getChildren()) {
              if (input->getNodeType() == AST::Block) {
                auto inputBlock = std::static_pointer_cast<BlockNode>(input);
                auto inputType = inputBlock->getName();
                if (state.typesMap.find(inputType) != state.typesMap.end()) {
                  parameters.push_back(state.typesMap[inputType]);
                } else {
                  std::cerr << "Type not mapped: " << inputType << std::endl;
                }
              }
            }
            assert(outputList->getChildren().size() < 2);
            if (outputList->getChildren().size() != 0) {
              auto outputBlock = outputList->getChildren()[0];
              if (outputBlock->getNodeType() == AST::Block) {
                auto outputType =
                    std::static_pointer_cast<BlockNode>(outputBlock)->getName();
                if (state.typesMap.find(outputType) != state.typesMap.end()) {
                  retType = state.typesMap[outputType];
                } else {
                  std::cerr << " Output Type not mapped: " << outputType
                            << std::endl;
                }
              }
            }
            auto name = std::static_pointer_cast<ValueNode>(functionNameNode)
                            ->getStringValue();
            llvm::FunctionType *FT =
                llvm::FunctionType::get(retType, parameters, false);
            state.functionMap[decl->getName()].push_back(
                ExternalFunction{name, FT});

            std::cout << "Loaded platform module: " << decl->getName()
                      << std::endl;
          }
        }
      }
    }
  }

  if (!ASTFunctions::preprocess(tree, &globalScope)) {
    return false;
  }

  generateCode(tree, &globalScope, state);
  //  if (mVerbose) {
  //    state.TheModule->dump();
  //  }
  if (m_optimizeCode) {
    for (auto &F : *state.TheModule) {
      state.TheFPM->run(F);
    }
  }
  if (mVerbose) {
    state.TheModule->dump();
  }
  return true;
}

bool StrideEnvironment::compileInMemory() {

  auto JTMB = llvm::orc::JITTargetMachineBuilder::detectHost();
  if (!JTMB) {
    std::cerr << " No machine builder" << std::endl;
    return false;
  }
  JTMB->setCodeModel(llvm::CodeModel::Small);

  auto JIT_ =
      llvm::orc::LLJITBuilder()
          .setJITTargetMachineBuilder(std::move(*JTMB))
          //          .setObjectLinkingLayerCreator(
          //              [&](orc::ExecutionSession &ES, const Triple &TT) {
          //                  // Create ObjectLinkingLayer.
          //                  auto ObjLinkingLayer =
          //                  std::make_unique<orc::ObjectLinkingLayer>(
          //                      ES,
          // jitlink::InProcessMemoryManager::Create());
          //                  // Add an instance of our plugin.
          //// ObjLinkingLayer->addPlugin(std::make_unique<MyPlugin>());
          //                  return ObjLinkingLayer;
          //              })
          .create();
  if (!JIT_)
    return false; // JIT.takeError();
  JIT = std::move(*JIT_);
  auto librarySearch =
      llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
          JIT->getDataLayout().getGlobalPrefix());
  if (!librarySearch) {
    return false;
  }
  JIT->getMainJITDylib().addGenerator(std::move(*librarySearch));
  // TODO only load required libraries
  std::string err;
  if (!loadLibrary("m", err)) {
    std::cerr << "Failed to load m: " << err << std::endl;
  }
  if (!loadLibrary("StrideLib", err)) {
    std::cerr << "Failed to load StrideLib: " << err << std::endl;
  }
  if (!llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr, &err)) {
    std::cerr << "Failed to load current symbols: " << err << std::endl;
  }
  //  if (!llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr, &err)) {
  //    std::cerr << "Failed to load current symbols: " << err << std::endl;
  //  }
  JIT->getMainJITDylib().addGenerator(llvm::cantFail(
      llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess('a')));
  llvm::orc::SymbolMap M;
  llvm::orc::MangleAndInterner Mangle(JIT->getExecutionSession(),
                                      JIT->getDataLayout());
  M[Mangle("__stride_Greater_b_dd")] = llvm::JITEvaluatedSymbol(
      llvm::pointerToJITTargetAddress(&__stride_Greater_b_dd),
      llvm::JITSymbolFlags());
  M[Mangle("__stride_Greater_d_dd")] = llvm::JITEvaluatedSymbol(
      llvm::pointerToJITTargetAddress(&__stride_Greater_d_dd),
      llvm::JITSymbolFlags());
  M[Mangle("__stride_Greater_b_ii")] = llvm::JITEvaluatedSymbol(
      llvm::pointerToJITTargetAddress(&__stride_Greater_b_ii),
      llvm::JITSymbolFlags());
  M[Mangle("__stride_Equal_b_dd")] = llvm::JITEvaluatedSymbol(
      llvm::pointerToJITTargetAddress(&__stride_Equal_b_dd),
      llvm::JITSymbolFlags());
  llvm::cantFail(JIT->getMainJITDylib().define(llvm::orc::absoluteSymbols(M)));

  if (auto Err = JIT->addIRModule(llvm::orc::ThreadSafeModule(
          std::move(state.TheModule), std::move(state.TheContext)))) {
    return false;
  }
  return true;
}

#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"

bool StrideEnvironment::compileObjectToDisk(std::string path) {
  auto fspath = std::filesystem::path(path);
  if (!std::filesystem::exists(fspath)) {
    std::filesystem::create_directories(fspath);
  }
  { // Write header file
    auto headerFile = fspath.append("stride_include.h");
    std::ofstream f(headerFile.string());
    if (!f.good()) {
      std::cerr << "Could not create header." << std::endl;
      return false;
    }
    f << "// Auto generated by stridejit. Do not modify" << std::endl;
    f << "#pragma once" << std::endl << std::endl;
    for (const auto &domain : state.domainArgs) {
      f << "int"; // Return type
      f << " " << domain.first << "_process(";
      for (const auto &domainFunc : domain.second) {
        if (&domainFunc != &domain.second.front()) {
          f << ", ";
        }
        if (domainFunc.type == DataType::DOUBLE) {
          f << "double *";
        } else if (domainFunc.type == DataType::INT32) {
          f << "int *";
        } else if (domainFunc.type == DataType::BOOL) {
          f << "bool *";
          //        } else if (domainFunc.type == DataType::INT64) {
          //          f << "int64_t *";
        } else {
          f << "INVALID";
        }
        f << " " << domainFunc.name;
      }
      f << ");" << std::endl << std::endl;
    }
  }

  LLVMInitializeAArch64TargetInfo();
  LLVMInitializeX86TargetInfo();
  LLVMInitializeARMTargetInfo();
  LLVMInitializeWebAssemblyTargetInfo();

  LLVMInitializeAArch64Target();
  LLVMInitializeX86Target();
  LLVMInitializeARMTarget();
  LLVMInitializeWebAssemblyTarget();

  LLVMInitializeAArch64TargetMC();
  LLVMInitializeX86TargetMC();
  LLVMInitializeARMTargetMC();
  LLVMInitializeWebAssemblyTargetMC();

  LLVMInitializeAArch64AsmParser();
  LLVMInitializeX86AsmParser();
  LLVMInitializeARMAsmParser();
  LLVMInitializeWebAssemblyAsmParser();

  LLVMInitializeAArch64AsmPrinter();
  LLVMInitializeX86AsmPrinter();
  LLVMInitializeARMAsmPrinter();
  LLVMInitializeWebAssemblyAsmPrinter();

  //  llvm::InitializeAllTargetInfos();
  //  llvm::InitializeAllTargets();
  //  llvm::InitializeAllTargetMCs();
  //  llvm::InitializeAllAsmParsers();
  //  llvm::InitializeAllAsmPrinters();
  std::string stbuf;
  llvm::raw_string_ostream sstr(stbuf);
  llvm::TargetRegistry::printRegisteredTargetsForVersion(sstr);
  std::cout << stbuf << std::endl;
  for (auto target : llvm::TargetRegistry::targets()) {

    llvm::Triple TargetTriple;
    TargetTriple.setArchName(target.getName());
    TargetTriple.setVendorName("PC");
    TargetTriple.setOSName("Linux");
    TargetTriple.setEnvironmentName("GNU");

    std::string Error;
    auto Target = llvm::TargetRegistry::lookupTarget(target.getName(),
                                                     TargetTriple, Error);
    if (Target) {
      std::cout << Target->getName() << "   " << TargetTriple.getTriple()
                << std::endl;
    }
  }

  auto TargetTriple = llvm::sys::getDefaultTargetTriple();

  if (!generateCompiledObject(path, TargetTriple)) {
    std::cerr << "Error creating object file" << std::endl;
    return false;
  }

  TargetTriple = "wasm32-wasi";
  if (!generateCompiledObject(path, TargetTriple)) {
    std::cerr << "Error creating object file" << std::endl;
    return false;
  }

  TargetTriple = "arm-none-eabi";
  if (!generateCompiledObject(path, TargetTriple)) {
    std::cerr << "Error creating object file" << std::endl;
    return false;
  }

  return true;
}

bool StrideEnvironment::generateCompiledObject(std::string path,
                                               std::string TargetTriple) {
  auto CPU = "generic";
  auto Features = "";
  std::string Error;
  auto Target = llvm::TargetRegistry::lookupTarget(TargetTriple, Error);
  // Print an error and exit if we couldn't find the requested target.
  // This generally occurs if we've forgotten to initialise the
  // TargetRegistry or we have a bogus target triple.
  if (!Target) {
    std::cerr << Error;
    return false;
  }
  llvm::TargetOptions opt;
  auto RM = llvm::Optional<llvm::Reloc::Model>();
  auto TargetMachine =
      Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);
  state.TheModule->setDataLayout(TargetMachine->createDataLayout());
  state.TheModule->setTargetTriple(TargetTriple);

  auto fspath = std::filesystem::path(path);
  fspath.append(TargetTriple + "/");
  std::filesystem::create_directories(fspath);
  fspath.replace_filename("output.o");
  std::string Filename = fspath.string();
  std::error_code EC;
  llvm::raw_fd_ostream dest(Filename.c_str(), EC, llvm::sys::fs::OF_None);

  if (EC) {
    std::cerr << "Could not open file: " << EC.message();
    return false;
  }
  llvm::legacy::PassManager pass;
  auto FileType = llvm::CGFT_ObjectFile;

  if (TargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    std::cerr << "TargetMachine can't emit a file of this type";
    return false;
  }

  pass.run(*state.TheModule);
  dest.flush();
  return true;
}

bool StrideEnvironment::loadLibrary(const char *libName, std::string &err) {
  // TODO Do a proper comprehensive search for libs and fix for other systems
  // On windows, just using the librery name seems to work, no need to add
  // path.
  std::vector<std::string> libPath = {"/lib/x86_64-linux-gnu/",
                                      "/usr/lib/x86_64-linux-gnu/"};
  for (const auto &path : libPath) {
#ifdef WIN32
    std::string libToLoad = libName;
#else
    //    auto libToLoad = path + "/lib" + libName + ".so";
    //    if (!std::filesystem::exists(libToLoad)) {
    //      continue;
    //    }
    //    if (std::filesystem::is_symlink(libToLoad)) {
    //      libToLoad = std::filesystem::read_symlink(libToLoad).string();
    //    }

    std::string libToLoad = libName;
    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(libToLoad.c_str(),
                                                          &err)) {
      std::cout << " Loaded lib: " << libToLoad << std::endl;
      return true;
    }
    std::cout << " Trying: " << libToLoad << std::endl;
#endif
    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(libToLoad.c_str(),
                                                          &err)) {
      std::cout << " Loaded lib: " << libToLoad << std::endl;
      return true;
    }
  }
  return false;
}

///////// -----------------------

#include "exprast.hpp"
#include "functionast.hpp"
#include "listexprast.hpp"
#include "numberexprast.hpp"

#include "astquery.h"

//#include "llvm/ExecutionEngine/ExecutionEngine.h"
//#include "llvm/ExecutionEngine/GenericValue.h"
//#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

void setTypeCastMetadata(ASTNode node, ExprAST *V) {
  if (auto typecastNode = node->getCompilerProperty("typecast")) {
    if (typecastNode->getNodeType() == AST::String) {
      V->typecast =
          std::static_pointer_cast<ValueNode>(typecastNode)->getStringValue();
    }
  }
}

std::unique_ptr<ExprAST> createExpr(ASTNode node) {
  if (node->getNodeType() == AST::Block) {
    std::unique_ptr<ExprAST> V = std::make_unique<VariableExprAST>(
        std::static_pointer_cast<BlockNode>(node)->getName());
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Bundle) {
    std::vector<size_t> indeces;
    auto bundleNode = std::static_pointer_cast<BundleNode>(node);
    auto V = std::make_unique<VariableExprAST>(bundleNode->getName(),
                                               bundleNode->getIndeces());
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Real) {
    // TODO separate float types
    auto V = std::make_unique<RealExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getRealValue());
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Int) {
    // TODO separate int types
    auto V = std::make_unique<IntExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getIntValue());
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Switch) {
    // TODO separate int types
    auto V = std::make_unique<BoolExprAST>(
        std::static_pointer_cast<ValueNode>(node)->getSwitchValue());
    setTypeCastMetadata(node, V.get());
    return V;
  } else if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->isUnary()) {
      assert(0 == 1);

    } else {
      auto l = createExpr(expr->getLeft());
      auto r = createExpr(expr->getRight());
      char op;
      if (expr->getExpressionType() == ExpressionNode::Multiply) {

        op = '*';
      } else if (expr->getExpressionType() == ExpressionNode::Divide) {

        op = '/';
      } else if (expr->getExpressionType() == ExpressionNode::Add) {

        op = '+';
      } else if (expr->getExpressionType() == ExpressionNode::Subtract) {

        op = '-';
      } else {
        assert(0 == 1);
        return nullptr;
      }

      auto V = std::make_unique<BinaryExprAST>(op, std::move(l), std::move(r));
      setTypeCastMetadata(node, V.get());
      return V;
    }
  } else if (node->getNodeType() == AST::List) {
    auto list = std::make_unique<ListExprAST>(node->getChildren());
    return list;
  } else if (node->getNodeType() == AST::PortProperty) {
    auto pp = std::static_pointer_cast<PortPropertyNode>(node);
    auto V =
        std::make_unique<PortPropertyAST>(pp->getName(), pp->getPortName());
    setTypeCastMetadata(node, V.get());
    return V;
  }
  std::cerr << " Node type not supported " << std::endl;
  return nullptr;
}

struct FunctionMapEntry {
  std::string name;
  std::vector<llvm::Type *> args;
};

GeneratedCode createStreamCode(std::shared_ptr<StreamNode> stream, ASTNode tree,
                               ScopeStack *scope, StrideCompiler &state) {
  GeneratedCode generated;
  ASTNode prev = nullptr;
  ASTNode next;
  ASTNode current;
  std::shared_ptr<StreamNode> inputStream = stream;

  do {
    if (stream && stream->getNodeType() == AST::Stream) {
      current = stream->getLeft();

      if (stream->getRight()->getNodeType() == AST::Stream) {
        stream = std::static_pointer_cast<StreamNode>(stream->getRight());
        next = stream->getLeft();
      } else {
        next = stream->getRight();
        stream = nullptr;
      }
    } else {
      next = nullptr;
      stream = nullptr;
    }
    auto domainName = CodeAnalysis::getNodeDomainName(current, *scope, tree);
    if (domainName.size() == 0) {
      auto streamPtr = stream;
      // FIXME resolve downstream domains correctly
      auto connection =
          CodeAnalysis::resolveConnectionBlock(next, *scope, tree, true);
      if (connection) {
        domainName = CodeAnalysis::getNodeDomainName(connection, *scope, tree);
      } else {
        domainName = "RootDomain";
      }
    }
    if (current->getNodeType() == AST::Expression) {
      assert(prev == nullptr);
      generated[domainName].expr.push_back(createExpr(current));
    } else if (current->getNodeType() == AST::Block ||
               current->getNodeType() == AST::Bundle) {
      auto block = createExpr(current);
      // TODO accumulate read and write variables for switch, expressions,
      // lists and function arguments
      if (ASTQuery::findDeclarationByName(ASTQuery::getNodeName(current),
                                          *scope, tree)) {
        // If on root namespace
        if (current == inputStream->getLeft()) {
          generated[domainName].readVariables.push_back(current);
        } else {
          generated[domainName].writeVariables.push_back(current);
          if (!next) {
            // TODO needed?
            generated[domainName].readVariables.push_back(current);
          }
        }
      }
      if (prev && prev->getNodeType() == AST::Function) {
        auto prevFunc = std::static_pointer_cast<FunctionNode>(prev);
        auto decl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(current), *scope, tree);
        if (!decl) {
          std::cerr << "No declaration for: " << ASTQuery::getNodeName(current)
                    << " in " << AST::toText(inputStream) << " in "
                    << inputStream->getFilename() << ":"
                    << inputStream->getLine() << std::endl;
          return generated;
        }
        auto retType = state.getLLVMType(decl);
        auto callexpr =
            static_cast<CallExprAST *>(generated[domainName].expr.back().get());
        assert(dynamic_cast<CallExprAST *>(
            generated[domainName].expr.back().get()));
        std::vector<llvm::Type *> argTypes;
        for (auto it = callexpr->OutArgs.begin(); it != callexpr->OutArgs.end();
             it++) {
          argTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
        }
        for (auto it = callexpr->InArgs.begin(); it != callexpr->InArgs.end();
             it++) {
          argTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
        }
        //        if (callexpr->isExternal) {
        //          assert(argTypes.size() > 0);
        //          argTypes.resize(argTypes.size() - 1);
        //        }

        // TODO do we also test that the function is not declared in Stride
        // code? Or is this check enough?
        auto externFunc =
            state.getExternalFunction(prevFunc->getName(), retType, argTypes);
        if (externFunc) {
          generated[domainName].expr.back() = std::make_unique<BinaryExprAST>(
              '=', std::move(block),
              std::move(generated[domainName].expr.back()));
        } else {
          // Stride Functions pass the output as arguments, so no need to
          // assign
        }
      } else if (generated[domainName].expr.size() > 0) {
        generated[domainName].expr.back() = std::make_unique<BinaryExprAST>(
            '=', std::move(block),
            std::move(generated[domainName].expr.back()));
      } else {
        generated[domainName].expr.push_back(std::move(block));
      }

    } else if (current->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(current);
      auto funcDecl =
          ASTQuery::findDeclarationByName(func->getName(), {}, tree);

      std::vector<std::unique_ptr<ExprAST>> mainInArgs;
      std::vector<llvm::Type *> mainInArgTypes;
      std::vector<std::unique_ptr<ExprAST>> mainOutArgs;
      std::vector<llvm::Type *> mainOutArgTypes;
      //      std::unique_ptr<ExprAST> ret;
      llvm::Type *retType;
      if (prev) { // inputs
        if (auto prevFunc = std::dynamic_pointer_cast<FunctionNode>(prev)) {
          mainInArgs.emplace_back(std::move(generated[domainName].expr.back()));
          generated[domainName].expr.pop_back();
          // FIXME do automatic type casting int ->float
          mainInArgTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
        } else if (auto prevList = std::dynamic_pointer_cast<ListNode>(prev)) {
          auto *v = dynamic_cast<ListExprAST *>(
              generated[domainName].expr.back().get());
          auto listNodes = prevList->getChildren();
          auto nodeIt = listNodes.begin();
          if (v->getType() == ListExprAST::Type::MUTABLE_CONSISTENT) {
            for (auto elem = v->elements().begin(); elem != v->elements().end();
                 elem++) {
              mainInArgs.emplace_back(std::move(*elem));
              auto elemDecl = ASTQuery::findDeclarationByName(
                  ASTQuery::getNodeName(*nodeIt), *scope, tree);
              if (elemDecl) {
                mainInArgTypes.push_back(state.getLLVMType(elemDecl));
              } else if ((*nodeIt)->getNodeType() == AST::Int) {
                mainInArgTypes.push_back(
                    llvm::Type::getInt32Ty(*state.TheContext));

              } else if ((*nodeIt)->getNodeType() == AST::Real) {
                mainInArgTypes.push_back(
                    llvm::Type::getDoubleTy(*state.TheContext));
              } else {
                // Fallback. We shouldn't get here when things are fully
                // implemented
                mainInArgTypes.push_back(
                    llvm::Type::getDoubleTy(*state.TheContext));
              }

              nodeIt++;
            }
          } else if (v->getType() == ListExprAST::Type::IMMUTABLE_CONSISTENT) {
            mainInArgs.emplace_back(
                std::move(generated[domainName].expr.back()));
            mainInArgTypes.push_back(llvm::PointerType::get(
                llvm::Type::getDoubleTy(*state.TheContext), 0));
          } else {
            assert(0 == 1);
          }
          generated[domainName].expr.pop_back();

        } else {
          mainInArgs.emplace_back(std::move(generated[domainName].expr.back()));
          generated[domainName].expr.pop_back();
          // FIXME set correct type
          mainInArgTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
        }
      }

      std::optional<ExternalFunction> externFunc;
      if (next) { // outputs
        // FIXME: ensure next node is processed correctly. This will not
        // work for many cases, e.g. if next is a function
        auto nextExpr = createExpr(next);
        std::shared_ptr<DeclarationNode> nextDecl =
            ASTQuery::findDeclarationByName(ASTQuery::getNodeName(next), *scope,
                                            tree);
        externFunc = state.getExternalFunction(
            func->getName(), state.getLLVMType(nextDecl), mainInArgTypes);
        if (!externFunc) {
          assert(nextExpr);
          mainOutArgs.emplace_back(std::move(nextExpr));
          mainOutArgTypes.push_back(llvm::Type::getDoubleTy(*state.TheContext));
        }
      } else {
        externFunc = state.getExternalFunction(
            func->getName(), state.getLLVMType(nullptr), mainInArgTypes);
      }

      if (externFunc) {
        if (!state.TheModule->getFunction(externFunc->name)) {
          auto *newFunc = llvm::Function::Create(
              externFunc->llvmFunctionType, llvm::Function::ExternalLinkage,
              externFunc->name, *state.TheModule);
          generated[domainName].externalFunctions.push_back(newFunc);
        }
        auto newCall = std::make_unique<CallExprAST>(
            externFunc->name, std::move(mainOutArgs), std::move(mainInArgs),
            std::vector<std::unique_ptr<ExprAST>>{},
            std::vector<std::unique_ptr<ExprAST>>{});
        newCall->callType = CallableType::External;
        generated[domainName].expr.push_back(std::move(newCall));
        std::cerr << " Using external function:" << externFunc->name
                  << std::endl;
      } else {
        auto newFuncDecl =
            createFunctionDeclaration(func, prev, next, tree, scope, state);
        std::vector<std::unique_ptr<ExprAST>> ExternalArgs;
        if (newFuncDecl) {
          for (const auto &arg : newFuncDecl->getProto().getExternalArgs()) {
            ExternalArgs.push_back(std::make_unique<VariableExprAST>(arg.name));
          }
        }
        std::vector<std::unique_ptr<ExprAST>> PortPropArgs;
        if (funcDecl) {
          auto usedPortProps = CodeAnalysis::getUsedPortProperties(funcDecl);
          auto innerScope = *scope;
          std::vector<ASTNode> blocks;
          auto blocksNode = funcDecl->getPropertyValue("blocks");
          if (blocksNode) {
            blocks = blocksNode->getChildren();
          }
          innerScope.push_back({funcDecl, blocks});
          for (const auto &ppNode : usedPortProps) {
            if (ppNode->getPortName() == "size") {
              auto size = CodeAnalysis::evaluateSizePortProperty(
                  ppNode->getName(), innerScope, funcDecl, func, tree);
              PortPropArgs.push_back(std::make_unique<IntExprAST>(size));
            } else if (ppNode->getPortName() == "rate") {
              auto rate = CodeAnalysis::evaluateRatePortProperty(
                  ppNode->getName(), innerScope, funcDecl, func, tree);
              PortPropArgs.push_back(std::make_unique<RealExprAST>(rate));
            }
          }
        }
        auto callexpr = std::make_unique<CallExprAST>(
            std::string(func->getName()), std::move(mainOutArgs),
            std::move(mainInArgs), std::move(ExternalArgs),
            std::move(PortPropArgs));
        if (newFuncDecl) {
          callexpr->callType = newFuncDecl->callType;
          generated[domainName].functions.push_back(std::move(newFuncDecl));
        } else {
          // Function already declared
          if (funcDecl) {
            if (funcDecl->getObjectType() == "module") {
              callexpr->callType = CallableType::Module;
            } else if (funcDecl->getObjectType() == "reaction") {
              callexpr->callType = CallableType::Reaction;
            } else if (funcDecl->getObjectType() == "loop") {
              callexpr->callType = CallableType::Loop;
            } else {
              std::cout << " ERROR: Can't set callable type" << std::endl;
            }
          } else {
            std::cout << " ERROR: Can't set callable type" << std::endl;
          }
        }

        generated[domainName].expr.push_back(std::move(callexpr));
      }
    } else if (current->getNodeType() == AST::Int ||
               current->getNodeType() == AST::Real ||
               current->getNodeType() == AST::String) {
      generated[domainName].expr.push_back(createExpr(current));
    } else if (current->getNodeType() == AST::List) {
      generated[domainName].expr.push_back(createExpr(current));
    } else if (current->getNodeType() == AST::Switch) {
      generated[domainName].expr.push_back(createExpr(current));
    } else if (current->getNodeType() == AST::PortProperty) {
      generated[domainName].expr.push_back(createExpr(current));
    } else {
      std::cerr << "ERROR: Unsupported type" << std::endl;
    }
    prev = current;
    current = next;
  } while (current);
  return generated;
}

std::unique_ptr<FunctionAST>
createFunctionDeclaration(std::shared_ptr<FunctionNode> func, ASTNode prev,
                          ASTNode next, ASTNode tree, ScopeStack *scope,
                          StrideCompiler &state) {

  auto funcDecl = ASTQuery::findDeclarationByName(func->getName(), {}, tree);
  if (!funcDecl) {
    return nullptr;
  }
  std::vector<PrototypeArg> Args;
  std::vector<PrototypeArg> OutArgs;
  std::vector<PrototypeArg> ExternalArgs;
  std::vector<PrototypeArg> UsedPortProperties;

  auto portsNode = funcDecl->getPropertyValue("ports");
  std::string portBlockName;
  for (const auto &node : portsNode->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getObjectType() == "mainInputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        auto type = llvm::Type::getDoubleTy(*state.TheContext);
        auto blockDecl =
            ASTQuery::findDeclarationByName(portBlockName, {}, tree);
        if (blockDecl) {
          type = state.getLLVMType(blockDecl);
        }
        Args.push_back(
            PrototypeArg{portBlockName, llvm::PointerType::get(type, 0)});
      } else if (decl->getObjectType() == "mainOutputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        auto type = llvm::Type::getDoubleTy(*state.TheContext);
        auto blockDecl =
            ASTQuery::findDeclarationByName(portBlockName, {}, tree);
        if (blockDecl) {
          type = state.getLLVMType(blockDecl);
        }
        OutArgs.push_back(
            PrototypeArg{portBlockName, llvm::PointerType::get(type, 0)});
      } else if (decl->getObjectType() == "propertyInputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        auto type = llvm::Type::getDoubleTy(*state.TheContext);
        auto blockDecl =
            ASTQuery::findDeclarationByName(portBlockName, {}, tree);
        if (blockDecl) {
          type = state.getLLVMType(blockDecl);
        }
        Args.push_back(
            PrototypeArg{portBlockName, llvm::PointerType::get(type, 0)});
      } else if (decl->getObjectType() == "propertyOutputPort") {
        auto blockNode = decl->getPropertyValue("block");
        if (blockNode && blockNode->getNodeType() == AST::Block) {
          portBlockName =
              std::static_pointer_cast<BlockNode>(blockNode)->getName();
        }
        auto type = llvm::Type::getDoubleTy(*state.TheContext);
        auto blockDecl =
            ASTQuery::findDeclarationByName(portBlockName, {}, tree);
        if (blockDecl) {
          type = state.getLLVMType(blockDecl);
        }
        OutArgs.push_back(
            PrototypeArg{portBlockName, llvm::PointerType::get(type, 0)});
      }
    }
  }

  auto functionScope = *scope;
  if (funcDecl->getObjectType() == "reaction" ||
      funcDecl->getObjectType() == "loop") {
    // share parent scope for reaction and loop
    for (const auto &node : tree->getChildren()) {
      if (node->getNodeType() == AST::Declaration ||
          node->getNodeType() == AST::BundleDeclaration) {
        // FIMXE insert right scope in the right place
        functionScope.back().second.push_back(node);
      }
    }
  }

  auto blocks = funcDecl->getPropertyValue("blocks");
  if (blocks) {
    for (const auto &blockDecl : blocks->getChildren()) {
      for (auto &scopeHead : functionScope) {
        if (scopeHead.first == nullptr) {
          scopeHead.second.push_back(blockDecl);
        }
      }
    }
    //      std::vector<ASTNode> internalVariables = blocks->getChildren();
  }

  auto streams = funcDecl->getPropertyValue("streams");
  std::vector<std::shared_ptr<DeclarationNode>> usedInternalVariables;
  std::vector<std::shared_ptr<DeclarationNode>> usedExternalVariables;
  std::vector<std::unique_ptr<ExprAST>> collected;
  std::unique_ptr<ExprAST> out;
  std::vector<llvm::Function *> externalFunctions;
  for (const auto &streamNode : streams->getChildren()) {
    if (streamNode->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(streamNode);
      auto code = createStreamCode(stream, tree, &functionScope, state);
      for (auto &domainCode : code) {
        while (domainCode.second.expr.size() > 0) {
          collected.push_back(std::move(domainCode.second.expr.front()));
          domainCode.second.expr.erase(domainCode.second.expr.begin());
        }
        externalFunctions.insert(externalFunctions.end(),
                                 domainCode.second.externalFunctions.begin(),
                                 domainCode.second.externalFunctions.end());
        for (const auto &readVar : domainCode.second.readVariables) {
          if (readVar->getNodeType() == AST::Block) {
            auto block = std::static_pointer_cast<BlockNode>(readVar);
            std::string blockName = block->getName();
            if (std::find_if(Args.begin(), Args.end(),
                             [&blockName](const PrototypeArg &x) {
                               return x.name == blockName;
                             }) == Args.end() &&
                std::find_if(OutArgs.begin(), OutArgs.end(),
                             [&blockName](const PrototypeArg &x) {
                               return x.name == blockName;
                             }) == OutArgs.end()) {
              if (readVar->getNodeType() == AST::Block) {
                auto decl = ASTQuery::findDeclarationByName(
                    std::static_pointer_cast<BlockNode>(readVar)->getName(), {},
                    blocks);
                if (decl) {
                  usedInternalVariables.push_back(decl);
                } else if (funcDecl->getObjectType() == "reaction" ||
                           funcDecl->getObjectType() == "loop") {
                  decl = ASTQuery::findDeclarationByName(
                      std::static_pointer_cast<BlockNode>(readVar)->getName(),
                      *scope, tree);
                  if (decl) {
                    usedExternalVariables.push_back(decl);
                  }
                }
              }
            }
          }
        }
        for (const auto &writeVar : domainCode.second.writeVariables) {
        }
      }
    } else {
    }
  }
  for (const auto &decl : usedExternalVariables) {
    ExternalArgs.push_back(PrototypeArg{
        decl->getName(), llvm::PointerType::get(state.getLLVMType(decl), 0)});
  }

  auto usedPortPropertiesNodes = CodeAnalysis::getUsedPortProperties(funcDecl);
  for (const auto &pp : usedPortPropertiesNodes) {
    // FIXME validate unique name
    std::string name = pp->getName() + "_" + pp->getPortName();
    UsedPortProperties.push_back(
        PrototypeArg{name, llvm::Type::getInt64Ty(*state.TheContext)});
  }
  llvm::Function *TheFunction = state.getFunctionInModule(func->getName());
  if (TheFunction) {
    // TODO this is here to check if current function is the same as existing
    // function
    return nullptr;
  }
  auto proto = std::make_unique<PrototypeAST>(func->getName(), OutArgs, Args,
                                              ExternalArgs, UsedPortProperties);

  // llvm::Function *TheFunction = state.getFunctionInModule(P.getName());
  auto newfunc =
      std::make_unique<FunctionAST>(std::move(proto), std::move(collected));
  newfunc->externalFunctions = std::move(externalFunctions);
  newfunc->internalVariables = std::move(usedInternalVariables);

  if (funcDecl->getObjectType() == "module") {
    newfunc->callType = CallableType::Module;
  } else if (funcDecl->getObjectType() == "reaction") {
    newfunc->callType = CallableType::Reaction;
  } else {
    std::cerr << "Callable type unsuported" << std::endl;
  }
  return newfunc;
}

void generateCode(ASTNode tree, ScopeStack *scope, StrideCompiler &state) {

  //  createGlobals(tree, state);
  std::map<std::string, std::vector<std::unique_ptr<ExprAST>>>
      domainGeneratedCode;
  std::vector<PrototypeArg> ExternalArgs;

  std::vector<DomainArg> domainArgs;

  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);

      auto code = createStreamCode(stream, tree, scope, state);
      for (auto &domainCode : code) {
        for (const auto &f : domainCode.second.functions) {
          f->codegen(state);
        }

        while (domainCode.second.expr.size() > 0) {
          domainGeneratedCode[domainCode.first].emplace_back(
              std::move(domainCode.second.expr.front()));
          domainCode.second.expr.erase(domainCode.second.expr.begin());
        }
      }

      //      auto *RetVal =
      //          llvm::ConstantInt::get(state.Builder->getInt32Ty(), 0,
      //          true);
      //      state.Builder->CreateRet(RetVal);
    } else if (node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::BundleDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getObjectType() == "signal" ||
          decl->getObjectType() == "constant") {
        // Global signals become pointers to domain function
        auto *type = state.getLLVMType(decl);
        // TODO For now all signals declared in the root domain are arguments to
        // the domain function but this should be defined somewhere with more
        // control, to choose what is external and the order of arguments to the
        // function
        ExternalArgs.push_back(
            {decl->getName(), llvm::PointerType::get(type, 0)});
        if (type->isDoubleTy()) {
          domainArgs.push_back({decl->getName(), DataType::DOUBLE});
        } else if (type->isIntegerTy(1)) {
          domainArgs.push_back({decl->getName(), DataType::BOOL});
        } else if (type->isIntegerTy(32)) {
          domainArgs.push_back({decl->getName(), DataType::INT32});
          //        } else if (type->isIntegerTy(64)) {
          //          domainArgs.push_back({decl->getName(), DataType::INT64});
        } else {
          assert(0 == 1);
        }
      } else if (decl->getObjectType() == "switch") {
        ExternalArgs.push_back(
            PrototypeArg{decl->getName(),
                         llvm::PointerType::get(state.getLLVMType(decl), 0)});
        domainArgs.push_back({decl->getName(), DataType::BOOL});
      } else {
        continue;
      }
    }
  }
  for (auto it = domainGeneratedCode.begin(); it != domainGeneratedCode.end();
       it++) {
    auto proto = std::make_unique<PrototypeAST>(
        std::string(it->first + "_process"), std::vector<PrototypeArg>{},
        std::vector<PrototypeArg>{}, ExternalArgs, std::vector<PrototypeArg>{});
    auto newfunc =
        std::make_unique<FunctionAST>(std::move(proto), std::move(it->second));
    newfunc->callType = CallableType::DomainFunction;

    newfunc->codegen(state);
    state.domainArgs[it->first] = domainArgs;
  }
}
