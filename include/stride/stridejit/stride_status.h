/*
 * Status codes the Stride runtime reports to compiled code.
 *
 * Plain C, shared by the string runtime, the platform runtimes (AVR, ...) and
 * the compiler. A runtime function that can fail returns one of these, or
 * writes one through a trailing status pointer when its return value is the
 * result. Stride programs never see the return value: the compiler ORs it
 * into the domain's status word (<Domain>.status, sticky until the program
 * clears it) and keeps the last write's code on the variable (<Name>.status).
 *
 * Each code is one bit so a word can hold several at once. Keep the list
 * short: a code names a kind of failure, not where it happened; the compiler
 * knows which call it came from. Bits 0-6 belong to the runtime, bit 7 to the
 * platform.
 */
#ifndef STRIDE_STATUS_H
#define STRIDE_STATUS_H

#include <stdint.h>

typedef uint8_t stride_status_t;

/* Done as asked. */
#define STRIDE_STATUS_OK ((stride_status_t)0)
/* The result did not fit where it had to go and part of it was dropped: a
 * string past its scratch or its maxSize, a value past a buffer. */
#define STRIDE_STATUS_TRUNCATED ((stride_status_t)1)
/* The input cannot be used: text with no number in it, a bad template. */
#define STRIDE_STATUS_INVALID ((stride_status_t)2)
/* A value or index lies outside what is allowed: a number too big for Int, a
 * pin the board does not have. */
#define STRIDE_STATUS_OUT_OF_RANGE ((stride_status_t)4)
/* The resource cannot be used right now: a full transmit buffer, a
 * peripheral that is not ready. */
#define STRIDE_STATUS_UNAVAILABLE ((stride_status_t)8)
/* Set by a platform runtime for a failure of its own. */
#define STRIDE_STATUS_PLATFORM ((stride_status_t)128)

#endif /* STRIDE_STATUS_H */
