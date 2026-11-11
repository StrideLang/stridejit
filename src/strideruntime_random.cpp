#include "stride/stridejit/strideruntime_random.h"

using namespace strd;

namespace {
uint64_t builtinState = 0;
StrideRandomSource currentSource;
bool hasSource = false;
} // namespace

extern "C" {

STRIDE_EXPORT int32_t __stride_jit_random_int(int32_t min, int32_t max) {
  if (max < min) {
    int32_t swap = min;
    min = max;
    max = swap;
  }
  if (hasSource && currentSource.randomInt) {
    return currentSource.randomInt(currentSource.context, min, max);
  }
  return splitmix64Int(builtinState, min, max);
}

STRIDE_EXPORT void stride_random_set_source(const StrideRandomSource *source) {
  hasSource = source != nullptr;
  currentSource = source ? *source : StrideRandomSource{};
}

STRIDE_EXPORT void stride_random_seed(uint64_t seed) { builtinState = seed; }

} // extern "C"
