#include "gtest/gtest.h"
#include <clocale>
#include <cstring>
#include <filesystem>
#include <vector>

#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/strideruntime_string.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace strd;

namespace {
// Scratch for Concat, Repeat and Format results called directly. Compiled
// Stride code gets its scratch from the calling function's stack instead.
constexpr int32_t kScratchSize = 4096;
char gScratch[kScratchSize];

StrideStringView sv(const char *text) {
  return StrideStringView{text, static_cast<int32_t>(std::strlen(text))};
}
std::string substring(const char *text, int32_t start, int32_t len) {
  StrideStringView src = sv(text), out{};
  __stride_jit_string_substring(&src, start, len, &out);
  return out.toString();
}
std::string trim(const char *text, const char *chars) {
  StrideStringView src = sv(text), set = sv(chars), out{};
  __stride_jit_string_trim(&src, &set, &out);
  return out.toString();
}
std::string tokenAt(const char *text, const char *delim, int32_t index) {
  StrideStringView src = sv(text), d = sv(delim), out{};
  __stride_jit_string_token_at(&src, &d, index, &out);
  return out.toString();
}
int32_t tokenCount(const char *text, const char *delim) {
  StrideStringView src = sv(text), d = sv(delim);
  return __stride_jit_string_token_count(&src, &d);
}
int32_t parseInt(const char *text, int32_t defaultVal) {
  StrideStringView s = sv(text);
  return __stride_jit_string_parse_int(&s, defaultVal);
}
double parseReal(const char *text, double defaultVal) {
  StrideStringView s = sv(text);
  return __stride_jit_string_parse_real(&s, defaultVal);
}
std::string format(const char *tpl, std::vector<StrideFormatArg> args) {
  StrideStringView t = sv(tpl), out{};
  __stride_jit_string_format(&t, args.data(),
                             static_cast<int32_t>(args.size()), gScratch, kScratchSize, &out);
  return out.toString();
}
StrideFormatArg realArg(double value) {
  StrideFormatArg arg{};
  arg.typeTag = STRIDE_FMT_ARG_REAL;
  arg.realVal = value;
  return arg;
}
// Overwrites a large part of the stack below the caller.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void clobberStack() {
  volatile char junk[65536];
  for (size_t i = 0; i < sizeof(junk); ++i) {
    junk[i] = '#';
  }
}
StrideFormatArg intArg(int32_t value) {
  StrideFormatArg arg{};
  arg.typeTag = STRIDE_FMT_ARG_INT;
  arg.intVal = value;
  return arg;
}
} // namespace

TEST(JITStringTest, RuntimeHelpersDirect) {
  StrideStringView src = sv("Hello World");

  EXPECT_EQ(substring("Hello World", 0, 5), "Hello");
  EXPECT_EQ(substring("Hello World", 6, -1), "World");
  EXPECT_EQ(substring("Hello World", 20, 5), "");

  EXPECT_EQ(trim("  \tfoo bar \r\n", " \t\r\n"), "foo bar");
  EXPECT_EQ(trim("   ", " "), "");

  StrideStringView world = sv("World"), xyz = sv("XYZ");
  EXPECT_EQ(__stride_jit_string_find(&src, &world, 0), 6);
  EXPECT_EQ(__stride_jit_string_find(&src, &xyz, 0), -1);
  EXPECT_EQ(__stride_jit_string_find(&src, &world, 7), -1);

  StrideStringView path = sv("/path/to/file.stride"), dot = sv(".");
  EXPECT_EQ(__stride_jit_string_find_last(&path, &dot), 13);

  StrideStringView loWo = sv("lo Wo"), hello = sv("Hello");
  EXPECT_TRUE(__stride_jit_string_contains(&src, &loWo));
  EXPECT_FALSE(__stride_jit_string_contains(&src, &xyz));
  EXPECT_TRUE(__stride_jit_string_starts_with(&src, &hello));
  EXPECT_FALSE(__stride_jit_string_starts_with(&src, &world));
  EXPECT_TRUE(__stride_jit_string_ends_with(&src, &world));
  EXPECT_FALSE(__stride_jit_string_ends_with(&src, &hello));

  StrideStringView same = sv("Hello World"), abc = sv("abc"), def = sv("def");
  EXPECT_TRUE(__stride_jit_string_equal(&src, &same));
  EXPECT_FALSE(__stride_jit_string_equal(&src, &hello));
  EXPECT_EQ(__stride_jit_string_compare(&src, &same), 0);
  EXPECT_LT(__stride_jit_string_compare(&abc, &def), 0);
  EXPECT_GT(__stride_jit_string_compare(&src, &hello), 0);

  StrideStringView empty = sv("");
  EXPECT_EQ(__stride_jit_string_length(&src), 11);
  EXPECT_FALSE(__stride_jit_string_is_empty(&src));
  EXPECT_TRUE(__stride_jit_string_is_empty(&empty));

  EXPECT_EQ(tokenCount("one,two,three", ","), 3);
  EXPECT_EQ(tokenAt("one,two,three", ",", 0), "one");
  EXPECT_EQ(tokenAt("one,two,three", ",", 1), "two");
  EXPECT_EQ(tokenAt("one,two,three", ",", 2), "three");
  EXPECT_EQ(tokenAt("one,two,three", ",", 3), "");
  EXPECT_EQ(tokenAt("one,two,three", ",", -1), "");
  // A delimiter at the start gives an empty first token.
  EXPECT_EQ(tokenCount(",a,b", ","), 3);
  EXPECT_EQ(tokenAt(",a,b", ",", 1), "a");
  EXPECT_EQ(tokenCount("", ","), 0);

  StrideStringView abcRep = sv("abc"), rep{}, repZero{};
  __stride_jit_string_repeat(&abcRep, 3, gScratch, kScratchSize, &rep);
  EXPECT_EQ(rep.toString(), "abcabcabc");
  __stride_jit_string_repeat(&abcRep, 0, gScratch, kScratchSize, &repZero);
  EXPECT_EQ(repZero.length, 0);
}

TEST(JITStringTest, ResultsAreCutOffAtTheScratchSize) {
  StrideStringView parts[2] = {sv("Hello"), sv(" World")};
  char small[5];
  StrideStringView out{};
  __stride_jit_string_concat_n(parts, 2, small, 5, &out);
  EXPECT_EQ(out.toString(), "Hello");
  EXPECT_EQ(out.data, small);

  StrideStringView star = sv("ab");
  __stride_jit_string_repeat(&star, 10, small, 5, &out);
  EXPECT_EQ(out.toString(), "ababa");

  StrideStringView tpl = sv("value {}");
  StrideFormatArg arg = intArg(12345);
  __stride_jit_string_format(&tpl, &arg, 1, small, 5, &out);
  EXPECT_EQ(out.toString(), "value");

  // No scratch: an empty result, never a write
  __stride_jit_string_concat_n(parts, 2, nullptr, 0, &out);
  EXPECT_EQ(out.length, 0);
}

TEST(JITStringTest, ParseNumbers) {
  EXPECT_EQ(parseInt("12345", 0), 12345);
  EXPECT_EQ(parseInt("  -42", 0), -42);
  EXPECT_EQ(parseInt("+7", 0), 7);
  EXPECT_EQ(parseInt("12abc", 0), 12);
  EXPECT_EQ(parseInt("invalid", 99), 99);
  EXPECT_EQ(parseInt("-", 99), 99);
  EXPECT_EQ(parseInt("2147483647", 0), 2147483647);
  EXPECT_EQ(parseInt("-2147483648", 0), INT32_MIN);
  EXPECT_EQ(parseInt("2147483648", 99), 99); // does not fit in Int32

  EXPECT_DOUBLE_EQ(parseReal("3.14159", 0.0), 3.14159);
  EXPECT_DOUBLE_EQ(parseReal(" -2.5e3", 0.0), -2500.0);
  EXPECT_DOUBLE_EQ(parseReal("1.5kg", 0.0), 1.5);
  EXPECT_DOUBLE_EQ(parseReal("abc", 7.0), 7.0);
}

// Stride numbers always use '.', whatever locale the host program sets.
TEST(JITStringTest, NumbersIgnoreHostLocale) {
  const char *commaLocales[] = {"de_DE.UTF-8", "de_DE", "German_Germany.1252",
                                "de-DE", "fr_FR.UTF-8"};
  std::string previous = std::setlocale(LC_NUMERIC, nullptr);
  const char *set = nullptr;
  for (const char *name : commaLocales) {
    if (std::setlocale(LC_NUMERIC, name)) {
      set = name;
      break;
    }
  }
  if (!set) {
    GTEST_SKIP() << "no locale with a decimal comma is installed";
  }
  double parsed = parseReal("1.5", 0.0);
  std::string fixed = format("{:.2f}", {realArg(2.25)});
  std::string shortest = format("{}", {realArg(0.5)});
  std::setlocale(LC_NUMERIC, previous.c_str());
  EXPECT_DOUBLE_EQ(parsed, 1.5);
  EXPECT_EQ(fixed, "2.25");
  EXPECT_EQ(shortest, "0.5");
}

TEST(JITStringTest, FormatSpecs) {
  EXPECT_EQ(format("{:04d}", {intArg(42)}), "0042");
  EXPECT_EQ(format("{:05d}", {intArg(-42)}), "-0042");
  EXPECT_EQ(format("{:4d}", {intArg(7)}), "   7");
  EXPECT_EQ(format("{:x} {:X}", {intArg(255), intArg(255)}), "ff FF");
  EXPECT_EQ(format("{:08x}", {intArg(-1)}), "ffffffff");
  EXPECT_EQ(format("{:.1f}", {realArg(98.6)}), "98.6");
  EXPECT_EQ(format("{:e}", {realArg(1500.0)}), "1.500000e+03");
  EXPECT_EQ(format("{{}} {}", {intArg(1)}), "{} 1");
  EXPECT_EQ(format("{} {}", {intArg(1)}), "1 ");
  EXPECT_EQ(format("open {", {}), "open {");
}

TEST(JITStringTest, PrintGoesToWriter) {
  std::string captured;
  stride_runtime_set_writer(
      [](void *context, const char *data, int32_t length, bool endOfLine) {
        auto *text = static_cast<std::string *>(context);
        text->append(data, static_cast<size_t>(length));
        if (endOfLine) {
          text->push_back('\n');
        }
      },
      &captured);
  StrideStringView a = sv("abc"), b = sv("def");
  __stride_jit_string_print(&a);
  __stride_jit_string_println(&b);
  __stride_jit_string_println(nullptr);
  stride_runtime_set_writer(nullptr, nullptr);
  EXPECT_EQ(captured, "abcdef\n\n");
}

TEST(JITStringTest, TokenizeWithOverflow) {
  StrideStringView src = sv("alpha,beta,gamma,delta,epsilon");
  StrideStringView delim = sv(",");
  StrideStringView tokens[3];
  int32_t count = 0;
  bool overflow = false;

  __stride_jit_string_tokenize(&src, &delim, tokens, 3, &count, &overflow);
  EXPECT_EQ(count, 5);
  EXPECT_TRUE(overflow);
  EXPECT_EQ(tokens[0].toString(), "alpha");
  EXPECT_EQ(tokens[1].toString(), "beta");
  EXPECT_EQ(tokens[2].toString(), "gamma");

  StrideStringView tokensFull[10];
  overflow = false;
  __stride_jit_string_tokenize(&src, &delim, tokensFull, 10, &count,
                               &overflow);
  EXPECT_EQ(count, 5);
  EXPECT_FALSE(overflow);
  EXPECT_EQ(tokensFull[0].toString(), "alpha");
  EXPECT_EQ(tokensFull[1].toString(), "beta");
  EXPECT_EQ(tokensFull[2].toString(), "gamma");
  EXPECT_EQ(tokensFull[3].toString(), "delta");
  EXPECT_EQ(tokensFull[4].toString(), "epsilon");
}

TEST(JITStringTest, JITTokenizeAndConcatDirect) {
  StrideStringView a{"Hello, ", 7};
  StrideStringView b{"World!", 6};
  StrideStringView views2[2] = {a, b};
  StrideStringView cat{};
  __stride_jit_string_concat_n(views2, 2, gScratch, kScratchSize, &cat);
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
  __stride_jit_string_concat_n(views3, 3, gScratch, kScratchSize, &cat3);
  EXPECT_EQ(cat3.toString(), "Hello, World!!");

  // Concat N (7 items)
  StrideStringView views7[7] = {
      StrideStringView{"A", 1}, StrideStringView{"B", 1},
      StrideStringView{"C", 1}, StrideStringView{"D", 1},
      StrideStringView{"E", 1}, StrideStringView{"F", 1},
      StrideStringView{"G", 1}};
  StrideStringView cat7{};
  __stride_jit_string_concat_n(views7, 7, gScratch, kScratchSize, &cat7);
  EXPECT_EQ(cat7.toString(), "ABCDEFG");

  // Repeat
  StrideStringView repOut{};
  StrideStringView star{"*", 1};
  __stride_jit_string_repeat(&star, 5, gScratch, kScratchSize, &repOut);
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
  __stride_jit_string_format(&fmtTpl, fmtArgs, 4, gScratch, kScratchSize, &fmtOut);
  EXPECT_EQ(fmtOut.toString(), "Player Hero: Score 0042, HP 98.6%, Alive on");

  StrideStringView fmtBoolTpl{"Bool: {:b}, Int: {:d}", 21};
  StrideFormatArg fmtBoolArgs[2] = {fmtArgs[3], fmtArgs[3]};
  StrideStringView fmtBoolOut{};
  __stride_jit_string_format(&fmtBoolTpl, fmtBoolArgs, 2, gScratch, kScratchSize, &fmtBoolOut);
  EXPECT_EQ(fmtBoolOut.toString(), "Bool: true, Int: 1");
}

TEST(JITStringTest, JITCompilationAndExecution) {
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
  // 22. maxSize cuts longer values off
  EXPECT_EQ(str("OutShort"), "Hello");
  EXPECT_EQ(str("OutShortSub"), "Hello, H");

  // Stored strings own their characters: Concat and Format made theirs in
  // scratch on the process function's stack, which later calls overwrite.
  clobberStack();
  EXPECT_EQ(str("OutConcat"), "Hello, World!");
  EXPECT_EQ(str("OutFmt1"), "Player Hero: Score 0042, HP 98.6%, Alive on");
  EXPECT_EQ(str("OutArraySub"), "Hello");

  // A String set by the host is copied: the host's string may go away.
  {
    std::string temporary = "Short lived";
    ASSERT_TRUE(strenv.setStateVar(state, "InStr", temporary, std::nullopt, dom));
    temporary.assign(temporary.size(), '#');
  }
  EXPECT_EQ(str("InStr"), "Short lived");

  // The state describes where each String's characters are.
  bool sawShort = false;
  for (const auto &field : strenv.getStateFields(dom)) {
    EXPECT_EQ(field.name.find("__chars"), std::string::npos) << field.name;
    if (field.name == "OutShort") {
      sawShort = true;
      EXPECT_EQ(field.kind, StrideEnvironment::StateField::Kind::String);
      EXPECT_EQ(field.capacity, 5u);
      const auto *chars =
          static_cast<const char *>(state) + field.charsOffset;
      EXPECT_EQ(std::string(chars, 5), "Hello");
    }
  }
  EXPECT_TRUE(sawShort);
}

TEST(JITStringTest, ObjectAndHeaderEmission) {
  StrideEnvironment strenv;
  bool ok = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "StringsTest.stride");
  ASSERT_TRUE(ok);

  std::string headerStr = strenv.generateCHeaderString("RootDomain");
  EXPECT_FALSE(headerStr.empty());
  EXPECT_NE(headerStr.find("StrideStringView"), std::string::npos);
  EXPECT_NE(headerStr.find("RootDomain_process"), std::string::npos);
  EXPECT_NE(headerStr.find("const StrideStringView* InStr"), std::string::npos);
  EXPECT_NE(headerStr.find("StrideStringView* OutFmt1"), std::string::npos);

  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stride_string_obj_test";
  std::filesystem::create_directories(tempDir);

  std::string headerPath = (tempDir / "strings_test.h").string();
  EXPECT_TRUE(strenv.emitCHeader(headerPath, "RootDomain"));
  EXPECT_TRUE(std::filesystem::exists(headerPath));

  std::string objPath = (tempDir / "strings_test.o").string();
  EXPECT_TRUE(strenv.emitObjectFile(objPath));
  EXPECT_TRUE(std::filesystem::exists(objPath));
  EXPECT_GT(std::filesystem::file_size(objPath), 0u);

  std::filesystem::remove_all(tempDir);
}

namespace {
// The scratch areas for string results in function name of env's module
std::vector<uint64_t> scratchSizes(StrideEnvironment &env,
                                   const std::string &name) {
  std::vector<uint64_t> sizes;
  llvm::Function *function = env.mStrideEnv.TheModule->getFunction(name);
  if (!function) {
    return sizes;
  }
  for (auto &block : *function) {
    for (auto &inst : block) {
      if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
        if (alloca->getName().starts_with("string_scratch")) {
          sizes.push_back(alloca->getAllocatedType()->getArrayNumElements());
        }
      }
    }
  }
  return sizes;
}
} // namespace

// Concat, Repeat and Format get scratch on the calling function's stack: one
// area per function, as big as its largest stream needs, and none at all in
// a program that never makes such a result.
TEST(JITStringTest, ScratchOnlyWhereNeeded) {
  StrideEnvironment strings;
  ASSERT_TRUE(strings.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "StringsTest.stride"));
  auto sizes = scratchSizes(strings, "RootDomain_process");
  ASSERT_EQ(sizes.size(), 1u);
  // Each stream reuses the area: it is the largest stream's need (the four
  // value Format), not the sum of every Concat, Repeat and Format.
  EXPECT_GT(sizes[0], 0u);
  EXPECT_LE(sizes[0], 2u * StrideCompiler::kDefaultStringMaxSize);

  StrideEnvironment plain;
  ASSERT_TRUE(plain.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "avr_blink.stride"));
  for (auto &function : *plain.mStrideEnv.TheModule) {
    EXPECT_TRUE(scratchSizes(plain, function.getName().str()).empty())
        << function.getName().str();
  }
}
