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

  // RootDomain declares a domain-level array (StrArray), so it is stateful.
  // In a stateful domain every port (inputs and outputs) lives in the domain
  // state struct and is accessed through setStateVar/getStateVar. The process
  // function still takes the state pointer followed by one (unused) pointer
  // per port, in declaration order: inputs then outputs.
  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *state = statePtr.get();

  const std::string dom = "RootDomain";
  ASSERT_TRUE(strenv.setStateVar(state, "InStr", StrideStringView{"Hello World", 11}, std::nullopt, dom));
  ASSERT_TRUE(strenv.setStateVar(state, "PaddedStr", StrideStringView{"   Trimmed   ", 13}, std::nullopt, dom));

  // state + 2 inputs + 14 outputs
  void *args[17] = {state};
  int32_t res = strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(res, 0);

  auto str = [&](const char *name) {
    auto v = strenv.getStateVar<StrideStringView>(state, name, std::nullopt, dom);
    EXPECT_TRUE(v.has_value()) << name;
    return v ? v->toString() : std::string();
  };

  // 1. Literal assignment
  EXPECT_EQ(str("OutLiteral"), "Constant Message");
  // 2. Substring [0, 5]
  EXPECT_EQ(str("OutSub"), "Hello");
  // 3. Trim
  EXPECT_EQ(str("OutTrim"), "Trimmed");
  // 4. Find "World"
  EXPECT_EQ(strenv.getStateVar<int32_t>(state, "OutPos", std::nullopt, dom).value_or(-1), 6);
  // 5. StartsWith "Hello"
  EXPECT_TRUE(strenv.getStateVar<bool>(state, "OutStarts", std::nullopt, dom).value_or(false));
  // 6. EndsWith "World"
  EXPECT_TRUE(strenv.getStateVar<bool>(state, "OutEnds", std::nullopt, dom).value_or(false));
  // 7. Contains "lo Wo"
  EXPECT_TRUE(strenv.getStateVar<bool>(state, "OutContains", std::nullopt, dom).value_or(false));
  // 8. Equal to "Hello World"
  EXPECT_TRUE(strenv.getStateVar<bool>(state, "OutEqual", std::nullopt, dom).value_or(false));
  // 9. Compare with "Hello World"
  EXPECT_EQ(strenv.getStateVar<int32_t>(state, "OutCmp", std::nullopt, dom).value_or(-999), 0);
  // 10. Length
  EXPECT_EQ(strenv.getStateVar<int32_t>(state, "OutLen", std::nullopt, dom).value_or(-1), 11);
  // 11. IsEmpty
  EXPECT_FALSE(strenv.getStateVar<bool>(state, "OutEmpty", std::nullopt, dom).value_or(true));
  // 12. Parse numbers
  EXPECT_EQ(strenv.getStateVar<int32_t>(state, "OutInt", std::nullopt, dom).value_or(-1), 12345);
  EXPECT_DOUBLE_EQ(strenv.getStateVar<double>(state, "OutReal", std::nullopt, dom).value_or(0.0), 98.765);
  // 13. Array of strings indexing: StrArray[1] should be "Second"
  EXPECT_EQ(str("OutArrayElem"), "Second");
}