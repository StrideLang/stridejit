#ifndef STRIDERUNTIME_GPIO_H
#define STRIDERUNTIME_GPIO_H

#include <stdint.h>

#include "stride/stridejit/strideruntime_string.h" // STRIDE_EXPORT

// A simulated GPIO register file for the JIT framework. The DigitalIO
// modules call these with the register addresses of the board's device, so a
// portable program runs on the desktop exactly as it would on the board,
// and a test can set inputs and read outputs and direction registers.
//
// Registers are 32-bit cells keyed by address; any address may be used.

#ifdef __cplusplus
extern "C" {
#endif

// level ? set : clear, as the GPIO modules write it. rmw: read-modify-write
// of the register at `set`; otherwise a store of the mask at the address.
STRIDE_EXPORT void __stride_jit_gpio_write(uint8_t level, uint32_t set,
                                           uint32_t clear, uint32_t setMask,
                                           uint32_t clearMask, uint8_t rmw);
// (register & mask) != 0
STRIDE_EXPORT uint8_t __stride_jit_gpio_read(uint32_t address, uint32_t mask);
// rmw: register = (register & ~clearMask) | setBits; otherwise a store.
STRIDE_EXPORT void __stride_jit_gpio_modify(uint32_t address, uint32_t clearMask,
                                            uint32_t setBits, uint8_t rmw);

// Test access to the simulated registers.
STRIDE_EXPORT uint32_t __stride_jit_gpio_peek(uint32_t address);
STRIDE_EXPORT void __stride_jit_gpio_poke(uint32_t address, uint32_t value);
STRIDE_EXPORT void __stride_jit_gpio_reset(void);

#ifdef __cplusplus
}

#include <vector>

namespace strd {
// Every store and read-modify-write the simulated GPIO saw, in order.
struct GpioSimWrite {
  uint32_t address;
  uint32_t value;   // the register after the write
  bool readModify;  // a read-modify-write rather than a plain store
};
const std::vector<GpioSimWrite> &gpioSimWrites();
} // namespace strd
#endif

#endif // STRIDERUNTIME_GPIO_H
