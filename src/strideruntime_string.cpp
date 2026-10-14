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
  return StrideStringView{src.data + start, actualLen};
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
  return StrideStringView{src.data + start, src.length - start};
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
  return StrideStringView{src.data, end};
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
      found = __stride_string_find(src, StrideStringView{delimPtr, delimLen}, start);
    }
    int64_t tokenEnd = (found >= 0) ? found : src.length;
    int64_t tokenLen = tokenEnd - start;

    if (tokenCount < maxTokens) {
      if (outTokens) {
        outTokens[tokenCount] = StrideStringView{src.data + start, tokenLen};
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

}
