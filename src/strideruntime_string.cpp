#include "stride/stridejit/strideruntime_string.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>

using namespace strd;

extern "C" {

STRIDE_EXPORT StrideStringView
__stride_string_substring(StrideStringView src, int64_t start, int64_t len) {
  if (!src.data || start < 0 || start >= src.length) {
    return StrideStringView{nullptr, 0};
  }
  int64_t avail = src.length - start;
  int64_t actualLen = (len < 0 || len > avail) ? avail : len;
  return StrideStringView{src.data + start, static_cast<int32_t>(actualLen)};
}

STRIDE_EXPORT StrideStringView
__stride_string_trim_left(StrideStringView src, StrideStringView chars) {
  if (!src.data || src.length == 0) {
    return StrideStringView{nullptr, 0};
  }
  const char *trimChars = (chars.data && chars.length > 0) ? chars.data : " \t\r\n";
  size_t trimLen = (chars.data && chars.length > 0) ? static_cast<size_t>(chars.length) : 4;

  int64_t start = 0;
  while (start < src.length) {
    char c = src.data[start];
    bool match = false;
    for (size_t i = 0; i < trimLen; ++i) {
      if (c == trimChars[i]) {
        match = true;
        break;
      }
    }
    if (!match)
      break;
    start++;
  }
  return StrideStringView{src.data + start, static_cast<int32_t>(src.length - start)};
}

STRIDE_EXPORT StrideStringView
__stride_string_trim_right(StrideStringView src, StrideStringView chars) {
  if (!src.data || src.length == 0) {
    return StrideStringView{nullptr, 0};
  }
  const char *trimChars = (chars.data && chars.length > 0) ? chars.data : " \t\r\n";
  size_t trimLen = (chars.data && chars.length > 0) ? static_cast<size_t>(chars.length) : 4;

  int64_t end = src.length;
  while (end > 0) {
    char c = src.data[end - 1];
    bool match = false;
    for (size_t i = 0; i < trimLen; ++i) {
      if (c == trimChars[i]) {
        match = true;
        break;
      }
    }
    if (!match)
      break;
    end--;
  }
  return StrideStringView{src.data, static_cast<int32_t>(end)};
}

STRIDE_EXPORT StrideStringView
__stride_string_trim(StrideStringView src, StrideStringView chars) {
  StrideStringView left = __stride_string_trim_left(src, chars);
  return __stride_string_trim_right(left, chars);
}

STRIDE_EXPORT int64_t __stride_string_find(StrideStringView haystack,
                                           StrideStringView needle,
                                           int64_t start) {
  if (!haystack.data || !needle.data || needle.length == 0) {
    return -1;
  }
  if (start < 0)
    start = 0;
  if (start + needle.length > haystack.length) {
    return -1;
  }

  size_t nlen = static_cast<size_t>(needle.length);
  for (int64_t i = start; i <= haystack.length - needle.length; ++i) {
    if (std::memcmp(haystack.data + i, needle.data, nlen) == 0) {
      return i;
    }
  }
  return -1;
}

STRIDE_EXPORT int64_t __stride_string_find_last(StrideStringView haystack,
                                                StrideStringView needle) {
  if (!haystack.data || !needle.data || needle.length == 0 ||
      needle.length > haystack.length) {
    return -1;
  }

  size_t nlen = static_cast<size_t>(needle.length);
  for (int64_t i = haystack.length - needle.length; i >= 0; --i) {
    if (std::memcmp(haystack.data + i, needle.data, nlen) == 0) {
      return i;
    }
  }
  return -1;
}

STRIDE_EXPORT bool __stride_string_contains(StrideStringView haystack,
                                            StrideStringView needle) {
  return __stride_string_find(haystack, needle, 0) >= 0;
}

STRIDE_EXPORT bool __stride_string_starts_with(StrideStringView str,
                                               StrideStringView prefix) {
  if (!str.data || !prefix.data || prefix.length > str.length) {
    return false;
  }
  return std::memcmp(str.data, prefix.data, static_cast<size_t>(prefix.length)) == 0;
}

STRIDE_EXPORT bool __stride_string_ends_with(StrideStringView str,
                                             StrideStringView suffix) {
  if (!str.data || !suffix.data || suffix.length > str.length) {
    return false;
  }
  size_t offset = static_cast<size_t>(str.length - suffix.length);
  return std::memcmp(str.data + offset, suffix.data,
                     static_cast<size_t>(suffix.length)) == 0;
}

STRIDE_EXPORT bool __stride_string_equal(StrideStringView a,
                                         StrideStringView b) {
  if (a.length != b.length)
    return false;
  if (a.length == 0)
    return true;
  if (!a.data || !b.data)
    return false;
  return std::memcmp(a.data, b.data, static_cast<size_t>(a.length)) == 0;
}

STRIDE_EXPORT int64_t __stride_string_compare(StrideStringView a,
                                              StrideStringView b) {
  size_t minLen = std::min(static_cast<size_t>(a.length),
                           static_cast<size_t>(b.length));
  if (minLen > 0 && a.data && b.data) {
    int cmp = std::memcmp(a.data, b.data, minLen);
    if (cmp != 0)
      return cmp < 0 ? -1 : 1;
  }
  if (a.length < b.length)
    return -1;
  if (a.length > b.length)
    return 1;
  return 0;
}

STRIDE_EXPORT int64_t __stride_string_length(StrideStringView str) {
  return str.length;
}

STRIDE_EXPORT bool __stride_string_is_empty(StrideStringView str) {
  return str.length == 0;
}

STRIDE_EXPORT int64_t __stride_string_parse_int(StrideStringView str,
                                                int64_t defaultVal) {
  if (!str.data || str.length == 0)
    return defaultVal;
  std::string s(str.data, static_cast<size_t>(str.length));
  try {
    return std::stoll(s);
  } catch (...) {
    return defaultVal;
  }
}

STRIDE_EXPORT double __stride_string_parse_real(StrideStringView str,
                                                double defaultVal) {
  if (!str.data || str.length == 0)
    return defaultVal;
  std::string s(str.data, static_cast<size_t>(str.length));
  try {
    return std::stod(s);
  } catch (...) {
    return defaultVal;
  }
}

STRIDE_EXPORT int64_t __stride_string_tokenize(
    StrideStringView src, StrideStringView delim,
    StrideStringView *outTokens, int64_t maxTokens, bool *outOverflow) {
  if (outOverflow)
    *outOverflow = false;
  if (!src.data || src.length == 0)
    return 0;

  const char *delimPtr = (delim.data && delim.length > 0) ? delim.data : " ";
  int64_t delimLen = (delim.data && delim.length > 0) ? delim.length : 1;

  int64_t tokenCount = 0;
  int64_t start = 0;

  while (start <= src.length) {
    int64_t found = -1;
    if (start < src.length) {
      found = __stride_string_find(src, StrideStringView{delimPtr, static_cast<int32_t>(delimLen)}, start);
    }
    int64_t tokenEnd = (found >= 0) ? found : src.length;
    int64_t tokenLen = tokenEnd - start;

    if (tokenCount < maxTokens) {
      if (outTokens) {
        outTokens[tokenCount] = StrideStringView{src.data + start, static_cast<int32_t>(tokenLen)};
      }
    } else {
      if (outOverflow) {
        *outOverflow = true;
      }
    }
    tokenCount++;

    if (found < 0)
      break;
    start = found + delimLen;
  }

  return tokenCount;
}

STRIDE_EXPORT void __stride_string_println(StrideStringView str) {
  if (str.data && str.length > 0) {
    std::cout.write(str.data, str.length);
  }
  std::cout << '\n';
  std::cout.flush();
}

STRIDE_EXPORT void __stride_string_print(StrideStringView str) {
  if (str.data && str.length > 0) {
    std::cout.write(str.data, str.length);
  }
  std::cout.flush();
}

// ---------------------------------------------------------------------------
// JIT-facing, ABI-neutral wrappers (see header).
// ---------------------------------------------------------------------------

STRIDE_EXPORT void
__stride_jit_string_substring(const StrideStringView *src, int32_t start,
                              int32_t len, StrideStringView *out) {
  *out = __stride_string_substring(*src, start, len);
}

STRIDE_EXPORT void __stride_jit_string_trim(const StrideStringView *src,
                                            const StrideStringView *chars,
                                            StrideStringView *out) {
  *out = __stride_string_trim(*src, *chars);
}

STRIDE_EXPORT void __stride_jit_string_trim_left(const StrideStringView *src,
                                                 const StrideStringView *chars,
                                                 StrideStringView *out) {
  *out = __stride_string_trim_left(*src, *chars);
}

STRIDE_EXPORT void __stride_jit_string_trim_right(const StrideStringView *src,
                                                  const StrideStringView *chars,
                                                  StrideStringView *out) {
  *out = __stride_string_trim_right(*src, *chars);
}

STRIDE_EXPORT int32_t __stride_jit_string_find(const StrideStringView *haystack,
                                               const StrideStringView *needle,
                                               int32_t start) {
  return static_cast<int32_t>(__stride_string_find(*haystack, *needle, start));
}

STRIDE_EXPORT int32_t __stride_jit_string_find_last(
    const StrideStringView *haystack, const StrideStringView *needle) {
  return static_cast<int32_t>(__stride_string_find_last(*haystack, *needle));
}

STRIDE_EXPORT bool __stride_jit_string_contains(const StrideStringView *haystack,
                                                const StrideStringView *needle) {
  return __stride_string_contains(*haystack, *needle);
}

STRIDE_EXPORT bool
__stride_jit_string_starts_with(const StrideStringView *str,
                                const StrideStringView *prefix) {
  return __stride_string_starts_with(*str, *prefix);
}

STRIDE_EXPORT bool __stride_jit_string_ends_with(const StrideStringView *str,
                                                 const StrideStringView *suffix) {
  return __stride_string_ends_with(*str, *suffix);
}

STRIDE_EXPORT bool __stride_jit_string_equal(const StrideStringView *a,
                                             const StrideStringView *b) {
  return __stride_string_equal(*a, *b);
}

STRIDE_EXPORT int32_t __stride_jit_string_compare(const StrideStringView *a,
                                                  const StrideStringView *b) {
  return static_cast<int32_t>(__stride_string_compare(*a, *b));
}

STRIDE_EXPORT int32_t __stride_jit_string_length(const StrideStringView *str) {
  return static_cast<int32_t>(__stride_string_length(*str));
}

STRIDE_EXPORT bool __stride_jit_string_is_empty(const StrideStringView *str) {
  return __stride_string_is_empty(*str);
}

STRIDE_EXPORT int32_t __stride_jit_string_parse_int(const StrideStringView *str,
                                                    int32_t defaultVal) {
  return static_cast<int32_t>(__stride_string_parse_int(*str, defaultVal));
}

STRIDE_EXPORT double __stride_jit_string_parse_real(const StrideStringView *str,
                                                    double defaultVal) {
  return __stride_string_parse_real(*str, defaultVal);
}

STRIDE_EXPORT void __stride_jit_string_println(const StrideStringView *str) {
  __stride_string_println(*str);
}

STRIDE_EXPORT void __stride_jit_string_print(const StrideStringView *str) {
  __stride_string_print(*str);
}

STRIDE_EXPORT void __stride_jit_string_tokenize(
    const StrideStringView *src, const StrideStringView *delim,
    StrideStringView *outTokens, int32_t maxTokens, int32_t *outCount,
    bool *outOverflow) {
  int64_t count = __stride_string_tokenize(
      src ? *src : StrideStringView{},
      delim ? *delim : StrideStringView{" ", 1},
      outTokens, maxTokens, outOverflow);
  if (outCount) {
    *outCount = static_cast<int32_t>(count);
  }
}

STRIDE_EXPORT void
__stride_jit_string_concat2(const StrideStringView *a,
                            const StrideStringView *b,
                            StrideStringView *out) {
  if (!out)
    return;
  int32_t lenA = (a && a->data && a->length > 0) ? a->length : 0;
  int32_t lenB = (b && b->data && b->length > 0) ? b->length : 0;
  int32_t totalLen = lenA + lenB;
  if (totalLen == 0) {
    *out = StrideStringView{"", 0};
    return;
  }
  static thread_local char buffer[65536];
  static thread_local size_t offset = 0;
  if (offset + static_cast<size_t>(totalLen) + 1 > sizeof(buffer)) {
    offset = 0;
  }
  char *dest = buffer + offset;
  offset += static_cast<size_t>(totalLen) + 1;
  if (lenA > 0) {
    std::memcpy(dest, a->data, static_cast<size_t>(lenA));
  }
  if (lenB > 0) {
    std::memcpy(dest + lenA, b->data, static_cast<size_t>(lenB));
  }
  dest[totalLen] = '\0';
  *out = StrideStringView{dest, totalLen};
}

}
