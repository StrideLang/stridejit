#include "stride/stridejit/strideruntime_sleep.h"

#include <chrono>
#include <thread>

extern "C" {

STRIDE_EXPORT void __stride_jit_sleep(double seconds) {
  if (seconds <= 0.0) {
    return;
  }
  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

STRIDE_EXPORT bool __stride_jit_sleep_tick_ready(int64_t *remainingTicks) {
  if (remainingTicks && *remainingTicks > 0) {
    --(*remainingTicks);
    return false;
  }
  return true;
}

} // extern "C"
