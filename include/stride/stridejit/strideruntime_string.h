#ifndef STRIDERUNTIME_STRING_H
#define STRIDERUNTIME_STRING_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

#if defined(_MSC_VER)
#define STRIDE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define STRIDE_EXPORT __attribute__((visibility("default")))
#else
#define STRIDE_EXPORT
#endif

namespace strd {

struct StrideStringView {
  const char *data{nullptr};
  int32_t length{0};

  std::string toString() const {
    return (data && length > 0)
               ? std::string(data, static_cast<size_t>(length))
               : std::string();
  }

  bool operator==(const StrideStringView &other) const {
    if (length != other.length)
      return false;
    if (length == 0)
      return true;
    return std::strncmp(data, other.data, static_cast<size_t>(length)) == 0;
  }

  bool operator!=(const StrideStringView &other) const {
    return !(*this == other);
  }

  bool operator==(const std::string &other) const {
    return toString() == other;
  }

  bool operator!=(const std::string &other) const {
    return toString() != other;
  }

  bool operator==(const char *other) const {
    if (!other)
      return length == 0;
    return toString() == other;
  }

  bool operator!=(const char *other) const {
    return !(*this == other);
  }
};

// ABI contract: the JIT's _StringType is the LLVM struct { ptr, i32 }. The
// host-side view must be bit-identical, and no layout assumption may drift.
// NOTE: aggregates are never passed or returned by value across the
// JIT <-> C++ boundary (calling conventions for them differ per platform and
// compiler, e.g. MSVC/Win64 uses hidden references, SysV splits into
// registers). Use the pointer-based __stride_jit_string_* entry points.
static_assert(std::is_standard_layout<StrideStringView>::value,
              "StrideStringView must be standard-layout to mirror the LLVM "
              "{ptr, i32} _StringType");
// The length is a fixed 32-bit integer (same as _IntType), so the layout is
// identical in LLVM and C++ on 32- and 64-bit targets: natural alignment
// gives 8 bytes on ILP32 and 16 bytes (4 bytes of padding) on LP64.
static_assert(sizeof(int32_t) == 4, "length must be 32-bit");
static_assert(offsetof(StrideStringView, data) == 0,
              "StrideStringView::data must be at offset 0");
static_assert(offsetof(StrideStringView, length) == sizeof(void *),
              "StrideStringView::length must immediately follow the pointer");
static_assert(sizeof(StrideStringView) == 2 * sizeof(void *),
              "StrideStringView must be {ptr, i32} with natural padding");

enum StrideFormatArgType : int32_t {
  STRIDE_FMT_ARG_INT = 0,
  STRIDE_FMT_ARG_REAL = 1,
  STRIDE_FMT_ARG_SWITCH = 2,
  STRIDE_FMT_ARG_STRING = 3,
};

struct StrideFormatArg {
  int32_t typeTag{0};
  int32_t intVal{0};
  double realVal{0.0};
  StrideStringView strVal{};
};

static_assert(std::is_standard_layout<StrideFormatArg>::value,
              "StrideFormatArg must be standard-layout");
static_assert(offsetof(StrideFormatArg, typeTag) == 0,
              "StrideFormatArg::typeTag must be at offset 0");
static_assert(offsetof(StrideFormatArg, intVal) == 4,
              "StrideFormatArg::intVal must be at offset 4");
static_assert(offsetof(StrideFormatArg, realVal) == 8,
              "StrideFormatArg::realVal must be at offset 8");
static_assert(offsetof(StrideFormatArg, strVal) == 16,
              "StrideFormatArg::strVal must be at offset 16");

} // namespace strd

extern "C" {

STRIDE_EXPORT strd::StrideStringView
__stride_string_substring(strd::StrideStringView src, int64_t start,
                          int64_t len);

STRIDE_EXPORT strd::StrideStringView
__stride_string_trim(strd::StrideStringView src, strd::StrideStringView chars);

STRIDE_EXPORT strd::StrideStringView
__stride_string_trim_left(strd::StrideStringView src,
                          strd::StrideStringView chars);

STRIDE_EXPORT strd::StrideStringView
__stride_string_trim_right(strd::StrideStringView src,
                           strd::StrideStringView chars);

STRIDE_EXPORT int64_t __stride_string_find(strd::StrideStringView haystack,
                                           strd::StrideStringView needle,
                                           int64_t start);

STRIDE_EXPORT int64_t __stride_string_find_last(strd::StrideStringView haystack,
                                                strd::StrideStringView needle);

STRIDE_EXPORT bool __stride_string_contains(strd::StrideStringView haystack,
                                            strd::StrideStringView needle);

STRIDE_EXPORT bool __stride_string_starts_with(strd::StrideStringView str,
                                               strd::StrideStringView prefix);

STRIDE_EXPORT bool __stride_string_ends_with(strd::StrideStringView str,
                                             strd::StrideStringView suffix);

STRIDE_EXPORT bool __stride_string_equal(strd::StrideStringView a,
                                         strd::StrideStringView b);

STRIDE_EXPORT int64_t __stride_string_compare(strd::StrideStringView a,
                                              strd::StrideStringView b);

STRIDE_EXPORT int64_t __stride_string_length(strd::StrideStringView str);

STRIDE_EXPORT bool __stride_string_is_empty(strd::StrideStringView str);

STRIDE_EXPORT int64_t __stride_string_parse_int(strd::StrideStringView str,
                                                int64_t defaultVal);

STRIDE_EXPORT double __stride_string_parse_real(strd::StrideStringView str,
                                                double defaultVal);

STRIDE_EXPORT int64_t __stride_string_tokenize(
    strd::StrideStringView src, strd::StrideStringView delim,
    strd::StrideStringView *outTokens, int64_t maxTokens, bool *outOverflow);

STRIDE_EXPORT strd::StrideStringView __stride_string_token_at(
    strd::StrideStringView src, strd::StrideStringView delim, int64_t index);

STRIDE_EXPORT int64_t __stride_string_token_count(
    strd::StrideStringView src, strd::StrideStringView delim);

STRIDE_EXPORT strd::StrideStringView __stride_string_repeat(
    strd::StrideStringView str, int64_t count);

STRIDE_EXPORT void __stride_string_println(strd::StrideStringView str);

STRIDE_EXPORT void __stride_string_print(strd::StrideStringView str);

// ---------------------------------------------------------------------------
// JIT-facing entry points. These are ABI-neutral: strings are always passed by
// pointer, string results are written through a trailing out pointer, and
// integers use the 32-bit width of _IntType. The JIT-generated code and the
// host C++ compiler (e.g. MSVC, which passes 16-byte structs by hidden
// reference) disagree on by-value struct passing, so Stride platform modules
// must bind to these instead of the by-value functions above.
// ---------------------------------------------------------------------------
STRIDE_EXPORT void
__stride_jit_string_substring(const strd::StrideStringView *src, int32_t start,
                              int32_t len, strd::StrideStringView *out);
STRIDE_EXPORT void __stride_jit_string_trim(const strd::StrideStringView *src,
                                            const strd::StrideStringView *chars,
                                            strd::StrideStringView *out);
STRIDE_EXPORT void
__stride_jit_string_trim_left(const strd::StrideStringView *src,
                              const strd::StrideStringView *chars,
                              strd::StrideStringView *out);
STRIDE_EXPORT void
__stride_jit_string_trim_right(const strd::StrideStringView *src,
                               const strd::StrideStringView *chars,
                               strd::StrideStringView *out);
STRIDE_EXPORT int32_t __stride_jit_string_find(
    const strd::StrideStringView *haystack,
    const strd::StrideStringView *needle, int32_t start);
STRIDE_EXPORT int32_t __stride_jit_string_find_last(
    const strd::StrideStringView *haystack,
    const strd::StrideStringView *needle);
STRIDE_EXPORT bool
__stride_jit_string_contains(const strd::StrideStringView *haystack,
                             const strd::StrideStringView *needle);
STRIDE_EXPORT bool
__stride_jit_string_starts_with(const strd::StrideStringView *str,
                                const strd::StrideStringView *prefix);
STRIDE_EXPORT bool
__stride_jit_string_ends_with(const strd::StrideStringView *str,
                              const strd::StrideStringView *suffix);
STRIDE_EXPORT bool __stride_jit_string_equal(const strd::StrideStringView *a,
                                             const strd::StrideStringView *b);
STRIDE_EXPORT int32_t __stride_jit_string_compare(
    const strd::StrideStringView *a, const strd::StrideStringView *b);
STRIDE_EXPORT int32_t
__stride_jit_string_length(const strd::StrideStringView *str);
STRIDE_EXPORT bool
__stride_jit_string_is_empty(const strd::StrideStringView *str);
STRIDE_EXPORT int32_t __stride_jit_string_parse_int(
    const strd::StrideStringView *str, int32_t defaultVal);
STRIDE_EXPORT double
__stride_jit_string_parse_real(const strd::StrideStringView *str,
                               double defaultVal);
STRIDE_EXPORT void
__stride_jit_string_println(const strd::StrideStringView *str);
STRIDE_EXPORT void __stride_jit_string_print(const strd::StrideStringView *str);
STRIDE_EXPORT void __stride_jit_string_tokenize(
    const strd::StrideStringView *src, const strd::StrideStringView *delim,
    strd::StrideStringView *outTokens, int32_t maxTokens, int32_t *outCount,
    bool *outOverflow);
STRIDE_EXPORT void __stride_jit_string_token_at(
    const strd::StrideStringView *src, const strd::StrideStringView *delim,
    int32_t index, strd::StrideStringView *out);
STRIDE_EXPORT int32_t __stride_jit_string_token_count(
    const strd::StrideStringView *src, const strd::StrideStringView *delim);
STRIDE_EXPORT void
__stride_jit_string_concat_n(const strd::StrideStringView *views, int32_t count,
                             strd::StrideStringView *out);
STRIDE_EXPORT void
__stride_jit_string_repeat(const strd::StrideStringView *str, int32_t count,
                           strd::StrideStringView *out);
STRIDE_EXPORT void
__stride_jit_string_format(const strd::StrideStringView *fmt,
                           const strd::StrideFormatArg *args, int32_t count,
                           strd::StrideStringView *out);

}

#endif // STRIDERUNTIME_STRING_H
