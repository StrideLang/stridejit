#ifndef STRIDERUNTIME_RANDOM_H
#define STRIDERUNTIME_RANDOM_H

#include <cstdint>

#include "stride/stridejit/strideruntime_string.h" // STRIDE_EXPORT

// Random numbers for compiled Stride programs (library/1.0/Random.stride).
//
// By default they come from a built-in generator (splitmix64), seeded with
// stride_random_seed. A host that keeps its own random state, so that it can
// save and restore it (a game engine taking snapshots), installs a source
// instead and every call goes to it.

namespace strd {

struct StrideRandomSource {
  void *context{nullptr};
  // A number from min to max, both included. Called with min <= max.
  int32_t (*randomInt)(void *context, int32_t min, int32_t max){nullptr};
};

// The next number of a splitmix64 generator, advancing its state
inline uint64_t splitmix64Next(uint64_t &state) {
  uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

// A number from min to max, both included, from a splitmix64 state. Unbiased
// and the same on every platform (unlike std::uniform_int_distribution).
inline int32_t splitmix64Int(uint64_t &state, int32_t min, int32_t max) {
  if (max <= min) {
    return min;
  }
  const uint64_t range = static_cast<uint64_t>(
                             static_cast<int64_t>(max) - static_cast<int64_t>(min)) +
                         1;
  // Reject the top values that would make some results more likely
  const uint64_t limit = UINT64_MAX - (UINT64_MAX % range);
  uint64_t value;
  do {
    value = splitmix64Next(state);
  } while (value >= limit);
  return static_cast<int32_t>(static_cast<int64_t>(min) +
                              static_cast<int64_t>(value % range));
}

} // namespace strd

extern "C" {

// Called by compiled code
STRIDE_EXPORT int32_t __stride_jit_random_int(int32_t min, int32_t max);

// Host API. A null source goes back to the built-in generator.
STRIDE_EXPORT void stride_random_set_source(const strd::StrideRandomSource *source);
STRIDE_EXPORT void stride_random_seed(uint64_t seed);

} // extern "C"

#endif // STRIDERUNTIME_RANDOM_H
