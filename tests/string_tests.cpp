#include "gtest/gtest.h"

#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/strideruntime_string.h"

using namespace strd;

TEST(Strings, RuntimeHelpersDirect) {
  // Substring
  StrideStringView src{"Hello World", 11};
  auto sub1 = __stride_string_substring(src, 0, 5);
  EXPECT_EQ(sub1.toString(), "Hello");

  auto sub2 = __stride_string_substring(src, 6, -1);
  EXPECT_EQ(sub2.toString(), "World");

  auto subOutOfBounds = __stride_string_substring(src, 20, 5);
  EXPECT_EQ(subOutOfBounds.length, 0);

  // Trim
  StrideStringView padded{"  \tfoo bar \r\n", 13};
  auto trimmed = __stride_string_trim(padded, StrideStringView{" \t\r\n", 4});
  EXPECT_EQ(trimmed.toString(), "foo bar");

  // Find
  EXPECT_EQ(__stride_string_find(src, StrideStringView{"World", 5}, 0), 6);
  EXPECT_EQ(__stride_string_find(src, StrideStringView{"XYZ", 3}, 0), -1);

  // FindLast
  StrideStringView path{"/path/to/file.stride", 20};
  EXPECT_EQ(__stride_string_find_last(path, StrideStringView{".", 1}), 13);

  // Contains
  EXPECT_TRUE(__stride_string_contains(src, StrideStringView{"lo Wo", 5}));
  EXPECT_FALSE(__stride_string_contains(src, StrideStringView{"xyz", 3}));

  // StartsWith / EndsWith
  EXPECT_TRUE(__stride_string_starts_with(src, StrideStringView{"Hello", 5}));
  EXPECT_FALSE(__stride_string_starts_with(src, StrideStringView{"World", 5}));
  EXPECT_TRUE(__stride_string_ends_with(src, StrideStringView{"World", 5}));
  EXPECT_FALSE(__stride_string_ends_with(src, StrideStringView{"Hello", 5}));

  // Equal / Compare
  EXPECT_TRUE(__stride_string_equal(src, StrideStringView{"Hello World", 11}));
  EXPECT_FALSE(__stride_string_equal(src, StrideStringView{"Hello", 5}));
  EXPECT_EQ(__stride_string_compare(src, StrideStringView{"Hello World", 11}), 0);
  EXPECT_LT(__stride_string_compare(StrideStringView{"abc", 3}, StrideStringView{"def", 3}), 0);

  // Length & IsEmpty
  EXPECT_EQ(__stride_string_length(src), 11);
  EXPECT_FALSE(__stride_string_is_empty(src));
  EXPECT_TRUE(__stride_string_is_empty(StrideStringView{"", 0}));

  // ParseInt / ParseReal
  EXPECT_EQ(__stride_string_parse_int(StrideStringView{"12345", 5}, 0), 12345);
  EXPECT_EQ(__stride_string_parse_int(StrideStringView{"invalid", 7}, 99), 99);
  EXPECT_DOUBLE_EQ(__stride_string_parse_real(StrideStringView{"3.14159", 7}, 0.0), 3.14159);
}

TEST(Strings, TokenizeWithOverflow) {
  StrideStringView src{"alpha,beta,gamma,delta,epsilon", 30};
  StrideStringView delim{",", 1};
  StrideStringView tokens[3];
  bool overflow = false;

  int64_t count = __stride_string_tokenize(src, delim, tokens, 3, &overflow);
  EXPECT_EQ(count, 5);
  EXPECT_TRUE(overflow);
  EXPECT_EQ(tokens[0].toString(), "alpha");
  EXPECT_EQ(tokens[1].toString(), "beta");
  EXPECT_EQ(tokens[2].toString(), "gamma");

  // Non-overflow case
  StrideStringView tokensFull[10];
  overflow = false;
  count = __stride_string_tokenize(src, delim, tokensFull, 10, &overflow);
  EXPECT_EQ(count, 5);
  EXPECT_FALSE(overflow);
  EXPECT_EQ(tokensFull[0].toString(), "alpha");
  EXPECT_EQ(tokensFull[1].toString(), "beta");
  EXPECT_EQ(tokensFull[2].toString(), "gamma");
  EXPECT_EQ(tokensFull[3].toString(), "delta");
  EXPECT_EQ(tokensFull[4].toString(), "epsilon");
}

TEST(Strings, JITCompilationAndExecution) {
  StrideEnvironment strenv;

  bool ok = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "StringsTest.stride");
  ASSERT_TRUE(ok);

  ok = strenv.compileInMemory();
  ASSERT_TRUE(ok);

  // According to Rule 2 of AGENTS.md:
  // RootDomain_process takes all declared inputs followed by all declared outputs by pointer.
  // Inputs:
  //   InStr (StrideStringView*)
  //   PaddedStr (StrideStringView*)
  // Outputs:
  //   OutLiteral (StrideStringView*)
  //   OutSub (StrideStringView*)
  //   OutTrim (StrideStringView*)
  //   OutPos (int32_t*)
  //   OutStarts (bool*)
  //   OutEnds (bool*)
  //   OutContains (bool*)
  //   OutEqual (bool*)
  //   OutCmp (int32_t*)
  //   OutLen (int32_t*)
  //   OutEmpty (bool*)
  //   OutInt (int32_t*)
  //   OutReal (double*)
  //   OutArrayElem (StrideStringView*)

  StrideStringView inStr{"Hello World", 11};
  StrideStringView paddedStr{"   Trimmed   ", 13};

  StrideStringView outLiteral{};
  StrideStringView outSub{};
  StrideStringView outTrim{};
  int32_t outPos = 0;
  bool outStarts = false;
  bool outEnds = false;
  bool outContains = false;
  bool outEqual = false;
  int32_t outCmp = -999;
  int32_t outLen = 0;
  bool outEmpty = true;
  int32_t outInt = 0;
  double outReal = 0.0;
  StrideStringView outArrayElem{};

  void *args[] = {
      &inStr,
      &paddedStr,
      &outLiteral,
      &outSub,
      &outTrim,
      &outPos,
      &outStarts,
      &outEnds,
      &outContains,
      &outEqual,
      &outCmp,
      &outLen,
      &outEmpty,
      &outInt,
      &outReal,
      &outArrayElem
  };

  int32_t res = strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(res, 0);

  // 1. Literal assignment
  EXPECT_EQ(outLiteral.toString(), "Constant Message");

  // 2. Substring [0, 5]
  EXPECT_EQ(outSub.toString(), "Hello");

  // 3. Trim
  EXPECT_EQ(outTrim.toString(), "Trimmed");

  // 4. Find "World"
  EXPECT_EQ(outPos, 6);

  // 5. StartsWith "Hello"
  EXPECT_TRUE(outStarts);

  // 6. EndsWith "World"
  EXPECT_TRUE(outEnds);

  // 7. Contains "lo Wo"
  EXPECT_TRUE(outContains);

  // 8. Equal to "Hello World"
  EXPECT_TRUE(outEqual);

  // 9. Compare with "Hello World"
  EXPECT_EQ(outCmp, 0);

  // 10. Length
  EXPECT_EQ(outLen, 11);

  // 11. IsEmpty
  EXPECT_FALSE(outEmpty);

  // 12. Parse numbers
  EXPECT_EQ(outInt, 12345);
  EXPECT_DOUBLE_EQ(outReal, 98.765);

  // 13. Array of strings indexing: StrArray[1] should be "Second"
  EXPECT_EQ(outArrayElem.toString(), "Second");
}
