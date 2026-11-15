/*
 * Stride string runtime.
 *
 * Plain C99 so the same file builds for the desktop JIT and for
 * microcontrollers (AVR: avr-gcc and avr-libc). Compiled Stride code calls the
 * __stride_jit_string_* entry points below; strings always cross the call
 * by pointer, and string results come back through a trailing out pointer, so
 * there is no by-value struct ABI to agree on.
 *
 * Layout contract (mirrored by strd::StrideStringView and StrideFormatArg in
 * strideruntime_string.h, and by the compiler's String type):
 *
 *   String     { const char *data; int32_t length; }
 *   FormatArg  { int32_t typeTag; int32_t intVal; double realVal; String strVal; }
 *
 * Concat, Repeat and Format write their characters into scratch space the
 * caller passes, with its size: the compiler reserves it on the stack of the
 * calling function, sized for the result when compiling. The runtime has no
 * buffers of its own, so it is safe from any thread or interrupt.
 *
 * A function that can fail returns a stride_status_t (stride_status.h), or
 * writes one through a trailing status pointer when its return value is the
 * result. The compiler folds it into the domain's status word.
 *
 * Build options (all optional):
 *
 *   STRIDE_RT_NO_STDIO      no <stdio.h>: printing goes to
 *                           stride_rt_platform_write(), which the platform
 *                           runtime provides, and numbers are formatted with
 *                           avr-libc's dtostrf/dtostre
 *   STRIDE_RT_REAL_IS_FLOAT Real is float on this target (FormatArg.realVal
 *                           and parse_real use float)
 *
 * Numbers are always read and written with '.' as the decimal separator,
 * whatever locale the host process has set.
 */

#if defined(_MSC_VER)
/* _snprintf_l: the buffer size is always passed, so it is safe. */
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#if !defined(_MSC_VER) && !defined(STRIDE_RT_NO_STDIO)
/* strtod_l, newlocale and uselocale */
#define _GNU_SOURCE 1
#endif

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stride/stridejit/stride_status.h"

#ifndef STRIDE_RT_NO_STDIO
#include <locale.h>
#include <stdio.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

#if defined(_MSC_VER)
#define STRIDE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) && !defined(__AVR__)
#define STRIDE_EXPORT __attribute__((visibility("default")))
#else
#define STRIDE_EXPORT
#endif

#ifdef STRIDE_RT_REAL_IS_FLOAT
typedef float stride_real_t;
#else
typedef double stride_real_t;
#endif

typedef struct StrideString {
  const char *data;
  int32_t length;
} StrideString;

enum {
  STRIDE_FMT_ARG_INT = 0,
  STRIDE_FMT_ARG_REAL = 1,
  STRIDE_FMT_ARG_SWITCH = 2,
  STRIDE_FMT_ARG_STRING = 3
};

typedef struct StrideFormatArg {
  int32_t typeTag;
  int32_t intVal;
  stride_real_t realVal;
  StrideString strVal;
} StrideFormatArg;

static const StrideString kEmpty = {"", 0};
static const StrideString kNull = {NULL, 0};
static const StrideString kWhitespace = {" \t\r\n", 4};
static const StrideString kSpace = {" ", 1};

static StrideString make_string(const char *data, int32_t length) {
  StrideString s;
  s.data = data;
  s.length = length;
  return s;
}

static bool is_empty(const StrideString *s) {
  return !s || !s->data || s->length <= 0;
}

/* ---- Output ------------------------------------------------------------- */

typedef void (*stride_rt_writer)(void *context, const char *data,
                                 int32_t length, bool endOfLine);

#ifdef STRIDE_RT_NO_STDIO
/* Provided by the platform runtime (e.g. the AVR serial port). */
void stride_rt_platform_write(const char *data, int32_t length);

static void default_writer(void *context, const char *data, int32_t length,
                           bool endOfLine) {
  (void)context;
  if (length > 0) {
    stride_rt_platform_write(data, length);
  }
  if (endOfLine) {
    stride_rt_platform_write("\r\n", 2);
  }
}
#else
static void default_writer(void *context, const char *data, int32_t length,
                           bool endOfLine) {
  (void)context;
  if (length > 0) {
    fwrite(data, 1, (size_t)length, stdout);
  }
  if (endOfLine) {
    fputc('\n', stdout);
  }
  fflush(stdout);
}
#endif

static stride_rt_writer s_writer = default_writer;
static void *s_writerContext = NULL;

/* Sends Print and PrintLn output somewhere other than stdout, e.g. to a host
 * that shows it in its own window. NULL restores stdout. */
STRIDE_EXPORT void stride_runtime_set_writer(stride_rt_writer writer,
                                             void *context) {
  s_writer = writer ? writer : default_writer;
  s_writerContext = writer ? context : NULL;
}

/* ---- Locale-independent numbers ----------------------------------------- */

#if !defined(STRIDE_RT_NO_STDIO)
#if defined(_MSC_VER)
static _locale_t c_numeric_locale(void) {
  static _locale_t locale = NULL;
  if (!locale) {
    /* Two threads may both create one the first time; one copy is then
     * never freed, which is harmless. */
    locale = _create_locale(LC_NUMERIC, "C");
  }
  return locale;
}
#else
static locale_t c_numeric_locale(void) {
  static locale_t locale = (locale_t)0;
  if (!locale) {
    locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  }
  return locale;
}
#endif
#endif

/* Reads a real number with '.' as the decimal separator. Returns false when
 * the text does not start with a number. */
static bool parse_real(const char *text, stride_real_t *value) {
  char *end = NULL;
#if defined(STRIDE_RT_NO_STDIO)
  double result = strtod(text, &end); /* avr-libc has no locales */
#elif defined(_MSC_VER)
  double result = _strtod_l(text, &end, c_numeric_locale());
#else
  double result = strtod_l(text, &end, c_numeric_locale());
#endif
  if (end == text) {
    return false;
  }
  *value = (stride_real_t)result;
  return true;
}

/* Formats a real number with '.' as the decimal separator. kind is 'f'
 * (fixed, precision digits), 'e'/'E' (exponent) or 'g' (shortest). */
static int format_real(char *buffer, size_t size, stride_real_t value,
                       char kind, int width, int precision, bool zeroPad) {
#if defined(STRIDE_RT_NO_STDIO)
  /* avr-libc: no float printf by default; dtostrf/dtostre are always there. */
  if (size < 24) {
    return 0;
  }
  if (kind == 'e' || kind == 'E') {
    dtostre(value, buffer, precision >= 0 ? precision : 6,
            kind == 'E' ? DTOSTR_UPPERCASE : 0);
  } else {
    dtostrf(value, zeroPad ? 0 : width, precision >= 0 ? precision : 6, buffer);
  }
  return (int)strlen(buffer);
#else
  char spec[16];
  size_t n = 0;
  spec[n++] = '%';
  if (zeroPad) {
    spec[n++] = '0';
  }
  spec[n++] = '*';
  if (precision >= 0) {
    spec[n++] = '.';
    spec[n++] = '*';
  }
  spec[n++] = kind;
  spec[n] = '\0';
#if defined(_MSC_VER)
  return precision >= 0
             ? _snprintf_l(buffer, size, spec, c_numeric_locale(), width,
                           precision, (double)value)
             : _snprintf_l(buffer, size, spec, c_numeric_locale(), width,
                           (double)value);
#else
  locale_t previous = uselocale(c_numeric_locale());
  int written = precision >= 0
                    ? snprintf(buffer, size, spec, width, precision,
                               (double)value)
                    : snprintf(buffer, size, spec, width, (double)value);
  uselocale(previous);
  return written;
#endif
#endif
}

/* Writes value in base 10 or 16 into buffer (at least 34 bytes), padded to
 * width with spaces or zeros. Returns the number of characters. */
static int format_int(char *buffer, int32_t value, int base, bool upper,
                      int width, bool zeroPad) {
  char digits[33];
  int count = 0;
  bool negative = base == 10 && value < 0;
  /* Hexadecimal shows the 32-bit pattern, as printf's %x does. */
  uint32_t magnitude =
      negative ? (uint32_t)0 - (uint32_t)value : (uint32_t)value;
  const char *symbols = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  do {
    digits[count++] = symbols[magnitude % (uint32_t)base];
    magnitude /= (uint32_t)base;
  } while (magnitude != 0);

  int length = count + (negative ? 1 : 0);
  int pad = width > length ? width - length : 0;
  if (pad > 32) {
    pad = 32;
  }
  int n = 0;
  if (!zeroPad) {
    while (pad-- > 0) {
      buffer[n++] = ' ';
    }
  }
  if (negative) {
    buffer[n++] = '-';
  }
  if (zeroPad) {
    while (pad-- > 0) {
      buffer[n++] = '0';
    }
  }
  while (count > 0) {
    buffer[n++] = digits[--count];
  }
  buffer[n] = '\0';
  return n;
}

/* ---- Searching ---------------------------------------------------------- */

static int32_t find_from(const StrideString *haystack,
                         const StrideString *needle, int32_t start) {
  if (is_empty(haystack) || is_empty(needle)) {
    return -1;
  }
  if (start < 0) {
    start = 0;
  }
  if (needle->length > haystack->length - start) {
    return -1;
  }
  const char first = needle->data[0];
  const size_t rest = (size_t)needle->length - 1;
  const int32_t last = haystack->length - needle->length;
  for (int32_t i = start; i <= last; ++i) {
    if (haystack->data[i] == first &&
        memcmp(haystack->data + i + 1, needle->data + 1, rest) == 0) {
      return i;
    }
  }
  return -1;
}

static bool in_set(char c, const StrideString *set) {
  return memchr(set->data, c, (size_t)set->length) != NULL;
}

static const StrideString *trim_set(const StrideString *chars) {
  return is_empty(chars) ? &kWhitespace : chars;
}

static const StrideString *delimiter(const StrideString *delim) {
  return is_empty(delim) ? &kSpace : delim;
}

/* ---- Entry points ------------------------------------------------------- */

/* Gives the String in *slot its own copy of its characters: copies them into
 * buffer (capacity bytes, the String's maxSize; longer values are cut off) and
 * points the String at it. The compiler calls this after every write to a
 * String variable, so the value no longer depends on where it came from: a
 * literal, the caller's scratch or the host. The source may lie in buffer itself
 * (e.g. a Substring of the same String). */
STRIDE_EXPORT stride_status_t __stride_jit_string_store(StrideString *slot,
                                                        char *buffer,
                                                        int32_t capacity) {
  if (!slot || !buffer || capacity < 0) {
    return STRIDE_STATUS_INVALID;
  }
  int32_t length = (slot->data && slot->length > 0) ? slot->length : 0;
  stride_status_t status = STRIDE_STATUS_OK;
  if (length > capacity) {
    length = capacity;
    status = STRIDE_STATUS_TRUNCATED;
  }
  if (length > 0 && slot->data != buffer) {
    memmove(buffer, slot->data, (size_t)length);
  }
  slot->data = buffer;
  slot->length = length;
  return status;
}

STRIDE_EXPORT void __stride_jit_string_substring(const StrideString *src,
                                                 int32_t start, int32_t len,
                                                 StrideString *out) {
  if (!out) {
    return;
  }
  if (is_empty(src) || start < 0 || start >= src->length) {
    *out = kNull;
    return;
  }
  int32_t available = src->length - start;
  *out = make_string(src->data + start,
                     (len < 0 || len > available) ? available : len);
}

STRIDE_EXPORT void __stride_jit_string_trim_left(const StrideString *src,
                                                 const StrideString *chars,
                                                 StrideString *out) {
  if (!out) {
    return;
  }
  if (is_empty(src)) {
    *out = kNull;
    return;
  }
  const StrideString *set = trim_set(chars);
  int32_t start = 0;
  while (start < src->length && in_set(src->data[start], set)) {
    ++start;
  }
  *out = make_string(src->data + start, src->length - start);
}

STRIDE_EXPORT void __stride_jit_string_trim_right(const StrideString *src,
                                                  const StrideString *chars,
                                                  StrideString *out) {
  if (!out) {
    return;
  }
  if (is_empty(src)) {
    *out = kNull;
    return;
  }
  const StrideString *set = trim_set(chars);
  int32_t end = src->length;
  while (end > 0 && in_set(src->data[end - 1], set)) {
    --end;
  }
  *out = make_string(src->data, end);
}

STRIDE_EXPORT void __stride_jit_string_trim(const StrideString *src,
                                            const StrideString *chars,
                                            StrideString *out) {
  if (!out) {
    return;
  }
  StrideString left;
  __stride_jit_string_trim_left(src, chars, &left);
  if (is_empty(&left)) {
    *out = left.data ? make_string(left.data, 0) : kNull;
    return;
  }
  __stride_jit_string_trim_right(&left, chars, out);
}

STRIDE_EXPORT int32_t __stride_jit_string_find(const StrideString *haystack,
                                               const StrideString *needle,
                                               int32_t start) {
  return find_from(haystack, needle, start);
}

STRIDE_EXPORT int32_t __stride_jit_string_find_last(
    const StrideString *haystack, const StrideString *needle) {
  if (is_empty(haystack) || is_empty(needle) ||
      needle->length > haystack->length) {
    return -1;
  }
  for (int32_t i = haystack->length - needle->length; i >= 0; --i) {
    if (memcmp(haystack->data + i, needle->data, (size_t)needle->length) ==
        0) {
      return i;
    }
  }
  return -1;
}

STRIDE_EXPORT bool __stride_jit_string_contains(const StrideString *haystack,
                                                const StrideString *needle) {
  return find_from(haystack, needle, 0) >= 0;
}

STRIDE_EXPORT bool __stride_jit_string_starts_with(const StrideString *str,
                                                   const StrideString *prefix) {
  if (!str || !prefix || !str->data || !prefix->data ||
      prefix->length > str->length) {
    return false;
  }
  return memcmp(str->data, prefix->data, (size_t)prefix->length) == 0;
}

STRIDE_EXPORT bool __stride_jit_string_ends_with(const StrideString *str,
                                                 const StrideString *suffix) {
  if (!str || !suffix || !str->data || !suffix->data ||
      suffix->length > str->length) {
    return false;
  }
  return memcmp(str->data + (str->length - suffix->length), suffix->data,
                (size_t)suffix->length) == 0;
}

STRIDE_EXPORT bool __stride_jit_string_equal(const StrideString *a,
                                             const StrideString *b) {
  if (!a || !b) {
    return a == b;
  }
  if (a->length != b->length) {
    return false;
  }
  if (a->length == 0) {
    return true;
  }
  if (!a->data || !b->data) {
    return false;
  }
  return memcmp(a->data, b->data, (size_t)a->length) == 0;
}

STRIDE_EXPORT int32_t __stride_jit_string_compare(const StrideString *a,
                                                  const StrideString *b) {
  if (!a && !b) {
    return 0;
  }
  if (!a) {
    return -1;
  }
  if (!b) {
    return 1;
  }
  int32_t common = a->length < b->length ? a->length : b->length;
  if (common > 0 && a->data && b->data) {
    int cmp = memcmp(a->data, b->data, (size_t)common);
    if (cmp != 0) {
      return cmp < 0 ? -1 : 1;
    }
  }
  if (a->length != b->length) {
    return a->length < b->length ? -1 : 1;
  }
  return 0;
}

STRIDE_EXPORT int32_t __stride_jit_string_length(const StrideString *str) {
  return (!str || !str->data || str->length < 0) ? 0 : str->length;
}

STRIDE_EXPORT bool __stride_jit_string_is_empty(const StrideString *str) {
  return is_empty(str);
}

static void set_status(stride_status_t *status, stride_status_t value) {
  if (status) {
    *status = value;
  }
}

/* Leading whitespace, an optional sign and decimal digits; anything after the
 * digits is ignored. Returns defaultVal, with status Invalid, when there are
 * no digits, and with status OutOfRange when the number does not fit in 32
 * bits. */
STRIDE_EXPORT int32_t __stride_jit_string_parse_int(const StrideString *str,
                                                    int32_t defaultVal,
                                                    stride_status_t *status) {
  set_status(status, STRIDE_STATUS_OK);
  if (is_empty(str)) {
    set_status(status, STRIDE_STATUS_INVALID);
    return defaultVal;
  }
  const char *p = str->data;
  const char *end = str->data + str->length;
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ||
                     *p == '\f' || *p == '\v')) {
    ++p;
  }
  bool negative = false;
  if (p < end && (*p == '+' || *p == '-')) {
    negative = *p == '-';
    ++p;
  }
  /* Accumulate as a negative number so INT32_MIN fits. */
  int32_t value = 0;
  bool anyDigit = false;
  while (p < end && *p >= '0' && *p <= '9') {
    int32_t digit = *p - '0';
    if (value < (INT32_MIN + digit) / 10) {
      set_status(status, STRIDE_STATUS_OUT_OF_RANGE);
      return defaultVal;
    }
    value = value * 10 - digit;
    anyDigit = true;
    ++p;
  }
  if (!anyDigit) {
    set_status(status, STRIDE_STATUS_INVALID);
    return defaultVal;
  }
  if (!negative) {
    if (value == INT32_MIN) {
      set_status(status, STRIDE_STATUS_OUT_OF_RANGE);
      return defaultVal;
    }
    value = -value;
  }
  return value;
}

/* Leading whitespace, then a number as strtod reads it with '.' as the
 * decimal separator; anything after it is ignored. Returns defaultVal with
 * status Invalid when there is no number. */
STRIDE_EXPORT stride_real_t __stride_jit_string_parse_real(
    const StrideString *str, stride_real_t defaultVal,
    stride_status_t *status) {
  set_status(status, STRIDE_STATUS_OK);
  if (is_empty(str)) {
    set_status(status, STRIDE_STATUS_INVALID);
    return defaultVal;
  }
  /* strtod needs a terminated string; numbers longer than this are not
   * meaningful anyway. */
  char text[64];
  size_t length = (size_t)str->length < sizeof(text) - 1
                      ? (size_t)str->length
                      : sizeof(text) - 1;
  memcpy(text, str->data, length);
  text[length] = '\0';
  stride_real_t value;
  if (!parse_real(text, &value)) {
    set_status(status, STRIDE_STATUS_INVALID);
    return defaultVal;
  }
  return value;
}

STRIDE_EXPORT void __stride_jit_string_print(const StrideString *str) {
  if (is_empty(str)) {
    return;
  }
  s_writer(s_writerContext, str->data, str->length, false);
}

STRIDE_EXPORT void __stride_jit_string_println(const StrideString *str) {
  if (is_empty(str)) {
    s_writer(s_writerContext, "", 0, true);
    return;
  }
  s_writer(s_writerContext, str->data, str->length, true);
}

/* Calls visit for each token of src split at delim, in order, until it
 * returns false. An empty src has no tokens; otherwise n delimiters give
 * n + 1 tokens, some of which may be empty. */
typedef bool (*token_visitor)(void *context, int32_t index,
                              const StrideString *token);

static int32_t for_each_token(const StrideString *src,
                              const StrideString *delim, token_visitor visit,
                              void *context) {
  if (is_empty(src)) {
    return 0;
  }
  const StrideString *d = delimiter(delim);
  int32_t index = 0;
  int32_t start = 0;
  for (;;) {
    int32_t found = find_from(src, d, start);
    int32_t tokenEnd = found >= 0 ? found : src->length;
    StrideString token = make_string(src->data + start, tokenEnd - start);
    if (visit && !visit(context, index, &token)) {
      return index + 1;
    }
    ++index;
    if (found < 0) {
      return index;
    }
    start = found + d->length;
  }
}

typedef struct TokenizeContext {
  StrideString *tokens;
  int32_t maxTokens;
  bool overflow;
} TokenizeContext;

static bool tokenize_visit(void *context, int32_t index,
                           const StrideString *token) {
  TokenizeContext *c = (TokenizeContext *)context;
  if (index < c->maxTokens) {
    if (c->tokens) {
      c->tokens[index] = *token;
    }
  } else {
    c->overflow = true;
  }
  return true;
}

STRIDE_EXPORT void __stride_jit_string_tokenize(
    const StrideString *src, const StrideString *delim,
    StrideString *outTokens, int32_t maxTokens, int32_t *outCount,
    bool *outOverflow) {
  TokenizeContext context = {outTokens, maxTokens, false};
  int32_t count = for_each_token(src, delim, tokenize_visit, &context);
  if (outCount) {
    *outCount = count;
  }
  if (outOverflow) {
    *outOverflow = context.overflow;
  }
}

typedef struct TokenAtContext {
  int32_t wanted;
  StrideString *out;
} TokenAtContext;

static bool token_at_visit(void *context, int32_t index,
                           const StrideString *token) {
  TokenAtContext *c = (TokenAtContext *)context;
  if (index == c->wanted) {
    *c->out = *token;
    return false;
  }
  return true;
}

STRIDE_EXPORT void __stride_jit_string_token_at(const StrideString *src,
                                                const StrideString *delim,
                                                int32_t index,
                                                StrideString *out) {
  if (!out) {
    return;
  }
  *out = kNull;
  if (index < 0) {
    return;
  }
  TokenAtContext context = {index, out};
  for_each_token(src, delim, token_at_visit, &context);
}

STRIDE_EXPORT int32_t __stride_jit_string_token_count(
    const StrideString *src, const StrideString *delim) {
  return for_each_token(src, delim, NULL, NULL);
}

STRIDE_EXPORT stride_status_t __stride_jit_string_concat_n(const StrideString *views,
                                                int32_t count, char *scratch,
                                                int32_t capacity,
                                                StrideString *out) {
  if (!out) {
    return STRIDE_STATUS_INVALID;
  }
  if (!views || count <= 0) {
    *out = kEmpty;
    return STRIDE_STATUS_OK;
  }
  size_t total = 0;
  for (int32_t i = 0; i < count; ++i) {
    if (!is_empty(&views[i])) {
      total += (size_t)views[i].length;
    }
  }
  if (total == 0) {
    *out = kEmpty;
    return STRIDE_STATUS_OK;
  }
  char *dest = scratch;
  size_t available = (scratch && capacity > 0) ? (size_t)capacity : 0;
  if (available > total) {
    available = total;
  }
  size_t written = 0;
  for (int32_t i = 0; i < count && written < available; ++i) {
    if (!is_empty(&views[i])) {
      size_t n = (size_t)views[i].length;
      if (n > available - written) {
        n = available - written;
      }
      memcpy(dest + written, views[i].data, n);
      written += n;
    }
  }
  *out = written ? make_string(dest, (int32_t)written) : kEmpty;
  return written < total ? STRIDE_STATUS_TRUNCATED : STRIDE_STATUS_OK;
}

STRIDE_EXPORT stride_status_t __stride_jit_string_repeat(const StrideString *str,
                                              int32_t count, char *scratch,
                                              int32_t capacity,
                                              StrideString *out) {
  if (!out) {
    return STRIDE_STATUS_INVALID;
  }
  if (is_empty(str) || count <= 0) {
    *out = kEmpty;
    return STRIDE_STATUS_OK;
  }
  size_t total = (size_t)str->length * (size_t)count;
  char *dest = scratch;
  size_t available = (scratch && capacity > 0) ? (size_t)capacity : 0;
  if (available > total) {
    available = total;
  }
  size_t written = 0;
  while (written < available) {
    size_t n = (size_t)str->length;
    if (n > available - written) {
      n = available - written;
    }
    memcpy(dest + written, str->data, n);
    written += n;
  }
  *out = written ? make_string(dest, (int32_t)written) : kEmpty;
  return written < total ? STRIDE_STATUS_TRUNCATED : STRIDE_STATUS_OK;
}

typedef struct FormatOutput {
  char *dest;
  size_t written;
  size_t available;
  bool truncated;
} FormatOutput;

static void append(FormatOutput *o, const char *src, size_t length) {
  if (!src || length == 0) {
    return;
  }
  if (o->written >= o->available) {
    o->truncated = true;
    return;
  }
  if (length > o->available - o->written) {
    length = o->available - o->written;
    o->truncated = true;
  }
  memcpy(o->dest + o->written, src, length);
  o->written += length;
}

static void append_padding(FormatOutput *o, int count) {
  while (count-- > 0) {
    append(o, " ", 1);
  }
}

/* Replaces {} and {:spec} placeholders in fmt with args, in order. A spec is
 * [0][width][.precision][type]; types are d, x, X (Int), f, e, E (Real),
 * b (Switch as true/false), d (Switch as 1/0). {{ and }} are literal braces.
 * A value is padded to at most 64 characters. The result is cut off at
 * capacity, the size of scratch. */
STRIDE_EXPORT stride_status_t __stride_jit_string_format(const StrideString *fmt,
                                              const StrideFormatArg *args,
                                              int32_t count, char *scratch,
                                              int32_t capacity,
                                              StrideString *out) {
  if (!out) {
    return STRIDE_STATUS_INVALID;
  }
  if (is_empty(fmt)) {
    *out = kEmpty;
    return STRIDE_STATUS_OK;
  }
  FormatOutput o;
  o.dest = scratch;
  o.available = (scratch && capacity > 0) ? (size_t)capacity : 0;
  o.written = 0;
  o.truncated = false;

  const char *p = fmt->data;
  const char *end = fmt->data + fmt->length;
  int32_t argIndex = 0;

  while (p < end) {
    if (*p == '{' && p + 1 < end && p[1] == '{') {
      append(&o, "{", 1);
      p += 2;
      continue;
    }
    if (*p == '}' && p + 1 < end && p[1] == '}') {
      append(&o, "}", 1);
      p += 2;
      continue;
    }
    if (*p != '{') {
      append(&o, p, 1);
      ++p;
      continue;
    }
    const char *close = p + 1;
    while (close < end && *close != '}') {
      ++close;
    }
    if (close >= end) { /* no closing brace: keep the text as it is */
      append(&o, p, (size_t)(end - p));
      break;
    }
    const char *spec = p + 1;
    const char *specEnd = close;
    p = close + 1;
    if (!args || argIndex >= count) {
      continue;
    }
    const StrideFormatArg *arg = &args[argIndex++];

    bool zeroPad = false;
    int width = 0;
    int precision = -1;
    char type = '\0';
    if (spec < specEnd && *spec == ':') {
      const char *s = spec + 1;
      if (s < specEnd && *s == '0') {
        zeroPad = true;
        ++s;
      }
      while (s < specEnd && *s >= '0' && *s <= '9') {
        if (width < 1000) {
          width = width * 10 + (*s - '0');
        }
        ++s;
      }
      if (s < specEnd && *s == '.') {
        ++s;
        precision = 0;
        while (s < specEnd && *s >= '0' && *s <= '9') {
          if (precision < 100) {
            precision = precision * 10 + (*s - '0');
          }
          ++s;
        }
      }
      if (s < specEnd) {
        type = *s;
      }
    }
    if (width > 64) {
      width = 64;
    }

    char number[128];
    int n = 0;
    switch (arg->typeTag) {
    case STRIDE_FMT_ARG_INT:
      if (type == 'x' || type == 'X') {
        n = format_int(number, arg->intVal, 16, type == 'X', width, zeroPad);
      } else {
        n = format_int(number, arg->intVal, 10, false, width, zeroPad);
      }
      break;
    case STRIDE_FMT_ARG_REAL: {
      char kind = 'g';
      if (precision >= 0 || type == 'f') {
        kind = 'f';
      } else if (type == 'e' || type == 'E') {
        kind = type;
      }
      n = format_real(number, sizeof(number), arg->realVal, kind, width,
                      precision, zeroPad);
      break;
    }
    case STRIDE_FMT_ARG_SWITCH: {
      const char *text;
      if (type == 'd') {
        text = arg->intVal ? "1" : "0";
      } else if (type == 'b') {
        text = arg->intVal ? "true" : "false";
      } else {
        text = arg->intVal ? "on" : "off";
      }
      append(&o, text, strlen(text));
      break;
    }
    case STRIDE_FMT_ARG_STRING: {
      int32_t length = is_empty(&arg->strVal) ? 0 : arg->strVal.length;
      if (width > length) {
        append_padding(&o, width - length);
      }
      append(&o, arg->strVal.data, (size_t)length);
      break;
    }
    default:
      break;
    }
    if (n > 0) {
      append(&o, number,
             (size_t)n < sizeof(number) ? (size_t)n : sizeof(number) - 1);
    }
  }

  *out = o.written ? make_string(o.dest, (int32_t)o.written) : kEmpty;
  return o.truncated ? STRIDE_STATUS_TRUNCATED : STRIDE_STATUS_OK;
}
