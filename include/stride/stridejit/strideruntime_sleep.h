#ifndef STRIDERUNTIME_SLEEP_H
#define STRIDERUNTIME_SLEEP_H

#include <stdint.h>

#include "stride/stridejit/strideruntime_string.h" // STRIDE_EXPORT

#ifdef __cplusplus
extern "C" {
#endif

// Suspends the calling thread for `seconds` seconds (no-op if <= 0).
STRIDE_EXPORT void __stride_jit_sleep(double seconds);

// Checks and decrements a subdomain sleep tick counter; returns true when awake.
STRIDE_EXPORT bool __stride_jit_sleep_tick_ready(int64_t *remainingTicks);

#ifdef __cplusplus
}
#endif

#endif // STRIDERUNTIME_SLEEP_H
