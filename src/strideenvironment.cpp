#include <filesystem>
#include <fstream>
#include <iostream>

// stride
#include "astfunctions.h"
#include "astquery.h"
#include "coderesolver.h"

// stridejit
#include "strideenvironment.hpp"
#include "stridegenerator.hpp"

// llvm
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
//#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
//#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
//#include "llvm/ExecutionEngine/Orc/EPCIndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
//#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
//#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
//#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
//#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
//#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

// JIT
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
//#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

//#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"

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
  // Add domain external signals to root scope
  // The must be added before code resolver so they are available.
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getObjectType() == "_domainDefinition") {
        auto inputsNode = decl->getPropertyValue("inputs");
        for (const auto &inputNode : inputsNode->getChildren()) {
          if (inputNode->getNodeType() == AST::Declaration ||
              inputNode->getNodeType() == AST::BundleDeclaration) {
            auto inputDecl =
                std::static_pointer_cast<DeclarationNode>(inputNode);
            if (inputDecl->getObjectType() == "signal" ||
                inputDecl->getObjectType() == "switch") {
              tree->addChild(inputNode);
            } else {
              std::cerr << "ERROR unexptected declaration for domain input "
                           "declaration:"
                        << AST::toText(inputNode) << std::endl;
            }
          }
        }
        auto outputsNode = decl->getPropertyValue("outputs");
        for (const auto &outputNode : outputsNode->getChildren()) {
          if (outputNode->getNodeType() == AST::Declaration ||
              outputNode->getNodeType() == AST::BundleDeclaration) {
            auto inputDecl =
                std::static_pointer_cast<DeclarationNode>(outputNode);
            if (inputDecl->getObjectType() == "signal" ||
                inputDecl->getObjectType() == "switch") {
              tree->addChild(outputNode);
            } else {
              std::cerr << "ERROR unexptected declaration for domain output "
                           "declaration:"
                        << AST::toText(outputNode) << std::endl;
            }
          }
        }
      }
    }
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

  StrideGenerator::generateCode(tree, &globalScope, state);
  //  if (mVerbose) {
  //    state.TheModule->dump();
  //  }

  if (m_optimizeCode) {
    std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
    TheFPM = std::make_unique<llvm::legacy::FunctionPassManager>(
        state.TheModule.get());

    // Do simple "peephole" optimizations and bit-twiddling optzns.
    TheFPM->add(llvm::createInstructionCombiningPass());
    // Reassociate expressions.
    TheFPM->add(llvm::createReassociatePass());
    // Eliminate Common SubExpressions.
    TheFPM->add(llvm::createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    TheFPM->add(llvm::createCFGSimplificationPass());

    { // Potential for loop vectorization?
      // From:
      // https://discourse.llvm.org/t/how-to-generate-ir-so-that-the-loop-vectorizer-can-vectorize-it/69096
      //      llvm::LoopAnalysisManager     lam;
      //      llvm::FunctionAnalysisManager fam;
      //      llvm::CGSCCAnalysisManager    cgam;
      //      llvm::ModuleAnalysisManager   mam;

      //      llvm::PassBuilder pb;

      //      pb.registerModuleAnalyses(mam);
      //      pb.registerCGSCCAnalyses(cgam);
      //      pb.registerFunctionAnalyses(fam);
      //      pb.registerLoopAnalyses(lam);
      //      pb.crossRegisterProxies(lam, fam, cgam, mam);

      //      llvm::ModulePassManager mpm =
      //      pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

      //      mpm.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::LoopVectorizePass()));

      //      mpm.run(*data->module, mam);
    }

    TheFPM->doInitialization();

    for (auto &F : *state.TheModule) {
      TheFPM->run(F);
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
  M[Mangle("__stride_Greater_d_dd")] = llvm::JITEvaluatedSymbol(
      llvm::pointerToJITTargetAddress(&__stride_Greater_d_dd),
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

llvm::Expected<llvm::JITEvaluatedSymbol>
StrideEnvironment::getFunction(std::string functionName) {
  auto EntrySym = JIT->lookup(functionName.c_str());
  return EntrySym;
}
