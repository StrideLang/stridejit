#include "gtest/gtest.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "stride/parser/ast.h"
#include "stride/stridejit/numberexprast.hpp"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/cachemanager.h"
#include "testcacheguard.hpp"

#include "llvm/Support/TargetSelect.h"

namespace {

// ============================================================================
// StrideEnvironment Initialization Tests
// ============================================================================

TEST(StrideEnvironmentTest, DefaultConstruction) {
  strd::StrideEnvironment env;

  EXPECT_NE(env.mStrideEnv.TheContext, nullptr);
  EXPECT_NE(env.mStrideEnv.TheModule, nullptr);
}

TEST(StrideEnvironmentTest, ConstructionWithStrideRoot) {
  strd::StrideEnvironment env("./test_root");

  EXPECT_NE(env.mStrideEnv.TheContext, nullptr);
}

TEST(StrideEnvironmentTest, IncludePathsManagement) {
  strd::StrideEnvironment env;
  EXPECT_TRUE(env.getIncludePaths().empty());

  env.addIncludePath("/path/one");
  env.addIncludePath("/path/two");
  EXPECT_EQ(env.getIncludePaths().size(), 2);
  EXPECT_EQ(env.getIncludePaths()[0], "/path/one");
  EXPECT_EQ(env.getIncludePaths()[1], "/path/two");

  env.setIncludePaths({"/custom/path"});
  EXPECT_EQ(env.getIncludePaths().size(), 1);
  EXPECT_EQ(env.getIncludePaths()[0], "/custom/path");
}

// ============================================================================
// IR Generation Tests
// ============================================================================

TEST(StrideEnvironmentTest, GenerateIRFromInvalidPath) {
  strd::StrideEnvironment env;

  bool ret = env.generateIr("nonexistent_file_xyz.stride");
  EXPECT_FALSE(ret);
}

TEST(StrideEnvironmentTest, ProcessTreeWithValidAST) {
  strd::StrideEnvironment env;

  // Create a minimal valid AST
  strd::ASTNode tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");

  if (tree != nullptr) {
    EXPECT_NO_THROW(env.prepareTree(tree));
  }
}

// ============================================================================
// JIT Compilation Tests
// ============================================================================

TEST(StrideEnvironmentTest, InitializeJIT) {
  strd::StrideEnvironment env;

  // Initialize JIT - should not crash
  env.initializeJIT();
}

TEST(StrideEnvironmentTest, JITIsNull) {
  strd::StrideEnvironment env;

  // JIT is initially null
  EXPECT_EQ(env.JIT, nullptr);
}

// ============================================================================
// Compiler State Access Tests
// ============================================================================

TEST(StrideEnvironmentTest, AccessCompilerState) {
  strd::StrideEnvironment env;

  EXPECT_NE(env.mStrideEnv.TheContext, nullptr);
  EXPECT_NE(env.mStrideEnv.TheModule, nullptr);
  EXPECT_NE(env.mStrideEnv.Builder, nullptr);
}

// ============================================================================
// Multi-Instance Tests
// ============================================================================

TEST(StrideEnvironmentTest, MultipleInstances) {
  strd::StrideEnvironment env1;
  strd::StrideEnvironment env2;

  EXPECT_NE(env1.mStrideEnv.TheContext.get(), env2.mStrideEnv.TheContext.get());
  EXPECT_NE(env1.mStrideEnv.TheModule.get(), env2.mStrideEnv.TheModule.get());
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(StrideEnvironmentTest, GetFunctionBeforeCompilation) {
  strd::StrideEnvironment env;

  auto result = env.getFunction("SomeFunction");

  // Should return error since JIT is not initialized
  EXPECT_FALSE((bool)result);
  llvm::consumeError(result.takeError());
}

TEST(StrideEnvironmentTest, EmitCHeaderAndObjectFile) {
  strd::StrideEnvironment env;
  bool ok = env.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  ASSERT_TRUE(ok);

  std::string headerStr = env.generateCHeaderString();
  EXPECT_FALSE(headerStr.empty());
  EXPECT_NE(headerStr.find("#ifndef"), std::string::npos);
  EXPECT_NE(headerStr.find("extern \"C\""), std::string::npos);
  EXPECT_NE(headerStr.find("RootDomain_process"), std::string::npos);

  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stride_test_aot";
  std::filesystem::create_directories(tempDir);

  std::string headerPath = (tempDir / "passthru.h").string();
  EXPECT_TRUE(env.emitCHeader(headerPath));
  EXPECT_TRUE(std::filesystem::exists(headerPath));

  std::string objPathHost = (tempDir / "passthru_host.o").string();
  EXPECT_TRUE(env.emitObjectFile(objPathHost));
  EXPECT_TRUE(std::filesystem::exists(objPathHost));
  EXPECT_GT(std::filesystem::file_size(objPathHost), 0);

  // Cross-compile to Cortex-M0+ (RP2040)
  std::string objPathArm = (tempDir / "passthru_arm.o").string();
  EXPECT_TRUE(
      env.emitObjectFile(objPathArm, "thumbv6m-none-eabi", "cortex-m0plus"));
  EXPECT_TRUE(std::filesystem::exists(objPathArm));
  EXPECT_GT(std::filesystem::file_size(objPathArm), 0);

  std::filesystem::remove_all(tempDir);
}

// ============================================================================
// AST Caching Performance Benchmark Tests
// ============================================================================

TEST(StrideEnvironmentTest, BenchmarkASTCacheVsParse) {
  strd::test::ScopedCacheEnable cacheGuard;

  std::filesystem::path fixturePath =
      STRIDEJIT_TESTS_SOURCE_DIR "module.stride";
  ASSERT_TRUE(std::filesystem::exists(fixturePath));

  std::filesystem::path tempDir = cacheGuard.tempDir() / "stride_bench_ast";
  std::filesystem::create_directories(tempDir);
  std::filesystem::path largeFixture = tempDir / "large_benchmark.stride";

  {
    std::ofstream out(largeFixture);
    out << "_domainDefinition BenchmarkDomain {\n"
        << "  framework: _JitFramework\n"
        << "  rate: -1\n"
        << "  outputs: [\n"
        << "    signal Out { domain: BenchmarkDomain type: Real }\n"
        << "  ]\n"
        << "  domainFunctionCode: ''\n"
        << "}\n\n";

    for (int m = 0; m < 20; ++m) {
      out << "module Mod" << m << " {\n"
          << "  ports: [\n"
          << "    mainInputPort InP { block: InB }\n"
          << "    mainOutputPort OutP { block: OutB }\n"
          << "  ]\n"
          << "  blocks: [\n"
          << "    signal InB { domain: OutP.domain type: OutP.type }\n"
          << "    signal OutB { domain: OutP.domain type: OutP.type }\n"
          << "  ]\n"
          << "  streams: [\n"
          << "    InB + " << (m + 1.0) << " >> OutB;\n"
          << "  ]\n"
          << "}\n\n";
    }

    out << "3.0 >> Mod0()";
    for (int m = 1; m < 20; ++m) {
      out << " >> Mod" << m << "()";
    }
    out << " >> Out;\n";
  }

  auto runBenchmarkForFile = [](const std::filesystem::path &filePath,
                                const std::string &label, int iterations) {
    // 1. Warm-up and populate the cache
    strd::CacheManager::instance().setEnabled(true);
    strd::CacheManager::instance().clearCache();

    {
      strd::StrideEnvironment env;
      ASSERT_TRUE(env.generateIr(filePath.generic_string()));
    }

    // 2. Measure Cold Runs (pure AST parse + prepareTree + preprocess)
    auto tStartCold = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
      auto tree = strd::AST::parseFile(filePath.generic_string().c_str());
      ASSERT_NE(tree, nullptr);
      strd::StrideEnvironment env;
      env.prepareTree(tree);
      strd::ScopeStack globalScope;
      globalScope.push_back({tree, {}});
      strd::ASTFunctions::preprocess(tree, &globalScope);
    }
    auto tEndCold = std::chrono::high_resolution_clock::now();
    double coldAvgUs =
        std::chrono::duration<double, std::micro>(tEndCold - tStartCold)
            .count() /
        iterations;

    // 3. Measure Warm Runs (CacheManager loadCachedAST from .scache)
    auto tStartWarm = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
      auto cached = strd::CacheManager::instance().loadCachedAST(
          filePath, {}, "prepared_JIT");
      ASSERT_NE(cached, nullptr);
    }
    auto tEndWarm = std::chrono::high_resolution_clock::now();
    double warmAvgUs =
        std::chrono::duration<double, std::micro>(tEndWarm - tStartWarm)
            .count() /
        iterations;

    double speedup = coldAvgUs / (warmAvgUs > 0.0 ? warmAvgUs : 0.001);

    std::cout << "\n======================================================\n";
    std::cout << " [AST CACHE BENCHMARK] " << label << " (" << iterations
              << " iterations)\n";
    std::cout << " File: " << filePath.filename().string() << "\n";
    std::cout << " Cold AST Time (Parse + Prepare + Preprocess): "
              << (coldAvgUs >= 1000.0 ? coldAvgUs / 1000.0 : coldAvgUs)
              << (coldAvgUs >= 1000.0 ? " ms" : " us") << " (avg)\n";
    std::cout << " Warm AST Time (Cached .scache Deserialization):"
              << (warmAvgUs >= 1000.0 ? warmAvgUs / 1000.0 : warmAvgUs)
              << (warmAvgUs >= 1000.0 ? " ms" : " us") << " (avg)\n";
    std::cout << " AST Phase Speedup: " << speedup << "x faster\n";
    std::cout << " Time saved per file: " << (coldAvgUs - warmAvgUs) / 1000.0
              << " ms\n";
    std::cout << "======================================================\n"
              << std::endl;
  };

  runBenchmarkForFile(fixturePath, "Standard Fixture (module.stride)", 10);
  runBenchmarkForFile(largeFixture, "Large Pipeline (20 Modules, 40 Signals)",
                      5);
}

TEST(StrideEnvironmentTest, CompiledObjectAndBitcodeCaching) {
  strd::test::ScopedCacheEnable cacheGuard;

  std::filesystem::path tempDir =
      cacheGuard.tempDir() / "stride_obj_cache_test";
  std::filesystem::create_directories(tempDir);
  std::filesystem::path sourceFile = tempDir / "AddTest.stride";

  {
    std::ofstream out(sourceFile);
    out << "_domainDefinition TestDomain {\n"
        << "  framework: _JitFramework\n"
        << "  rate: -1\n"
        << "  outputs: [\n"
        << "    signal Out { domain: TestDomain type: Real }\n"
        << "  ]\n"
        << "  domainFunctionCode: ''\n"
        << "}\n\n"
        << "5.0 + 7.0 >> Out;\n";
  }

  // 1. First compile: Cold compile (cache miss, saves .obj and .bc)
  auto t0 = std::chrono::high_resolution_clock::now();
  {
    strd::StrideEnvironment env;
    ASSERT_TRUE(env.generateIr(sourceFile.generic_string()));
    ASSERT_TRUE(env.compileInMemory());

    auto sym = env.getFunction("TestDomain_process");
    ASSERT_TRUE((bool)sym);
    auto *func = sym->toPtr<void (*)(double *)>();
    double outVal = 0.0;
    func(&outVal);
    EXPECT_DOUBLE_EQ(outVal, 12.0);
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  double coldMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

  // Verify .obj and .bc files were written to .strdcache
  auto cacheDir =
      strd::CacheManager::instance().resolveCacheDirForSource(sourceFile);
  bool foundObj = false, foundBc = false;
  size_t objSize = 0, bcSize = 0;
  std::string objFileName, bcFileName;
  for (const auto &entry : std::filesystem::directory_iterator(cacheDir)) {
    if (entry.path().extension() == ".obj") {
      foundObj = true;
      objSize = entry.file_size();
      objFileName = entry.path().filename().string();
    } else if (entry.path().extension() == ".bc") {
      foundBc = true;
      bcSize = entry.file_size();
      bcFileName = entry.path().filename().string();
    }
  }

  EXPECT_TRUE(foundObj);
  EXPECT_TRUE(foundBc);
  EXPECT_GT(objSize, 0u);
  EXPECT_GT(bcSize, 0u);

  // 2. Second compile: Warm compile (hits .scache AND .obj)
  auto t2 = std::chrono::high_resolution_clock::now();
  {
    strd::StrideEnvironment env;
    ASSERT_TRUE(env.generateIr(sourceFile.generic_string()));
    ASSERT_TRUE(env.compileInMemory());

    auto sym = env.getFunction("TestDomain_process");
    ASSERT_TRUE((bool)sym);
    auto *func = sym->toPtr<void (*)(double *)>();
    double outVal = 0.0;
    func(&outVal);
    EXPECT_DOUBLE_EQ(outVal, 12.0);
  }
  auto t3 = std::chrono::high_resolution_clock::now();
  double warmMs = std::chrono::duration<double, std::milli>(t3 - t2).count();

  std::cout << "\n======================================================\n";
  std::cout << " [COMPILED OBJECT CACHE TEST & BENCHMARK]\n";
  std::cout << " .obj Cache File: " << objFileName << " (" << objSize
            << " bytes)\n";
  std::cout << " .bc  Cache File: " << bcFileName << " (" << bcSize
            << " bytes)\n";
  std::cout << " Cold Compile Time (Full IR + O2 Opt + MC Gen): " << coldMs
            << " ms\n";
  std::cout << " Warm Compile Time (Cached AST + Cached .obj):   " << warmMs
            << " ms\n";
  std::cout << " End-to-End Speedup:                           "
            << (coldMs / (warmMs > 0.0 ? warmMs : 0.001)) << "x faster\n";
  std::cout << "======================================================\n"
            << std::endl;
}

} // namespace
