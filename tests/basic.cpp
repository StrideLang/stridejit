#include "gtest/gtest.h"

// stridejit
#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// stride
#include "stride/codegen/astquery.hpp"

// llvm
#include "llvm/ExecutionEngine/JITSymbol.h"
//#include "llvm/ExecutionEngine/Orc/LLJIT.h"

// TEST(JIT, LoopIterator) {

//  strd::StrideEnvironment strenv;

//  auto ret =
//      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop_iterator.stride");
//  EXPECT_TRUE(ret);
//  ret = strenv.compileInMemory();
//  EXPECT_TRUE(ret);

//  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
//      strenv.JIT->lookup("TestDomain_process");
//  if (!EntrySym) {
//    std::cerr << "No entry" << std::endl;
//  }

//  auto *Entry = (void (*)(...))EntrySym->getAddress();

//  EXPECT_NE(Entry, nullptr);
//  {
//    double List[20] = {1000, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//                       1,    1, 1, 1, 1, 1, 1, 1, 1, 100};
//    double Out = 0;
//    Entry(List, &Out);
//    EXPECT_EQ(Out, 1000);
//  }
//  {
//    double List[20] = {1, 1, 1, 1, 1, 1000, 1, 1, 1, 1,
//                       1, 1, 1, 1, 1, 1,    1, 1, 1, 100};
//    double Out = 0;
//    Entry(List, &Out);
//    EXPECT_EQ(Out, 1000);
//  }
//  {
//    double List[20] = {1, 1, 1, 1, 1, 100, 1, 1, 1, 1,
//                       1, 1, 1, 1, 1, 1,   1, 1, 1, 1000};
//    double Out = 0;
//    Entry(List, &Out);
//    EXPECT_EQ(Out, 1000);
//  }
//}

TEST(JIT, ReactionInModule) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction_in_module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.JIT->lookup("TestDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();
  EXPECT_NE(Entry, nullptr);

  double In = 3.0;
  double Out = 1.0;

  Entry(&In, &Out);
  EXPECT_EQ(Out, 1.0);
  In = 4.0;
  Entry(&In, &Out);
  EXPECT_EQ(Out, 1.0);
  In = 5.0;
  Entry(&In, &Out);
  EXPECT_EQ(Out, 1.0);
  In = 6.0;
  Entry(&In, &Out);
  EXPECT_EQ(Out, 6.0);
  In = 10.0;
  Entry(&In, &Out);
  EXPECT_EQ(Out, 10.0);
}

TEST(JIT, Reset) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reset.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.JIT->lookup("TestDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();
  EXPECT_NE(Entry, nullptr);

  llvm::Expected<llvm::JITEvaluatedSymbol> InitSym =
      strenv.JIT->lookup("TestDomain_init");
  if (!InitSym) {
    std::cerr << "No init entry" << std::endl;
  }
  auto *InitEntry = (void (*)(...))InitSym->getAddress();
  EXPECT_NE(InitEntry, nullptr);

  double In;
  int32_t Count = 0;

  InitEntry(&In, &Count);

  Entry(&In, &Count);
  EXPECT_EQ(In, 5.0);
  Entry(&In, &Count);
  EXPECT_EQ(In, 7.0);
  Entry(&In, &Count);
  EXPECT_EQ(In, 9.0);
  Entry(&In, &Count);
  EXPECT_EQ(In, 11.0);
  Entry(&In, &Count);
  EXPECT_EQ(In, 13.0);
  Entry(&In, &Count);
  EXPECT_EQ(In, 5.0);
  Entry(&In, &Count);
  EXPECT_EQ(In, 7.0);
}

TEST(JIT, DomainInit) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domain_init.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.JIT->lookup("TestDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  llvm::Expected<llvm::JITEvaluatedSymbol> InitSym =
      strenv.JIT->lookup("TestDomain_init");
  if (!InitSym) {
    std::cerr << "No init entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();
  auto *InitEntry = (void (*)(...))InitSym->getAddress();

  EXPECT_NE(Entry, nullptr);
  double In;
  int32_t In2;
  InitEntry(&In, &In2);
  EXPECT_EQ(In, 3.0);
  EXPECT_EQ(In2, 10);
  Entry(&In, &In2);
  EXPECT_EQ(In, 5.0);
  EXPECT_EQ(In2, 11);
  InitEntry(&In, &In2);
  EXPECT_EQ(In, 3.0);
  EXPECT_EQ(In2, 10);
}

TEST(JIT, Loop) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.JIT->lookup("TestDomain_process");
  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }

  auto *Entry = (void (*)(...))EntrySym->getAddress();

  EXPECT_NE(Entry, nullptr);
  double List[20] = {1000, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                     1,    1, 1, 1, 1, 1, 1, 1, 1, 100};
  double Out = 0;
  Entry(List, &Out);

  EXPECT_EQ(Out, 1118);
}

TEST(JIT, PortPropertySize) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "port_property_size.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  EXPECT_NE(Entry, nullptr);
  double In[16] = {0};
  double Out[2] = {0};
  Entry(In, Out);

  EXPECT_EQ(Out[0], 32);
  EXPECT_EQ(Out[1], 8);
}

TEST(JIT, TypecastStream) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "typecast_stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  //    EXPECT_NE(Entry, nullptr);
  //    //  double in = 0;
  //    //  double out = 0;
  int32_t Int[2] = {0};
  double Float[2] = {0};
  Entry(Int, Float);

  EXPECT_EQ(Int[0], 1);
  EXPECT_EQ(Int[1], 4);

  EXPECT_DOUBLE_EQ(Float[0], 3.0);
  EXPECT_DOUBLE_EQ(Float[1], 5.0);
}

TEST(JIT, IntegerType) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "integer_type.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  //    EXPECT_NE(Entry, nullptr);
  //    //  double in = 0;
  //    //  double out = 0;
  double S = 0;
  int32_t Val = 0;
  Entry(&S, &Val);

  EXPECT_EQ(S, 0);

  Entry(&S, &Val);
  EXPECT_EQ(S, 3);
}

TEST(JIT, Bundles) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "bundles.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  double In[16] = {0};
  double Out[16] = {0};
  In[1] = 0.3;
  In[2] = 0.4;

  Entry(In, Out);
  // Stride code: In[1] >> Out[2];
  EXPECT_DOUBLE_EQ(In[1], Out[2]);
  // Stride code: In[2] + 1.2 >> Out[3];
  EXPECT_DOUBLE_EQ(Out[3], 0.4 + 1.2);
  //  Stride code : In[3] >> Cos() >> Out[4];
  EXPECT_DOUBLE_EQ(Out[4], cos(In[3]));
}

TEST(JIT, CompileToDisk) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileObjectToDisk("out");
  EXPECT_TRUE(ret);
}

TEST(JIT, PackDomainExternalPointer) {

  strd::StrideEnvironment strenv;
  strenv.state.setConfiguration(
      strd::StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL);
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  auto args = strenv.state.domainArgs["TestDomain"];

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

  strd::StrideEnvironment strenv;
  strenv.state.setConfiguration(
      strd::StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL);
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

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

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reactioncondition.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

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

TEST(JIT, Reaction) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

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

TEST(JIT, PassThru) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  double in = 1.0;
  double out = 0;

  Entry(&in, &out);

  EXPECT_FLOAT_EQ(out, 1.0);
}

TEST(JIT, Stream) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  //  auto Entry = (void (*)(...))(EntrySym->getAddress());
}

TEST(JIT, DomainFunctions) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  EXPECT_EQ(strenv.state.domainArgs.size(), 1);
  EXPECT_EQ(strenv.state.domainArgs["TestDomain"].size(), 1);
  EXPECT_EQ(strenv.state.domainArgs["TestDomain"][0].name, "Out");
  EXPECT_EQ(strenv.state.domainArgs["TestDomain"][0].type,
            strd::DataType::DOUBLE);
}

TEST(JIT, ModuleInternal) {

  strd::StrideEnvironment strenv;
  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module_internal.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  double out;

  Entry(&out);
  EXPECT_EQ(out, 5);
}

TEST(JIT, CreateClass) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  double out;

  Entry(&out);
  EXPECT_EQ(out, 5);
}

TEST(JIT, MathFunction) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "functions.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  double out = 0;

  Entry(&out);
  EXPECT_EQ(out, 1.0);
}

TEST(JIT, TwoStreams) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "twostreams.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

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

  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "switchout.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

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

  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "listinput.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  double in = 0;
  double out = 0;

  Entry(&in, &out);
  EXPECT_EQ(out, 0.0);

  in = 10;
  Entry(&in, &out);
  EXPECT_EQ(out, 1.0);
}

TEST(JIT, Domains) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domains.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

  EXPECT_NE(Entry, nullptr);
  double in = 1;
  double out = 0;
  Entry(&in, &out);
  EXPECT_EQ(out, 1);
}

TEST(JIT, IO) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "io.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::JITEvaluatedSymbol> EntrySym =
      strenv.getFunction("TestDomain_process");

  if (!EntrySym) {
    std::cerr << "No entry" << std::endl;
  }
  auto Entry = (void (*)(...))(EntrySym->getAddress());

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

TEST(Function, Simple) {

  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;

  auto stream = tree->getChildren()[2];

  auto addFunc = std::static_pointer_cast<strd::FunctionNode>(
      std::static_pointer_cast<strd::StreamNode>(
          std::static_pointer_cast<strd::StreamNode>(stream)->getRight())
          ->getLeft());
  auto prev = std::static_pointer_cast<strd::StreamNode>(stream)->getLeft();

  auto next =
      std::static_pointer_cast<strd::StreamNode>(
          std::static_pointer_cast<strd::StreamNode>(stream)->getRight())
          ->getRight();

  strd::ScopeStack scope;
  auto funcDecl =
      strd::ASTQuery::findDeclarationByName(addFunc->getName(), scope, tree);
  EXPECT_NE(funcDecl, nullptr);
  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, tree, &scope, strenv.state);
  auto *v = func->codegen(strenv.state);
  EXPECT_NE(v, nullptr);

  EXPECT_TRUE(v->getType()->isPointerTy());
  //  v->dump();

  strenv.state.TheModule->dump();
}

//%Input3 = load double, double* %Input1, align 8
//    double 2.000000e+00
//    define double @AddTwo(double %Input, double %Output) {
//        entry:
//                %Output2 = alloca double, align 8
//            %Input1 = alloca double, align 8
//            store double %Input, double* %Input1, align 8
//            store double %Output, double* %Output2, align 8
//            %Input3 = load double, double* %Input1, align 8
//            %addtmp = fadd double %Input3, 2.000000e+00
//            store double %addtmp, double* %Output2, align 8
//            ret double %addtmp
//    }

TEST(Value, Assignment) {
  auto decl = std::make_shared<strd::DeclarationNode>("G", "signal", nullptr,
                                                      __FILE__, __LINE__);
  auto value1 = std::make_shared<strd::ValueNode>(3.3, __FILE__, __LINE__);
  auto block = std::make_shared<strd::BlockNode>("G", __FILE__, __LINE__);

  auto str =
      std::make_shared<strd::StreamNode>(value1, block, __FILE__, __LINE__);

  strd::StrideEnvironment strenv;

  strenv.state.NamedValues[decl->getName()] =
      strd::RealExprAST(0.0).codegen(strenv.state);

  auto left = str->getLeft();
  auto right = str->getRight();
  std::unique_ptr<strd::ExprAST> n1, n2;
  if (left->getNodeType() == strd::AST::Real) {
    n1 = std::make_unique<strd::RealExprAST>(
        std::static_pointer_cast<strd::ValueNode>(left)->getRealValue());
  }
  if (right->getNodeType() == strd::AST::Block) {
    strenv.state.NamedValues[std::static_pointer_cast<strd::BlockNode>(right)
                                 ->getName()] = n1->codegen(strenv.state);
  }
  EXPECT_TRUE(strenv.state.NamedValues["G"]->getType()->isDoubleTy());
  llvm::ConstantFP *CFP =
      llvm::dyn_cast<llvm::ConstantFP>(strenv.state.NamedValues["G"]);
  EXPECT_NE(CFP, nullptr);
  EXPECT_EQ(CFP->getValue().convertToDouble(), 3.3);
}

TEST(Expressions, FloatLiterals) {
  auto value1 = std::make_shared<strd::ValueNode>(3.0, __FILE__, __LINE__);
  auto value2 = std::make_shared<strd::ValueNode>(5.1, __FILE__, __LINE__);

  auto expr = std::make_shared<strd::ExpressionNode>(
      strd::ExpressionNode::Add, value1, value2, __FILE__, __LINE__);

  strd::StrideEnvironment strenv;

  auto left = expr->getLeft();
  auto right = expr->getRight();
  std::unique_ptr<strd::ExprAST> n1, n2;

  if (left->getNodeType() == strd::AST::Real) {
    n1 = std::make_unique<strd::RealExprAST>(
        std::static_pointer_cast<strd::ValueNode>(left)->getRealValue());
  } else if (left->getNodeType() == strd::AST::Block) {
    n1 = std::make_unique<strd::VariableExprAST>(
        std::static_pointer_cast<strd::BlockNode>(left)->getName());
  }

  if (right->getNodeType() == strd::AST::Real) {
    n2 = std::make_unique<strd::RealExprAST>(
        std::static_pointer_cast<strd::ValueNode>(right)->getRealValue());
  } else if (right->getNodeType() == strd::AST::Block) {
    n2 = std::make_unique<strd::VariableExprAST>(
        std::static_pointer_cast<strd::BlockNode>(right)->getName());
  }
  auto binExpr = strd::BinaryExprAST('+', std::move(n1), std::move(n2));
  auto *v = binExpr.codegen(strenv.state);
  EXPECT_TRUE(v->getType()->isDoubleTy());
  llvm::ConstantFP *CFP = llvm::dyn_cast<llvm::ConstantFP>(v);
  EXPECT_NE(CFP, nullptr);
  EXPECT_EQ(CFP->getValue().convertToDouble(), 8.1);
}
