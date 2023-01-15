#include "gtest/gtest.h"

#include "exprast.hpp"
#include "numberexprast.hpp"
#include "strideenvironment.hpp"

#include "astfunctions.h"
#include "strideparser.h"

#include "llvm/ADT/STLExtras.h"
//#include "llvm/ExecutionEngine/ExecutionEngine.h"
//#include "llvm/ExecutionEngine/GenericValue.h"
//#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

//#include "llvm/ExecutionEngine/JITLink/JITLink.h"
//#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
//#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
//#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

//#include "llvm/Support/Casting.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
//#include "llvm/Support/ManagedStatic.h"
//#include "llvm/Support/raw_ostream.h"

using namespace llvm;

TEST(JIT, Bundles) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "bundles.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double In[16] = {0};
  double Out[16] = {0};
  Entry(In, Out);

  EXPECT_DOUBLE_EQ(In[1], Out[2]);
}

TEST(JIT, CompileToDisk) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileObjectToDisk("out");
  EXPECT_TRUE(ret);
}

TEST(JIT, PackDomainExternalPointer) {

  StrideEnvironment strenv;
  strenv.state.setConfiguration(StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL);
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(void *))EntrySym->getAddress();

  auto args = strenv.state.domainArgs["RootDomain"];

  std::map<std::string, double> doubleArgs;
  size_t memsize = 0;

  for (const auto &arg : args) {
    doubleArgs[arg.name] = 0.0;
    memsize += sizeof(double *);
  }
  uint8_t *domainArgs = (uint8_t *)malloc(memsize);
  double *in = &doubleArgs["In"];
  memcpy(domainArgs, &in, sizeof(double *));
  double *out = &doubleArgs["Out"];
  memcpy(domainArgs + sizeof(double *), &out, sizeof(double *));

  doubleArgs["In"] = 1.0;

  Entry(domainArgs);

  EXPECT_FLOAT_EQ(doubleArgs["Out"], 1.0);
}

TEST(JIT, PackDomainExternal) {

  StrideEnvironment strenv;
  strenv.state.setConfiguration(StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL);
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double in = 1.0;
  double out = 0.0;

  struct {
    double *In;
    double *Out;
  } PackedArgs;

  PackedArgs.In = &in;
  PackedArgs.Out = &out;

  Entry(&PackedArgs);

  EXPECT_FLOAT_EQ(*PackedArgs.Out, 1.0);
}

TEST(JIT, ReactionCondition) {

  StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reactioncondition.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  EXPECT_NE(Entry, nullptr);
  //  double in = 0;
  //  double out = 0;
  double S = 0;
  double Val = 0;
  Entry(&S, &Val);

  EXPECT_EQ(S, 0);

  Entry(&S, &Val);
  EXPECT_EQ(S, 3);

  //  EntrySym = strenv.JIT->lookup("S");
  //  if (!EntrySym) {
  //    std::cerr << "No entry" << std::endl;
  //  }
  //  double v = *((double *)EntrySym->getAddress());
  //  EXPECT_EQ(Entry, nullptr);
}

TEST(JIT, PassThru) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double in = 1.0;
  double out = 0;

  Entry(&in, &out);

  EXPECT_FLOAT_EQ(out, 1.0);
}

TEST(JIT, Reaction) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  EXPECT_NE(Entry, nullptr);
  //  double in = 0;
  //  double out = 0;
  double S = 0;
  Entry(&S);

  EXPECT_EQ(S, 3);

  //  EntrySym = strenv.JIT->lookup("S");
  //  if (!EntrySym) {
  //    std::cerr << "No entry" << std::endl;
  //  }
  //  double v = *((double *)EntrySym->getAddress());
  //  EXPECT_EQ(Entry, nullptr);
}

TEST(JIT, Stream) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("Out");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  //  EXPECT_EQ(*(double *)EntrySym->getAddress(), 1);
}

TEST(JIT, DomainFunctions) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  EXPECT_EQ(strenv.state.domainArgs.size(), 1);
  EXPECT_EQ(strenv.state.domainArgs["RootDomain"].size(), 1);
  EXPECT_EQ(strenv.state.domainArgs["RootDomain"][0].name, "Out");
  EXPECT_EQ(strenv.state.domainArgs["RootDomain"][0].type, DataType::DOUBLE);
}

TEST(JIT, ModuleInternal) {

  StrideEnvironment strenv;
  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module_internal.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double out;

  Entry(&out);
  EXPECT_EQ(out, 5);
}

TEST(JIT, CreateClass) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double out;

  Entry(&out);
  EXPECT_EQ(out, 5);
}

TEST(JIT, MathFunction) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "functions.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double out = 0;

  Entry(&out);
  EXPECT_EQ(out, 1.0);
}

TEST(JIT, Create) {

  ASTNode tree;
  tree = ASTFunctions::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_NE(tree, nullptr);

  StrideEnvironment strenv;
  ScopeStack scope;
  generateCode(tree, &scope, strenv.state);
  strenv.state.TheModule->dump();
  // -------------------

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  auto JTMB = orc::JITTargetMachineBuilder::detectHost();
  if (!JTMB) {
    std::cerr << " No machine builder" << std::endl;
  }
  JTMB->setCodeModel(CodeModel::Small);

  auto JIT =
      orc::LLJITBuilder()
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
  if (!JIT)
    return; // JIT.takeError();

  if (auto Err = (*JIT)->addIRModule(
          llvm::orc::ThreadSafeModule(std::move(strenv.state.TheModule),
                                      std::move(strenv.state.TheContext)))) {
  }
  auto EntrySym = (*JIT)->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double out = 0;

  Entry(&out);
  EXPECT_EQ(out, 5);
}

TEST(JIT, TwoStreams) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "twostreams.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double in = 0;
  double out = 0;

  Entry(&in, &out);
  EXPECT_EQ(out, cos(cos(in)));

  in = 3.14159;
  Entry(&in, &out);
  EXPECT_FLOAT_EQ(out, cos(cos(in)));

  in = 1.0;
  Entry(&in, &out);
  EXPECT_FLOAT_EQ(out, cos(cos(in)));
}

TEST(JIT, SwitchOut) {

  StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "switchout.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double in = 0;
  bool out = false;

  Entry(&in, &out);
  EXPECT_FALSE(out);

  in = 10;
  Entry(&in, &out);
  EXPECT_TRUE(out);

  in = 1;
  Entry(&in, &out);
  EXPECT_FALSE(out);
}

TEST(JIT, List) {

  StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "listinput.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double in = 0;
  double out = 0;

  Entry(&in, &out);
  EXPECT_EQ(out, 0.0);

  in = 10;
  Entry(&in, &out);
  EXPECT_EQ(out, 1.0);
}

TEST(JIT, Domains) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domains.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("TestDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  EXPECT_NE(Entry, nullptr);
  double in = 0;
  double out = 0;
}

TEST(JIT, IO) {

  StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "io.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compile();
  EXPECT_TRUE(ret);

  auto EntrySym = strenv.JIT->lookup("RootDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  double in = 0;
  double out = 0;

  Entry(&in, &out);
  EXPECT_EQ(out, 1.0);

  in = 3.14159;
  Entry(&in, &out);
  EXPECT_FLOAT_EQ(out, cos(3.14159));

  in = 1.0;
  Entry(&in, &out);
  EXPECT_FLOAT_EQ(out, cos(1.0));
}
