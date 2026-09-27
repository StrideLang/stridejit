#include "gtest/gtest.h"

// stridejit
#include "stride/stridejit/strideenvironment.hpp"

TEST(LogicalComparisonsTest, LogicalOperationsJIT) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "logical_comparisons.stride");
  ASSERT_TRUE(ret);
  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  auto EntrySym = strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(!!EntrySym);

  // Function signature: void RootDomain_process(bool* InA, bool* InB, bool* OutAnd, bool* OutOr, bool* OutNot, bool* OutXor, bool* OutNand, bool* OutNor)
  auto *Entry = EntrySym->toPtr<void (*)(bool*, bool*, bool*, bool*, bool*, bool*, bool*, bool*)>();

  // Test case 1: InA = true, InB = false
  {
    bool inA = true;
    bool inB = false;
    bool outAnd = false;
    bool outOr = false;
    bool outNot = false;
    bool outXor = false;
    bool outNand = false;
    bool outNor = false;

    Entry(&inA, &inB, &outAnd, &outOr, &outNot, &outXor, &outNand, &outNor);

    EXPECT_FALSE(outAnd);
    EXPECT_TRUE(outOr);
    EXPECT_FALSE(outNot);
    EXPECT_TRUE(outXor);
    EXPECT_TRUE(outNand);
    EXPECT_FALSE(outNor);
  }

  // Test case 2: InA = true, InB = true
  {
    bool inA = true;
    bool inB = true;
    bool outAnd = false;
    bool outOr = false;
    bool outNot = false;
    bool outXor = false;
    bool outNand = false;
    bool outNor = false;

    Entry(&inA, &inB, &outAnd, &outOr, &outNot, &outXor, &outNand, &outNor);

    EXPECT_TRUE(outAnd);
    EXPECT_TRUE(outOr);
    EXPECT_FALSE(outNot);
    EXPECT_FALSE(outXor);
    EXPECT_FALSE(outNand);
    EXPECT_FALSE(outNor);
  }

  // Test case 3: InA = false, InB = false
  {
    bool inA = false;
    bool inB = false;
    bool outAnd = false;
    bool outOr = false;
    bool outNot = false;
    bool outXor = false;
    bool outNand = false;
    bool outNor = false;

    Entry(&inA, &inB, &outAnd, &outOr, &outNot, &outXor, &outNand, &outNor);

    EXPECT_FALSE(outAnd);
    EXPECT_FALSE(outOr);
    EXPECT_TRUE(outNot);
    EXPECT_FALSE(outXor);
    EXPECT_TRUE(outNand);
    EXPECT_TRUE(outNor);
  }

  // Test case 4: InA = false, InB = true
  {
    bool inA = false;
    bool inB = true;
    bool outAnd = false;
    bool outOr = false;
    bool outNot = false;
    bool outXor = false;
    bool outNand = false;
    bool outNor = false;

    Entry(&inA, &inB, &outAnd, &outOr, &outNot, &outXor, &outNand, &outNor);

    EXPECT_FALSE(outAnd);
    EXPECT_TRUE(outOr);
    EXPECT_TRUE(outNot);
    EXPECT_TRUE(outXor);
    EXPECT_TRUE(outNand);
    EXPECT_FALSE(outNor);
  }
}
