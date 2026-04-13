#include "gtest/gtest.h"

// stridejit
#include "stride/codegen/coderesolver.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// stride
#include "stride/codegen/codeanalysis.hpp"
#include "stride/utils/astfunctions.h"

// llvm
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/TargetSelect.h"

using namespace llvm;

TEST(JIT, Create) {

  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strd::ScopeStack scope;

  strd::CodeResolver resolver{tree, ""};
  resolver.process();

  strd::StrideGenerator::generateCode(tree, scope, strenv.state);
  strenv.state.TheModule->dump();

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
