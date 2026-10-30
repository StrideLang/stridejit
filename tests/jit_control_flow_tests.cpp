#include "gtest/gtest.h"
#include <cstdint>
#include <filesystem>

#include "stride/stridegen/codeanalysis.hpp"
#include "stride/stridegen/codevalidator.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include "llvm/IR/Function.h"

namespace {

TEST(JITControlFlowTest, ReactionBasic) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto Out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "TestDomain")
                 .value_or(0.0);
  EXPECT_EQ(Out, 3);
}

TEST(JITControlFlowTest, ReactionSingleStream) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "reaction_single_stream.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_process", args);
  auto Out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "TestDomain")
                 .value_or(0.0);
  EXPECT_EQ(Out, 3);
}

TEST(JITControlFlowTest, ReactionCondition) {
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
  auto Out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "TestDomain")
                 .value_or(0.0);
  EXPECT_EQ(Out, 0);

  strenv.invoke("TestDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "TestDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 3);
}

TEST(JITControlFlowTest, ReactionTerminateWhen) {
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
  auto Out = strenv
                 .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                       "TestDomain")
                 .value_or(0);
  EXPECT_EQ(Out, 2);

  auto Done =
      strenv
          .getStateVar<bool>(statePtr.get(), "Done", std::nullopt, "TestDomain")
          .value_or(false);
  EXPECT_TRUE(Done);
}

TEST(JITControlFlowTest, ReactionInModule) {
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
  auto Out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "RootDomain")
                 .value_or(0.0);
  EXPECT_EQ(Out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 4.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 5.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 6.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 6.0);

  strenv.setStateVar(statePtr.get(), "In", 10.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  Out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_EQ(Out, 10.0);
}

TEST(JITControlFlowTest, LoopBasic) {
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
  auto Out = strenv
                 .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                       "RootDomain")
                 .value_or(0);
  EXPECT_EQ(Out, 1118);
}

// A loop's output array sized by OutputPort.size takes the size of the array
// the call writes to, and the loop fills it in place.
TEST(JITControlFlowTest, LoopOutputPortSize) {
  strd::StrideEnvironment strenv;

  ASSERT_TRUE(
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop_output_size.stride"));
  ASSERT_TRUE(strenv.compileInMemory());

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_process", args);
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(strenv
                  .getStateVar<int32_t>(statePtr.get(), "Table", i,
                                        "RootDomain")
                  .value_or(-1),
              3 * i)
        << "Table[" << i << "]";
  }
}

// A constant without a value is a writable global: a loop fills it and later
// code reads it
TEST(JITControlFlowTest, ConstantFilledByLoop) {
  strd::StrideEnvironment strenv;

  ASSERT_TRUE(
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "constant_fill.stride"));
  ASSERT_TRUE(strenv.compileInMemory());

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(
      strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                  "RootDomain")
          .value_or(-1),
      12);
}

// Output port sizes in a loop sized by both ports, into a slice and a
// single signal, in a module, and with an output default
TEST(JITControlFlowTest, LoopOutputPortSizeCases) {
  strd::StrideEnvironment strenv;

  ASSERT_TRUE(
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop_output_cases.stride"));
  ASSERT_TRUE(strenv.compileInMemory());

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_init", args);
  strenv.invoke("RootDomain_process", args);
  auto value = [&](const char *name, std::optional<int> index) {
    return strenv.getStateVar<int32_t>(statePtr.get(), name, index,
                                       "RootDomain")
        .value_or(-100);
  };
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(value("Doubled", i), 6 * i) << "Doubled[" << i << "]";
  }
  // Slice[2:4] is filled as a 3 entry table; the rest keep their default
  const int slice[6] = {-1, -1, 0, 3, 6, -1};
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(value("Slice", i), slice[i]) << "Slice[" << i << "]";
  }
  EXPECT_EQ(value("Scalar", std::nullopt), 0);
  EXPECT_EQ(value("Sizes", 0), 5);
  // Entries a loop does not write keep their values
  EXPECT_EQ(value("Partial", 0), -1);
  EXPECT_EQ(value("Partial", 1), 5);
  EXPECT_EQ(value("Partial", 2), -1);
}

// An output sized by OutputPort.size must write to a declared array,
// element or bundle: into another call that sizes its input from what feeds
// it, or into nothing, the size is unknown. An input sized by InputPort.size
// must be fed.
TEST(JITControlFlowTest, UnsizedPorts) {
  for (const char *fixture :
       {"loop_output_chained.stride", "loop_output_unwritten.stride"}) {
    auto tree = strd::AST::parseFile(
        (std::string(STRIDEJIT_TESTS_SOURCE_DIR) + fixture).c_str());
    ASSERT_NE(tree, nullptr) << fixture;
    strd::StrideEnvironment strenv;
    strenv.prepareTree(tree);
    auto errors = strd::CodeValidator::findUnsizedPorts(tree);
    ASSERT_EQ(errors.size(), 1u) << fixture;
    EXPECT_NE(errors[0].getErrorText().find(
                  "'Fill()' takes the size of its output from what it writes "
                  "to (OutputPort.size)"),
              std::string::npos)
        << errors[0].getErrorText();
  }
  // The same for an input sized by what feeds it
  {
    auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                     "loop_input_unfed.stride");
    ASSERT_NE(tree, nullptr);
    strd::StrideEnvironment strenv;
    strenv.prepareTree(tree);
    auto errors = strd::CodeValidator::findUnsizedPorts(tree);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].getErrorText().find(
                  "'Double()' takes the size of its input from what feeds it "
                  "(InputPort.size), so it must be fed"),
              std::string::npos)
        << errors[0].getErrorText();
  }
  // The working cases have none
  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "loop_output_cases.stride");
  ASSERT_NE(tree, nullptr);
  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  for (auto &error : strd::CodeValidator::findUnsizedPorts(tree)) {
    ADD_FAILURE() << error.getErrorText();
  }
}

// A reaction's input is the trigger that runs it: no fixture declares a
// reaction with a main input port
TEST(JITControlFlowTest, NoReactionDeclaresAMainInput) {
  for (const auto &entry :
       std::filesystem::directory_iterator(STRIDEJIT_TESTS_SOURCE_DIR)) {
    if (entry.path().extension() != ".stride") {
      continue;
    }
    auto tree = strd::AST::parseFile(entry.path().string().c_str());
    if (!tree) {
      continue; // Fixtures of parse errors
    }
    for (auto &error : strd::CodeValidator::findReactionMainInputs(tree)) {
      ADD_FAILURE() << entry.path().filename().string() << ": "
                    << error.getErrorText();
    }
  }
}

// A loop fills a switch array sized by what it writes to, whole or a slice
TEST(JITControlFlowTest, LoopOutputSwitches) {
  strd::StrideEnvironment strenv;
  ASSERT_TRUE(strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                                "loop_output_switches.stride"));
  ASSERT_TRUE(strenv.compileInMemory());

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_init", args);
  strenv.invoke("RootDomain_process", args);
  auto flag = [&](const char *name, int i) {
    return strenv.getStateVar<int8_t>(statePtr.get(), name, i, "RootDomain")
        .value_or(-1);
  };
  const int flags[4] = {0, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(flag("Flags", i), flags[i]) << "Flags[" << i << "]";
  }
  // Slice[1:4] is a 4 entry table: its entry 1 is Slice[2]
  const int slice[6] = {0, 0, 1, 0, 0, 0};
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(flag("Slice", i), slice[i]) << "Slice[" << i << "]";
  }  // A switch written into the output element, rather than a call's result
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(flag("Copied", i), flags[i]) << "Copied[" << i << "]";
  }
}

// The same loop called from a state's updateGuard
TEST(JITControlFlowTest, LoopOutputInStateMachine) {
  strd::StrideEnvironment strenv;
  ASSERT_TRUE(strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                                "loop_output_in_state.stride"));
  ASSERT_TRUE(strenv.compileInMemory());

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_init", args);
  strenv.invoke("RootDomain_process", args);
  auto flag = [&](const char *name, int i) {
    return strenv.getStateVar<int8_t>(statePtr.get(), name, i, "RootDomain")
        .value_or(-1);
  };
  const int flags[4] = {0, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(flag("Flags", i), flags[i]) << "Flags[" << i << "]";
  }
  const int slice[6] = {0, 0, 1, 0, 0, 0};
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(flag("Slice", i), slice[i]) << "Slice[" << i << "]";
  }
}

// A loop that reads a domain array and writes its output array, called from
// a state's updateGuard
TEST(JITControlFlowTest, LoopOutputReadsDomainArray) {
  strd::StrideEnvironment strenv;
  ASSERT_TRUE(strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                                "loop_output_reads_domain.stride"));
  ASSERT_TRUE(strenv.compileInMemory());

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_init", args);
  const int flags[4] = {0, 1, 0, 1};
  for (int i = 0; i < 4; ++i) {
    strenv.setStateVar(statePtr.get(), "Source", static_cast<bool>(flags[i]), i,
                       "RootDomain");
  }
  strenv.invoke("RootDomain_process", args);
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(strenv.getStateVar<int8_t>(statePtr.get(), "Flags", i,
                                         "RootDomain")
                  .value_or(-1),
              flags[i])
        << "Flags[" << i << "]";
  }
}

// A call's properties are matched to the callee's ports by name, in any order
TEST(JITControlFlowTest, PropertiesMatchedByName) {
  strd::StrideEnvironment strenv;
  ASSERT_TRUE(
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "property_order.stride"));
  ASSERT_TRUE(strenv.compileInMemory());

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                        "RootDomain")
                .value_or(-100),
            9);
  EXPECT_EQ(strenv.getStateVar<int32_t>(statePtr.get(), "OutInOrder",
                                        std::nullopt, "RootDomain")
                .value_or(-100),
            9);
}

TEST(JITControlFlowTest, StandaloneReactionSimple) {
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

TEST(JITControlFlowTest, StandaloneReactionTerminateWhen) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "reaction_terminate_when.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  auto funcDecl = strd::ASTQuery::findDeclarationByName("ReactionTerminateWhen",
                                                        scope, tree);
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

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, bool *)>();
  Entry(&out, &done);
  EXPECT_EQ(out, 2);
  EXPECT_TRUE(done);
}

TEST(JITControlFlowTest, StandaloneReaction) {
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

  // Compile and execute the generated standalone function
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

TEST(JITControlFlowTest, StandaloneLoopSimple) {
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
        node->getNodeType() == strd::AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<strd::DeclarationNode>(node);
      if (decl->getEntityType() == "platformModule") {
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

TEST(JITControlFlowTest, StandaloneLoopTerminateWhen) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "loop_terminate_when.stride");
  ASSERT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.mStrideEnv.m_intanceTree =
      strd::CodeAnalysis::getStateStructInformation({}, tree);

  strd::ScopeStack scope;
  scope.push_back({nullptr, {}});
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == strd::AST::Declaration ||
        node->getNodeType() == strd::AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<strd::DeclarationNode>(node);
      if (decl->getEntityType() == "platformModule") {
        strd::StrideGenerator::generatePlatformFunctionSignature(
            decl, scope.back().second, strenv.mStrideEnv);
      }
    }
  }

  auto funcDecl =
      strd::ASTQuery::findDeclarationByName("LoopTerminateWhen", scope, tree);
  ASSERT_NE(funcDecl, nullptr);

  auto func = strd::StrideGenerator::createFunctionDeclaration(
      funcDecl, nullptr, tree, &scope, strenv.mStrideEnv);
  ASSERT_NE(func, nullptr);
  ASSERT_NE(func->codegen(strenv.mStrideEnv), nullptr);
  ASSERT_TRUE(strenv.compileInMemory());

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("LoopTerminateWhen");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  int32_t out = 0;
  EntrySym->toPtr<void (*)(int32_t *)>()(&out);
  EXPECT_EQ(out, 10);
}

TEST(JITControlFlowTest, StandaloneLoop) {
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
        node->getNodeType() == strd::AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<strd::DeclarationNode>(node);
      if (decl->getEntityType() == "platformModule") {
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

} // namespace
