#include "stride/stridejit/strideruntime_gpio.h"

#include <map>
#include <mutex>
#include <vector>

namespace {

std::mutex s_mutex;
std::map<uint32_t, uint32_t> s_registers;
std::vector<strd::GpioSimWrite> s_writes;

void store(uint32_t address, uint32_t value, bool readModify) {
  s_registers[address] = value;
  s_writes.push_back({address, value, readModify});
}

} // namespace

namespace strd {
const std::vector<GpioSimWrite> &gpioSimWrites() { return s_writes; }
} // namespace strd

extern "C" {

STRIDE_EXPORT void __stride_jit_gpio_write(uint8_t level, uint32_t set,
                                           uint32_t clear, uint32_t setMask,
                                           uint32_t clearMask, uint8_t rmw) {
  std::lock_guard<std::mutex> lock(s_mutex);
  // Switch arguments arrive as i1: only the lowest bit is meaningful.
  level &= 1;
  if (rmw & 1) {
    uint32_t value = s_registers[set];
    value = level ? (value | setMask) : (value & ~clearMask);
    store(set, value, true);
  } else {
    store(level ? set : clear, level ? setMask : clearMask, false);
  }
}

STRIDE_EXPORT uint8_t __stride_jit_gpio_read(uint32_t address, uint32_t mask) {
  std::lock_guard<std::mutex> lock(s_mutex);
  auto it = s_registers.find(address);
  return it != s_registers.end() && (it->second & mask) != 0;
}

STRIDE_EXPORT void __stride_jit_gpio_modify(uint32_t address, uint32_t clearMask,
                                            uint32_t setBits, uint8_t rmw) {
  std::lock_guard<std::mutex> lock(s_mutex);
  if (rmw & 1) {
    uint32_t value = s_registers[address];
    store(address, (value & ~clearMask) | setBits, true);
  } else {
    store(address, setBits, false);
  }
}

STRIDE_EXPORT uint32_t __stride_jit_gpio_peek(uint32_t address) {
  std::lock_guard<std::mutex> lock(s_mutex);
  auto it = s_registers.find(address);
  return it == s_registers.end() ? 0 : it->second;
}

STRIDE_EXPORT void __stride_jit_gpio_poke(uint32_t address, uint32_t value) {
  std::lock_guard<std::mutex> lock(s_mutex);
  s_registers[address] = value;
}

STRIDE_EXPORT void __stride_jit_gpio_reset(void) {
  std::lock_guard<std::mutex> lock(s_mutex);
  s_registers.clear();
  s_writes.clear();
}

} // extern "C"
