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

  // TokenAt & TokenCount
  StrideStringView tokSrc{"one,two,three", 13};
  StrideStringView tokDelim{",", 1};
  EXPECT_EQ(__stride_string_token_count(tokSrc, tokDelim), 3);
  EXPECT_EQ(__stride_string_token_at(tokSrc, tokDelim, 0).toString(), "one");
  EXPECT_EQ(__stride_string_token_at(tokSrc, tokDelim, 1).toString(), "two");
  EXPECT_EQ(__stride_string_token_at(tokSrc, tokDelim, 2).toString(), "three");
  EXPECT_EQ(__stride_string_token_at(tokSrc, tokDelim, 3).length, 0);
  EXPECT_EQ(__stride_string_token_at(tokSrc, tokDelim, -1).length, 0);

  // Repeat
  auto rep = __stride_string_repeat(StrideStringView{"abc", 3}, 3);
  EXPECT_EQ(rep.toString(), "abcabcabc");
  auto repZero = __stride_string_repeat(StrideStringView{"abc", 3}, 0);
  EXPECT_EQ(repZero.length, 0);
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

TEST(Strings, JITTokenizeAndConcatDirect) {
  StrideStringView a{"Hello, ", 7};
  StrideStringView b{"World!", 6};
  StrideStringView views2[2] = {a, b};
  StrideStringView cat{};
  __stride_jit_string_concat_n(views2, 2, &cat);
  EXPECT_EQ(cat.toString(), "Hello, World!");

  StrideStringView src{"one:two:three", 13};
  StrideStringView delim{":", 1};
  StrideStringView tokens[4];
  int32_t count = 0;
  bool overflow = false;
  __stride_jit_string_tokenize(&src, &delim, tokens, 4, &count, &overflow);
  EXPECT_EQ(count, 3);
  EXPECT_FALSE(overflow);
  EXPECT_EQ(tokens[0].toString(), "one");
  EXPECT_EQ(tokens[1].toString(), "two");
  EXPECT_EQ(tokens[2].toString(), "three");

  // Concat 3
  StrideStringView c{"!", 1};
  StrideStringView views3[3] = {a, b, c};
  StrideStringView cat3{};
  __stride_jit_string_concat_n(views3, 3, &cat3);
  EXPECT_EQ(cat3.toString(), "Hello, World!!");

  // Concat N (7 items)
  StrideStringView views7[7] = {
      StrideStringView{"A", 1}, StrideStringView{"B", 1},
      StrideStringView{"C", 1}, StrideStringView{"D", 1},
      StrideStringView{"E", 1}, StrideStringView{"F", 1},
      StrideStringView{"G", 1}};
  StrideStringView cat7{};
  __stride_jit_string_concat_n(views7, 7, &cat7);
  EXPECT_EQ(cat7.toString(), "ABCDEFG");

  // Repeat
  StrideStringView repOut{};
  StrideStringView star{"*", 1};
  __stride_jit_string_repeat(&star, 5, &repOut);
  EXPECT_EQ(repOut.toString(), "*****");

  // TokenAt & TokenCount
  StrideStringView tokAtOut{};
  __stride_jit_string_token_at(&src, &delim, 1, &tokAtOut);
  EXPECT_EQ(tokAtOut.toString(), "two");

  int32_t tokCnt = __stride_jit_string_token_count(&src, &delim);
  EXPECT_EQ(tokCnt, 3);

  // Format direct
  StrideStringView fmtTpl{"Player {}: Score {:04d}, HP {:.1f}%, Alive {}", 45};
  StrideFormatArg fmtArgs[4] = {};
  fmtArgs[0].typeTag = STRIDE_FMT_ARG_STRING;
  fmtArgs[0].strVal = StrideStringView{"Hero", 4};
  fmtArgs[1].typeTag = STRIDE_FMT_ARG_INT;
  fmtArgs[1].intVal = 42;
  fmtArgs[2].typeTag = STRIDE_FMT_ARG_REAL;
  fmtArgs[2].realVal = 98.6;
  fmtArgs[3].typeTag = STRIDE_FMT_ARG_SWITCH;
  fmtArgs[3].intVal = 1;
  StrideStringView fmtOut{};
  __stride_jit_string_format(&fmtTpl, fmtArgs, 4, &fmtOut);
  EXPECT_EQ(fmtOut.toString(), "Player Hero: Score 0042, HP 98.6%, Alive on");

  StrideStringView fmtBoolTpl{"Bool: {:b}, Int: {:d}", 21};
  StrideFormatArg fmtBoolArgs[2] = {fmtArgs[3], fmtArgs[3]};
  StrideStringView fmtBoolOut{};
  __stride_jit_string_format(&fmtBoolTpl, fmtBoolArgs, 2, &fmtBoolOut);
  EXPECT_EQ(fmtBoolOut.toString(), "Bool: true, Int: 1");
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

  // state + 2 inputs + 28 outputs
  void *args[40] = {state};
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
  // 13. Concat
  EXPECT_EQ(str("OutConcat"), "Hello, World!");
  // 14. Array of strings indexing: StrArray[1] should be "Second"
  EXPECT_EQ(str("OutArrayElem"), "Second");
  // 15. Substring written directly into array element StrArray[2]
  EXPECT_EQ(str("OutArraySub"), "Hello");
  // 16. Multi-argument concat
  EXPECT_EQ(str("OutConcat3"), "ABC");
  EXPECT_EQ(str("OutConcat5"), "12345");
  EXPECT_EQ(str("OutConcat7"), "ABCDEFG");
  // 17. Binary string addition sugar
  EXPECT_EQ(str("OutPlus"), "Hello World");
  // 18. Repeat
  EXPECT_EQ(str("OutRepeat"), "=====");
  // 19. Tokenization queries
  EXPECT_EQ(strenv.getStateVar<int32_t>(state, "OutTokCount", std::nullopt, dom).value_or(-1), 3);
  EXPECT_EQ(str("OutTok1"), "beta");
  // 20. Format
  EXPECT_EQ(str("OutFmt1"), "Player Hero: Score 0042, HP 98.6%, Alive on");
  EXPECT_EQ(str("OutFmt2"), "[Boss] Power: 9000");
  // 21. Constant with string value
  EXPECT_EQ(str("OutConstStr"), "Greetings");
  EXPECT_EQ(str("OutConstCat"), "Greetings, Hello World");
  EXPECT_EQ(str("OutConstFmt"), "Greetings #7!");
}