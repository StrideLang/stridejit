#include "gtest/gtest.h"

// stridejit
#include "stride/stridejit/binaryexprast.hpp"
#include "stride/stridejit/exprast.hpp"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// stride
#include "stride/utils/astquery.h"

// llvm
#include "llvm/Support/raw_ostream.h"

TEST(Basic, Assignment) {
  auto decl = std::make_shared<strd::DeclarationNode>("G", "signal", nullptr,
                                                      __FILE__, __LINE__);
  auto value1 = std::make_shared<strd::ValueNode>(3.3, __FILE__, __LINE__);
  auto block = std::make_shared<strd::BlockNode>("G", __FILE__, __LINE__);

  auto str =
      std::make_shared<strd::StreamNode>(value1, block, __FILE__, __LINE__);

  strd::StrideEnvironment strenv;

  strenv.mStrideEnv.NamedValues[decl->getName()] =
      strd::RealExprAST(0.0).codegen(strenv.mStrideEnv);

  auto left = str->getLeft();
  auto right = str->getRight();
  std::unique_ptr<strd::ExprAST> n1, n2;
  if (left->getNodeType() == strd::AST::Real) {
    n1 = std::make_unique<strd::RealExprAST>(
        std::static_pointer_cast<strd::ValueNode>(left)->getRealValue());
  }
  if (right->getNodeType() == strd::AST::Block) {
    strenv.mStrideEnv.NamedValues[std::static_pointer_cast<strd::BlockNode>(right)
                                 ->getName()] = n1->codegen(strenv.mStrideEnv);
  }
  // llvm::Type *type =strenv.state.NamedValues["G"].second;

  EXPECT_TRUE(strenv.mStrideEnv.NamedValues["G"].second.value()->isDoubleTy());
  llvm::ConstantFP *CFP =
      llvm::dyn_cast<llvm::ConstantFP>(strenv.mStrideEnv.NamedValues["G"].first);
  EXPECT_NE(CFP, nullptr);
  EXPECT_EQ(CFP->getValue().convertToDouble(), 3.3);
}

TEST(Basic, ExpressionFloatLiterals) {
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
  auto v = binExpr.codegen(strenv.mStrideEnv);
  EXPECT_TRUE(v.second.value()->isDoubleTy());
  llvm::ConstantFP *CFP = llvm::dyn_cast<llvm::ConstantFP>(v.first);
  EXPECT_NE(CFP, nullptr);
  EXPECT_EQ(CFP->getValue().convertToDouble(), 8.1);
}

TEST(Basic, FunctionSimple) {

  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);

  auto stream = tree->getChildren()[2];
  ASSERT_EQ(stream->getNodeType(), strd::AST::Stream);

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
      funcDecl, addFunc, tree, &scope, strenv.mStrideEnv);
  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  EXPECT_TRUE(v->getType()->isPointerTy());
  //  v->print(llvm::outs());

  // Although function is emmitted, it will be empty as the streams in it
  // have
  // not been processed
  strenv.mStrideEnv.TheModule->print(llvm::outs(), nullptr);
  llvm::outs() << "\n";
}

TEST(Basic, PassThru) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 1, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);
  double out =
      strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt, "TestDomain")
          .value_or(0);

  EXPECT_FLOAT_EQ(out, 1.0);
}

TEST(Basic, DomainVariable) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domain_variable.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 6, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);

  auto A = strenv.getStateVar<int32_t>(statePtr.get(), "A", std::nullopt, "TestDomain");
  EXPECT_TRUE(A.has_value());
  EXPECT_EQ(*A, 6);
}

TEST(JIT, BlockDefaults) {
  // Test defaults for signal declaration of domain variable

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "block_default.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_init", args);
  strenv.invoke("TestDomain_process", args);

  auto out = strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt, "TestDomain");
  EXPECT_TRUE(out.has_value());
  EXPECT_EQ(out.value_or(0), 6);
}
TEST(JIT, DomainInit) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domain_init.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);
  auto In = strenv.getStateVar<int32_t>(statePtr.get(), "In", std::nullopt, "RootDomain");
  auto In2 = strenv.getStateVar<int32_t>(statePtr.get(), "In2", std::nullopt, "RootDomain");
  EXPECT_EQ(In.value_or(0), 3);
  EXPECT_EQ(In2.value_or(0), 10);

  strenv.invoke("RootDomain_process", args);
  In = strenv.getStateVar<int32_t>(statePtr.get(), "In", std::nullopt, "RootDomain");
  In2 = strenv.getStateVar<int32_t>(statePtr.get(), "In2", std::nullopt, "RootDomain");
  EXPECT_EQ(In.value_or(0), 5);
  EXPECT_EQ(In2.value_or(0), 11);

  strenv.invoke("RootDomain_init", args);
  In = strenv.getStateVar<int32_t>(statePtr.get(), "In", std::nullopt, "RootDomain");
  In2 = strenv.getStateVar<int32_t>(statePtr.get(), "In2", std::nullopt, "RootDomain");
  EXPECT_EQ(In.value_or(0), 3);
  EXPECT_EQ(In2.value_or(0), 10);
}

TEST(JIT, ExternalFunctions) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "external_function.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  {
    strenv.setStateVar(statePtr.get(), "Out", true, std::nullopt, "RootDomain");
    strenv.invoke("RootDomain_init", args);
    // out should get initialized to false
    auto out = strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(true);
    EXPECT_EQ(out, false);
  }

  strenv.setStateVar(statePtr.get(), "Out", false, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto out = strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(false);
  EXPECT_EQ(out, true);
}

TEST(JIT, DomainFunctions) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  EXPECT_EQ(strenv.mStrideEnv.domainArgs.size(), 1);
  EXPECT_EQ(strenv.mStrideEnv.domainArgs["RootDomain"].size(), 1);
  EXPECT_EQ(strenv.mStrideEnv.domainArgs["RootDomain"][0].name, "Out");
  EXPECT_EQ(strenv.mStrideEnv.domainArgs["RootDomain"][0].type,
            strd::DataType::DOUBLE);
}

TEST(JIT, Bundles) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "bundles.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 0.3, 1, "RootDomain");
  strenv.setStateVar(statePtr.get(), "In", 0.4, 2, "RootDomain");

  strenv.invoke("RootDomain_process", args);

  auto In1 = strenv.getStateVar<double>(statePtr.get(), "In", 1, "RootDomain").value_or(0.0);
  auto In2 = strenv.getStateVar<double>(statePtr.get(), "In", 2, "RootDomain").value_or(0.0);
  auto In3 = strenv.getStateVar<double>(statePtr.get(), "In", 3, "RootDomain").value_or(0.0);

  auto Out2 = strenv.getStateVar<double>(statePtr.get(), "Out", 2, "RootDomain").value_or(0.0);
  auto Out3 = strenv.getStateVar<double>(statePtr.get(), "Out", 3, "RootDomain").value_or(0.0);
  auto Out4 = strenv.getStateVar<double>(statePtr.get(), "Out", 4, "RootDomain").value_or(0.0);

  // Stride code: In[1] >> Out[2];
  EXPECT_DOUBLE_EQ(In1, Out2);
  // Stride code: In[2] + 1.2 >> Out[3];
  EXPECT_DOUBLE_EQ(Out3, 0.4 + 1.2);
  //  Stride code : In[3] >> Cos() >> Out[4];
  EXPECT_DOUBLE_EQ(Out4, cos(In3));
}

TEST(JIT, BundleDynamicIndex) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "bundle_variable.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 7.7, 1, "RootDomain");
  strenv.setStateVar(statePtr.get(), "In", 4.2, 2, "RootDomain");
  strenv.setStateVar(statePtr.get(), "Index", 2, std::nullopt, "RootDomain");

  strenv.invoke("RootDomain_process", args);

  auto Out2 = strenv.getStateVar<double>(statePtr.get(), "Out", 2, "RootDomain").value_or(0.0);
  auto Out3 = strenv.getStateVar<double>(statePtr.get(), "Out", 3, "RootDomain").value_or(0.0);
  auto Out4 = strenv.getStateVar<double>(statePtr.get(), "Out", 4, "RootDomain").value_or(0.0);

  // Stride code: In[Index] >> Out[3];
  EXPECT_DOUBLE_EQ(Out3, 4.2);
  // Stride code: In[Index] + 1.5 >> Out[4];
  EXPECT_DOUBLE_EQ(Out4, 4.2 + 1.5);
  // Stride code: In[1] >> Out[Index];
  EXPECT_DOUBLE_EQ(Out2, 7.7);

  // Test with another value for Index
  strenv.setStateVar(statePtr.get(), "Index", 5, std::nullopt, "RootDomain");
  strenv.setStateVar(statePtr.get(), "In", 9.1, 5, "RootDomain");
  for (int i = 0; i < 16; ++i) {
    strenv.setStateVar(statePtr.get(), "Out", 0.0, i, "RootDomain");
  }

  strenv.invoke("RootDomain_process", args);

  Out3 = strenv.getStateVar<double>(statePtr.get(), "Out", 3, "RootDomain").value_or(0.0);
  Out4 = strenv.getStateVar<double>(statePtr.get(), "Out", 4, "RootDomain").value_or(0.0);
  auto Out5 = strenv.getStateVar<double>(statePtr.get(), "Out", 5, "RootDomain").value_or(0.0);

  // Stride code: In[Index] >> Out[3];
  EXPECT_DOUBLE_EQ(Out3, 9.1);
  // Stride code: In[Index] + 1.5 >> Out[4];
  EXPECT_DOUBLE_EQ(Out4, 9.1 + 1.5);
  // Stride code: In[1] >> Out[Index];
  EXPECT_DOUBLE_EQ(Out5, 7.7);
}

TEST(JIT, Stream) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  
  strenv.invoke("TestDomain_process", args);
}

TEST(JIT, IO) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "io.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 0.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 3.14159, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_FLOAT_EQ(out, cos(3.14159));

  strenv.setStateVar(statePtr.get(), "In", 1.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_FLOAT_EQ(out, cos(1.0));
}

TEST(JIT, MathFunction) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "functions.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);
  auto out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(out, 1.0);
}

TEST(JIT, TwoStreams) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "twostreams.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 0.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(out, cos(cos(0.0)));

  strenv.setStateVar(statePtr.get(), "In", 3.14159, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_FLOAT_EQ(out, cos(cos(3.14159)));

  strenv.setStateVar(statePtr.get(), "In", 1.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_FLOAT_EQ(out, cos(cos(1.0)));
}

TEST(JIT, List) {

  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "listinput.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 0.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(out, 0.0);

  strenv.setStateVar(statePtr.get(), "In", 10.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(out, 1.0);
}

TEST(JIT, SwitchOut) {

  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "switchout.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 0.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto out = strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(false);
  EXPECT_FALSE(out);

  strenv.setStateVar(statePtr.get(), "In", 10.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(false);
  EXPECT_TRUE(out);

  strenv.setStateVar(statePtr.get(), "In", 1.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(false);
  EXPECT_FALSE(out);
}

TEST(JIT, Module) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);
  auto out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(out, 5);
}

TEST(JIT, Domains) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domains.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 1.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);
  auto out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "TestDomain").value_or(0.0);
  EXPECT_EQ(out, 1);
}

// % Input3 = load double, double * % Input1,
//   align 8 double 2.000000e+00 define double
//     @AddTwo(double % Input, double % Output){
//       entry : % Output2 = alloca double,
//       align 8 % Input1 = alloca double,
//       align 8 store double % Input,
//       double * % Input1,
//       align 8 store double % Output,
//       double * % Output2,
//       align 8 % Input3 = load double,
//       double * % Input1,
//       align 8 % addtmp = fadd double % Input3,
//       2.000000e+00 store double % addtmp,
//       double * % Output2,
//       align 8 ret double % addtmp
//     }

TEST(JIT, ModuleInternal) {

  strd::StrideEnvironment strenv;
  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module_internal.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "TestDomain").value_or(0.0);
  EXPECT_EQ(out, 5);
}

TEST(JIT, Reaction) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto Out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "TestDomain").value_or(0.0);
  EXPECT_EQ(Out, 3);
}

TEST(JIT, IntegerType) {
  // Depends on external function and reactions

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "integer_type.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);
  auto S = strenv.getStateVar<double>(statePtr.get(), "S", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(S, 0);

  strenv.invoke("RootDomain_process", args);
  S = strenv.getStateVar<double>(statePtr.get(), "S", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(S, 3);
}

TEST(JIT, ReactionCondition) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reactioncondition.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto Out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "TestDomain").value_or(0.0);
  EXPECT_EQ(Out, 0);

  strenv.invoke("TestDomain_process", args);
  Out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "TestDomain").value_or(0.0);
  EXPECT_EQ(Out, 3);
}

TEST(JIT, ReactionTerminateWhen) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "reaction_terminate_when.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto Out = strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                        "TestDomain")
                 .value_or(0);
  EXPECT_EQ(Out, 2);

  auto Done = strenv.getStateVar<bool>(statePtr.get(), "Done", std::nullopt,
                                       "TestDomain")
                  .value_or(false);
  EXPECT_TRUE(Done);
}

TEST(JIT, Reset) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reset.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  strenv.invoke("RootDomain_process", args);
  auto In = strenv.getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(In, 5.0);

  strenv.invoke("RootDomain_process", args);
  In = strenv.getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(In, 7.0);

  strenv.invoke("RootDomain_process", args);
  In = strenv.getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(In, 9.0);

  strenv.invoke("RootDomain_process", args);
  In = strenv.getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(In, 11.0);

  strenv.invoke("RootDomain_process", args);
  In = strenv.getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(In, 13.0);

  strenv.invoke("RootDomain_process", args);
  In = strenv.getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(In, 5.0);

  strenv.invoke("RootDomain_process", args);
  In = strenv.getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(In, 7.0);
}

TEST(JIT, LiteralModuleInput) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "literal_input.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto out0 = strenv.getStateVar<int32_t>(statePtr.get(), "Out", 0, "TestDomain").value_or(0);
  auto out1 = strenv.getStateVar<int32_t>(statePtr.get(), "Out", 1, "TestDomain").value_or(0);
  EXPECT_EQ(out0, 5);
  EXPECT_EQ(out1, 8);
}

TEST(JIT, TypecastStream) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "typecast_stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);

  auto Int0 = strenv.getStateVar<double>(statePtr.get(), "Int", 0, "RootDomain").value_or(0.0);
  auto Int1 = strenv.getStateVar<double>(statePtr.get(), "Int", 1, "RootDomain").value_or(0.0);
  EXPECT_EQ(Int0, 1);
  EXPECT_EQ(Int1, 4);

  auto Real0 = strenv.getStateVar<double>(statePtr.get(), "Real", 0, "RootDomain").value_or(0.0);
  auto Real1 = strenv.getStateVar<double>(statePtr.get(), "Real", 1, "RootDomain").value_or(0.0);
  EXPECT_DOUBLE_EQ(Real0, 3.0);
  EXPECT_DOUBLE_EQ(Real1, 5.0);
}

TEST(JIT, TypecastList) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "typecast_list.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "InReal", 5.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);

  auto compReal = strenv.getStateVar<double>(statePtr.get(), "CompReal", std::nullopt, "TestDomain").value_or(0.0);
  EXPECT_FALSE(compReal != 0.0);

  strenv.setStateVar(statePtr.get(), "InReal", 0.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);
  compReal = strenv.getStateVar<double>(statePtr.get(), "CompReal", std::nullopt, "TestDomain").value_or(0.0);
  EXPECT_TRUE(compReal != 0.0);
}

TEST(JIT, BundleDefaults) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "bundle_default.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  auto A0 = strenv.getStateVar<double>(statePtr.get(), "A", 0, "RootDomain").value_or(0.0);
  auto A1 = strenv.getStateVar<double>(statePtr.get(), "A", 1, "RootDomain").value_or(0.0);

  EXPECT_EQ(A0, 5);
  EXPECT_EQ(A1, 2);

  strenv.invoke("RootDomain_process", args);

  auto out0 = strenv.getStateVar<double>(statePtr.get(), "Out", 0, "RootDomain").value_or(0.0);
  auto out1 = strenv.getStateVar<double>(statePtr.get(), "Out", 1, "RootDomain").value_or(0.0);
  EXPECT_EQ(out0, 10);
  EXPECT_EQ(out1, 13);
}

TEST(JIT, Polymorphism) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "polymorphism_external.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "InInt", 5, std::nullopt, "TestDomain");
  strenv.setStateVar(statePtr.get(), "InReal", 5.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);

  auto compInt = strenv.getStateVar<double>(statePtr.get(), "CompInt", std::nullopt, "TestDomain").value_or(0.0);
  auto compReal = strenv.getStateVar<double>(statePtr.get(), "CompReal", std::nullopt, "TestDomain").value_or(0.0);

  EXPECT_FALSE(compInt != 0.0);
  EXPECT_FALSE(compReal != 0.0);

  strenv.setStateVar(statePtr.get(), "InInt", 0, std::nullopt, "TestDomain");
  strenv.setStateVar(statePtr.get(), "InReal", 0.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);
  
  compInt = strenv.getStateVar<double>(statePtr.get(), "CompInt", std::nullopt, "TestDomain").value_or(0.0);
  compReal = strenv.getStateVar<double>(statePtr.get(), "CompReal", std::nullopt, "TestDomain").value_or(0.0);
  EXPECT_TRUE(compInt != 0.0);
  EXPECT_TRUE(compReal != 0.0);
}

TEST(JIT, Loop) {

  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  int32_t List[20] = {1000, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      1,    1, 1, 1, 1, 1, 1, 1, 1, 100};
  for (int i = 0; i < 20; ++i) {
      strenv.setStateVar(statePtr.get(), "List", List[i], i, "RootDomain");
  }

  strenv.invoke("RootDomain_process", args);
  auto Out = strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0);
  EXPECT_EQ(Out, 1118);
}

TEST(JIT, PortPropertySize) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "port_property_size.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);
  auto Out0 = strenv.getStateVar<double>(statePtr.get(), "Out", 0, "RootDomain").value_or(0.0);
  auto Out1 = strenv.getStateVar<double>(statePtr.get(), "Out", 1, "RootDomain").value_or(0.0);

  EXPECT_EQ(Out0, 32);
  EXPECT_EQ(Out1, 8);
}

TEST(JIT, PlatformFunction) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "platform_module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "InReal", 3.0, std::nullopt, "TestDomain");
  strenv.setStateVar(statePtr.get(), "InInt", 2, std::nullopt, "TestDomain");

  strenv.invoke("TestDomain_process", args);

  auto OutInt = strenv.getStateVar<double>(statePtr.get(), "OutInt", std::nullopt, "TestDomain").value_or(0.0);
  auto OutReal = strenv.getStateVar<double>(statePtr.get(), "OutReal", std::nullopt, "TestDomain").value_or(0.0);
  auto OutWrapped = strenv.getStateVar<double>(statePtr.get(), "OutWrapped", std::nullopt, "TestDomain").value_or(0.0);

  EXPECT_EQ(OutInt, 2);
  EXPECT_DOUBLE_EQ(OutReal, 5.5);
  EXPECT_DOUBLE_EQ(OutWrapped, 5.5);
}

TEST(JIT, ModuleProperties) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module_properties.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 2.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  
  auto out0 = strenv.getStateVar<double>(statePtr.get(), "Out", 0, "RootDomain").value_or(0.0);
  auto out1 = strenv.getStateVar<double>(statePtr.get(), "Out", 1, "RootDomain").value_or(0.0);

  EXPECT_EQ(out0, 6);
  EXPECT_EQ(out1, 8);
}

// Function Standalone ------------------------------------

TEST(JIT, FunctionStandaloneModuleSimple) {
  strd::ASTNode tree;
  tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module_simple.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("ModuleSimple", scope, tree);
  EXPECT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  EXPECT_NE(func, nullptr);

  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  auto *llvmFunc = llvm::dyn_cast<llvm::Function>(v);
  EXPECT_NE(llvmFunc, nullptr);
  EXPECT_FALSE(llvmFunc->isDeclaration());

  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("ModuleSimple");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  EXPECT_NE(Entry, nullptr);

  double in = 3.0;
  double out = 0.0;

  Entry(&out, &in);
  EXPECT_FLOAT_EQ(out, 5.0);
}

TEST(JIT, FunctionStandaloneReactionSimple) {
  strd::ASTNode tree;
  tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "reaction_simple.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("ReactionSimple", scope, tree);
  EXPECT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  EXPECT_NE(func, nullptr);

  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  auto *llvmFunc = llvm::dyn_cast<llvm::Function>(v);
  EXPECT_NE(llvmFunc, nullptr);
  EXPECT_FALSE(llvmFunc->isDeclaration());

  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("ReactionSimple");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  double out = 1.0;

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  Entry(&out);
  EXPECT_FLOAT_EQ(out, 4.0);
}

TEST(JIT, FunctionStandaloneReactionTerminateWhen) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "reaction_terminate_when.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl = strd::ASTQuery::findDeclarationByName(
      "ReactionTerminateWhen", scope, tree);
  EXPECT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  EXPECT_NE(func, nullptr);

  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  auto *llvmFunc = llvm::dyn_cast<llvm::Function>(v);
  EXPECT_NE(llvmFunc, nullptr);
  EXPECT_FALSE(llvmFunc->isDeclaration());

  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("ReactionTerminateWhen");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  int32_t out = 0;
  bool done = false;

  auto *Entry = EntrySym->toPtr<void (*)(int32_t*, bool*)>();
  Entry(&out, &done);
  EXPECT_EQ(out, 2);
  EXPECT_TRUE(done);
}

TEST(JIT, FunctionStandaloneLoopSimple) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "loop_simple.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  scope.push_back({nullptr, {}});
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == strd::AST::Declaration ||
        node->getNodeType() == strd::AST::BundleDeclaration) {
      auto decl = std::static_pointer_cast<strd::DeclarationNode>(node);
      if (decl->getObjectType() == "platformModule") {
        strd::StrideGenerator::generatePlatformFunctionSignature(
            decl, scope.back().second, strenv.mStrideEnv);
      }
    }
  }

  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("LoopSimple", scope, tree);
  EXPECT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  EXPECT_NE(func, nullptr);

  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  auto *llvmFunc = llvm::dyn_cast<llvm::Function>(v);
  EXPECT_NE(llvmFunc, nullptr);
  EXPECT_FALSE(llvmFunc->isDeclaration());

  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("LoopSimple");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  EXPECT_NE(Entry, nullptr);

  int32_t out = 0;
  Entry(&out);
  EXPECT_EQ(out, 11);
}

TEST(JIT, FunctionStandaloneModule) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl = strd::ASTQuery::findDeclarationByName("AddTwo", scope, tree);
  EXPECT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  EXPECT_NE(func, nullptr);

  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  // Ensure it generated an actual function with a body
  auto *llvmFunc = llvm::dyn_cast<llvm::Function>(v);
  EXPECT_NE(llvmFunc, nullptr);
  EXPECT_FALSE(llvmFunc->isDeclaration());

  // Compile and execute the generated standalone function
  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("AddTwo");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  EXPECT_NE(Entry, nullptr);

  double in = 3.0;
  double out = 0.0;

  Entry(&out, &in);
  EXPECT_FLOAT_EQ(out, 5.0);
}

TEST(JIT, FunctionStandaloneReaction) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "reaction.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("Reaction", scope, tree);
  EXPECT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  EXPECT_NE(func, nullptr);

  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  // Ensure it generated an actual function with a body
  auto *llvmFunc = llvm::dyn_cast<llvm::Function>(v);
  EXPECT_NE(llvmFunc, nullptr);
  EXPECT_FALSE(llvmFunc->isDeclaration());

  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("Reaction");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  double out = 1.0;

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  Entry(&out);
  EXPECT_FLOAT_EQ(out, 4.0);
}

TEST(JIT, FunctionStandaloneLoop) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "loop.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  scope.push_back({nullptr, {}});
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == strd::AST::Declaration ||
        node->getNodeType() == strd::AST::BundleDeclaration) {
      auto decl = std::static_pointer_cast<strd::DeclarationNode>(node);
      if (decl->getObjectType() == "platformModule") {
        strd::StrideGenerator::generatePlatformFunctionSignature(
            decl, scope.back().second, strenv.mStrideEnv);
      }
    }
  }

  auto funcDecl = strd::ASTQuery::findDeclarationByName("Add", scope, tree);
  EXPECT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  EXPECT_NE(func, nullptr);

  auto *v = func->codegen(strenv.mStrideEnv);
  EXPECT_NE(v, nullptr);

  // Ensure it generated an actual function with a body
  auto *llvmFunc = llvm::dyn_cast<llvm::Function>(v);
  EXPECT_NE(llvmFunc, nullptr);
  EXPECT_FALSE(llvmFunc->isDeclaration());

  auto ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym = strenv.getFunction("Add");
  EXPECT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(...)>();
  EXPECT_NE(Entry, nullptr);

  int32_t out = 0;
  int32_t in[20];
  for (int i = 0; i < 20; ++i) {
    in[i] = 2; // sum should be 40
  }
  int32_t size = 20;

  Entry(&out, in, size);
  EXPECT_EQ(out, 40);
}

TEST(JIT, ReactionInModule) {

  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction_in_module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 3.0, std::nullopt, "RootDomain");
  strenv.setStateVar(statePtr.get(), "Out", 1.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto Out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(Out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 4.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(Out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 5.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(Out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 6.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(Out, 6.0);

  strenv.setStateVar(statePtr.get(), "In", 10.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv.getStateVar<double>(statePtr.get(), "Out", std::nullopt, "RootDomain").value_or(0.0);
  EXPECT_EQ(Out, 10.0);
}

///  -------------------------------------------------
///  -------------------------------------------------
///  All Above should pass
///  -------------------------------------------------
///  -------------------------------------------------

// TEST(JIT, LoopIterator) {

//   strd::StrideEnvironment strenv;

//   auto ret =
//       strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop_iterator.stride");
//   EXPECT_TRUE(ret);
//   ret = strenv.compileInMemory();
//   EXPECT_TRUE(ret);

//   llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
//       strenv.JIT->lookup("TestDomain_process");
//   if (!EntrySym) {
//     std::cerr << "No entry" << std::endl;
//   }

//   auto *Entry = EntrySym->toPtr<void (*)(...)>();

//   EXPECT_NE(Entry, nullptr);
//   {
//     double List[20] = {1000, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//                        1,    1, 1, 1, 1, 1, 1, 1, 1, 100};
//     double Out = 0;
//     Entry(List, &Out);
//     EXPECT_EQ(Out, 1000);
//   }
//   {
//     double List[20] = {1, 1, 1, 1, 1, 1000, 1, 1, 1, 1,
//                        1, 1, 1, 1, 1, 1,    1, 1, 1, 100};
//     double Out = 0;
//     Entry(List, &Out);
//     EXPECT_EQ(Out, 1000);
//   }
//   {
//     double List[20] = {1, 1, 1, 1, 1, 100, 1, 1, 1, 1,
//                        1, 1, 1, 1, 1, 1,   1, 1, 1, 1000};
//     double Out = 0;
//     Entry(List, &Out);
//     EXPECT_EQ(Out, 1000);
//   }
// }

// TEST(JIT, PackDomainExternalPointer) {

//   strd::StrideEnvironment strenv;
//   strenv.state.setConfiguration(
//       strd::StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL);
//   auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
//   EXPECT_TRUE(ret);
//   ret = strenv.compileInMemory();
//   EXPECT_TRUE(ret);

//   llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
//       strenv.getFunction("TestDomain_process");

//   if (!EntrySym) {
//     std::cerr << "No entry" << std::endl;
//   }

//   auto *Entry = EntrySym->toPtr<void (*)(...)>();

//   auto args = strenv.state.domainArgs["TestDomain"];

//   std::map<std::string, double> doubleArgs;
//   size_t memsize = 0;

//   for (const auto &arg : args) {
//     doubleArgs[arg.name] = 0.0;
//     memsize += sizeof(double *);
//   }
//   uint8_t *domainArgs = (uint8_t *)malloc(memsize);
//   double *in = &doubleArgs["In"];
//   memcpy(domainArgs, &in, sizeof(double *));
//   double *out = &doubleArgs["Out"];
//   memcpy(domainArgs + sizeof(double *), &out, sizeof(double *));

//   doubleArgs["In"] = 1.0;

//   Entry(domainArgs);

//   EXPECT_FLOAT_EQ(doubleArgs["Out"], 1.0);
// }

// TEST(JIT, PackDomainExternal) {

//   strd::StrideEnvironment strenv;
//   strenv.state.setConfiguration(
//       strd::StrideConfig::PACK_DOMAIN_FUNCTION_EXTERNAL);
//   auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
//   EXPECT_TRUE(ret);
//   ret = strenv.compileInMemory();
//   EXPECT_TRUE(ret);

//   llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
//       strenv.getFunction("TestDomain_process");

//   if (!EntrySym) {
//     std::cerr << "No entry" << std::endl;
//   }

//   auto *Entry = EntrySym->toPtr<void (*)(...)>();

//   double in = 1.0;
//   double out = 0.0;

//   struct {
//     double *In;
//     double *Out;
//   } PackedArgs;

//   PackedArgs.In = &in;
//   PackedArgs.Out = &out;

//   Entry(&PackedArgs);

//   EXPECT_FLOAT_EQ(*PackedArgs.Out, 1.0);
// }

TEST(JIT, ChainedCodeGeneratorsInStream) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "chained_code_generators.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  auto getResult = [&]() {
    return strenv.getStateVar(statePtr.get(), "Result", std::nullopt, "RootDomain").value_or(0);
  };

  // Initially Trigger is off, Result is 0
  EXPECT_EQ(getResult(), 0);

  // Tick with Trigger off -> Entire stream (StepOne and StepTwo) must be skipped -> Result remains 0
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 0);

  // Set Trigger to on (true) -> StepOne(factor: 5) outputs 10 -> StepTwo(offset: 10) outputs 20 -> Result = 20
  strenv.setStateVar(statePtr.get(), "Trigger", true, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 20);

  // Turn Trigger back to off (false), reset Result to 999 -> Process tick must NOT execute StepOne or StepTwo
  strenv.setStateVar(statePtr.get(), "Trigger", false, std::nullopt, "RootDomain");
  strenv.setStateVar(statePtr.get(), "Result", 999, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 999);
}

TEST(JIT, TriggeredReactionStreamPipelineWholeStreamInIf) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "triggered_reaction_stream_pipeline.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  auto getResult = [&]() {
    return strenv.getStateVar(statePtr.get(), "Result", std::nullopt, "RootDomain").value_or(0);
  };
  auto getLiteralOnResult = [&]() {
    return strenv.getStateVar(statePtr.get(), "LiteralOnResult", std::nullopt, "RootDomain").value_or(0);
  };

  // Before process: both outputs 0
  EXPECT_EQ(getResult(), 0);
  EXPECT_EQ(getLiteralOnResult(), 0);

  // 1st tick with Trigger off:
  // - 3-stage Trigger pipeline is skipped entirely -> Result remains 0
  // - 2-stage 'on' pipeline executes: (4 * 3) + 8 = 20 -> LiteralOnResult becomes 20
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 0);
  EXPECT_EQ(getLiteralOnResult(), 20);

  // 2nd tick with Trigger on:
  // - 3-stage Trigger pipeline executes: ((5 * 3) + 5) * 2 = 40 -> Result becomes 40
  // - 2-stage 'on' pipeline executes again -> LiteralOnResult remains 20
  strenv.setStateVar(statePtr.get(), "Trigger", true, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 40);
  EXPECT_EQ(getLiteralOnResult(), 20);

  // 3rd tick with Trigger off:
  // - Mutate Result manually to 123
  // - Process tick: Trigger is off -> 3-stage pipeline does not run -> Result stays 123
  strenv.setStateVar(statePtr.get(), "Trigger", false, std::nullopt, "RootDomain");
  strenv.setStateVar(statePtr.get(), "Result", 123, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getResult(), 123);
  EXPECT_EQ(getLiteralOnResult(), 20);
}

TEST(JIT, SwitchDefaults) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "switch_defaults.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);
  strenv.invoke("RootDomain_process", args);

  auto getSw = [&](const std::string &name) -> bool {
    return strenv.getStateVar<bool>(statePtr.get(), name, std::nullopt, "RootDomain").value_or(false);
  };

  // Scalar defaults
  EXPECT_TRUE(getSw("OutScalarOn"));
  EXPECT_FALSE(getSw("OutScalarOff"));

  // Uniform array defaults (SwArr[3] { default: on })
  EXPECT_TRUE(getSw("OutArrayUniform0"));
  EXPECT_TRUE(getSw("OutArrayUniform1"));
  EXPECT_TRUE(getSw("OutArrayUniform2"));

  // List array defaults (SwList[3] { default: [on, off, on] })
  EXPECT_TRUE(getSw("OutArrayList0"));
  EXPECT_FALSE(getSw("OutArrayList1"));
  EXPECT_TRUE(getSw("OutArrayList2"));
}

TEST(JIT, ChainedFunctionsInStreamPipeline) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "chained_functions_in_stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  auto getOutput = [&]() {
    return strenv.getStateVar(statePtr.get(), "OutputValue", std::nullopt, "RootDomain").value_or(0);
  };

  // Default InputValue = 10:
  // (10 * 3) + 7 - 2 = 30 + 7 - 2 = 35
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getOutput(), 35);

  // Update InputValue = 4:
  // (4 * 3) + 7 - 2 = 12 + 7 - 2 = 17
  strenv.setStateVar(statePtr.get(), "InputValue", 4, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getOutput(), 17);

  // Update InputValue = 0:
  // (0 * 3) + 7 - 2 = 0 + 7 - 2 = 5
  strenv.setStateVar(statePtr.get(), "InputValue", 0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(getOutput(), 5);
}



