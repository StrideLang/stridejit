#ifndef STRIDERUNTIME_STRING_H
#define STRIDERUNTIME_STRING_H

#include <cstdint>
#include <cstring>
#include <string>

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
  int64_t length{0};

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

STRIDE_EXPORT void __stride_string_println(strd::StrideStringView str);

STRIDE_EXPORT void __stride_string_print(strd::StrideStringView str);

}

#endif // STRIDERUNTIME_STRING_H
