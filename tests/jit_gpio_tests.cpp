// Portable GPIO: a program uses gpioPins from a hardware file through the
// DigitalIO modules. Under the JIT framework the registers are simulated
// (strideruntime_gpio.h), so these tests run the same Stride code that runs
// on a board and look at the registers it writes.

#include "gtest/gtest.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/strideruntime_gpio.h"

#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

namespace {

// A simulated microcontroller with two kinds of port: PortA is AVR-like
// (read-modify-write output, a direction register, a pull-up field), PortB is
// STM32-like (32-bit set/clear register, a clock gate, a 2-bit mode field).
const char *kSimMcu = R"(
package SimMcu

import Gpio

device SimMcu {
    platform: JIT
    ports: [
        gpioPort PortA {
            width: 32 output: 4096 input: 4100 direction: 4104
            fields: [
                gpioField Direction { option: Gpio::_PinModes register: 4104 bitsPerPin: 1
                                      options: [Gpio::PinInput, Gpio::PinOutput] codes: [0, 1] },
                gpioField Pull { option: Gpio::_PinPulls register: 4108 bitsPerPin: 1
                                 options: [Gpio::PullNone, Gpio::PullUp] codes: [0, 1] }
            ]
        },
        gpioPort PortB {
            width: 32 output: 8192 input: 8196
            outputSet: 8200 outputClear: 8200 setShift: 0 clearShift: 16
            enable: { register: 12288 bit: 1 }
            fields: [
                gpioField Mode { option: Gpio::_PinModes register: 8204 bitsPerPin: 2
                                 options: [Gpio::PinInput, Gpio::PinOutput] codes: [0, 1] }
            ]
        }
    ]
    gpio: gpioCapabilities {
        emulated: [Gpio::OutputOpenDrain]
    }
}
)";

const char *kSimBoard = R"(
package SimBoard

import SimMcu

hardwareBoard SimBoard {
    platform: JIT
    overlays: SimMcu
    pins: [
        pin P0 { port: PortA bit: 0 },
        pin P1 { port: PortA bit: 1 },
        pin P2 { port: PortA bit: 2 },
        pin Q5 { port: PortB bit: 5 },
        pin Q6 { port: PortB bit: 6 }
    ]
}
)";

// The project's wiring on the AVR-like port
const char *kRigOnPortA = R"(
package SimRig

import Gpio
import SimBoard

hardwareBoard SimRig {
    overlays: SimBoard
}

gpioPin Button { pin: SimBoard::P0 mode: Gpio::PinInput pull: Gpio::PullUp activeLow: on }
gpioPin Status { pin: SimBoard::P1 mode: Gpio::PinOutput initial: off }
gpioPin Relay { pin: SimBoard::P2 mode: Gpio::PinOutput outputType: Gpio::OutputOpenDrain }
)";

// The same signals rewired: Status on the STM32-like port
const char *kRigOnPortB = R"(
package SimRig

import Gpio
import SimBoard

hardwareBoard SimRig {
    overlays: SimBoard
}

gpioPin Button { pin: SimBoard::P0 mode: Gpio::PinInput pull: Gpio::PullUp activeLow: on }
gpioPin Status { pin: SimBoard::Q5 mode: Gpio::PinOutput initial: off }
gpioPin Relay { pin: SimBoard::P2 mode: Gpio::PinOutput outputType: Gpio::OutputOpenDrain }
)";

// The program: nothing in it names the framework's domains or a register.
const char *kProgram = R"(
use JIT version 1.0 on SimRig

import DigitalIO
import SimRig

switch Blink { default: off domain: PlatformLoopDomain }
Blink >> Not() >> Blink;
Blink >> DigitalIO::Led(pin: SimRig::Status);

switch Pressed { default: off domain: PlatformTimerDomain }
SimRig::Button >> DigitalIO::DigitalIn() >> Pressed;
Pressed >> DigitalIO::DigitalOut(pin: SimRig::Relay);
)";

class Project {
public:
  explicit Project(const std::string &name) {
    m_dir = std::filesystem::temp_directory_path() /
            ("stride_gpio_" + name + "_" + std::to_string(std::rand()));
    std::filesystem::create_directories(m_dir);
  }
  ~Project() {
    std::error_code ec;
    std::filesystem::remove_all(m_dir, ec);
  }
  std::string write(const std::string &file, const std::string &text) {
    std::ofstream(m_dir / file) << text;
    return (m_dir / file).generic_string();
  }

private:
  std::filesystem::path m_dir;
};

uint32_t reg(uint32_t address) { return __stride_jit_gpio_peek(address); }

} // namespace

TEST(JITGpioTest, PortableProgramOnReadModifyWritePort) {
  Project project("porta");
  project.write("SimMcu.stride", kSimMcu);
  project.write("SimBoard.stride", kSimBoard);
  project.write("SimRig.stride", kRigOnPortA);
  auto program = project.write("Blink.stride", kProgram);

  __stride_jit_gpio_reset();
  strd::StrideEnvironment strenv;
  ASSERT_TRUE(strenv.generateIr(program));
  ASSERT_TRUE(strenv.compileInMemory());

  auto rootState = strenv.allocateSharedState("RootDomain");
  void *rootArgs[] = {rootState.get()};
  strenv.invoke("RootDomain_init", rootArgs);

  // Startup: levels first, then pulls, then direction, so nothing glitches.
  const auto &writes = strd::gpioSimWrites();
  ASSERT_FALSE(writes.empty());
  size_t firstDirection = writes.size();
  size_t lastLevel = 0;
  for (size_t i = 0; i < writes.size(); ++i) {
    if (writes[i].address == 4104 && firstDirection == writes.size()) {
      firstDirection = i;
    }
    if (writes[i].address == 4096) {
      lastLevel = i;
    }
  }
  EXPECT_LT(lastLevel, firstDirection);
  EXPECT_EQ(reg(4096) & 0x6u, 0u);    // Status off, Relay's output bit low
  EXPECT_EQ(reg(4108) & 0x1u, 0x1u);  // Button pull-up
  EXPECT_EQ(reg(4104) & 0x7u, 0x6u);  // Status out, Relay driving low, Button in

  auto timerState = strenv.allocateSharedState("JitTimerDomain");
  void *timerArgs[] = {timerState.get()};
  auto loopState = strenv.allocateSharedState("JitLoopDomain");
  void *loopArgs[] = {loopState.get()};

  // Button is active low: a low input is a press, which releases the relay
  __stride_jit_gpio_poke(4100, 0x0);
  strenv.invoke("JitTimerDomain_process", timerArgs);
  EXPECT_EQ(reg(4104) & 0x4u, 0x0u);  // released: direction bit cleared
  EXPECT_EQ(reg(4096) & 0x4u, 0x0u);  // open drain never drives high
  __stride_jit_gpio_poke(4100, 0x1);
  strenv.invoke("JitTimerDomain_process", timerArgs);
  EXPECT_EQ(reg(4104) & 0x4u, 0x4u);  // not pressed: driven low

  // The loop blinks Status
  strenv.invoke("JitLoopDomain_process", loopArgs);
  bool first = reg(4096) & 0x2u;
  strenv.invoke("JitLoopDomain_process", loopArgs);
  bool second = reg(4096) & 0x2u;
  EXPECT_NE(first, second);
}

TEST(JITGpioTest, SameProgramRewiredToSetClearPort) {
  Project project("portb");
  project.write("SimMcu.stride", kSimMcu);
  project.write("SimBoard.stride", kSimBoard);
  project.write("SimRig.stride", kRigOnPortB);
  auto program = project.write("Blink.stride", kProgram);

  __stride_jit_gpio_reset();
  strd::StrideEnvironment strenv;
  ASSERT_TRUE(strenv.generateIr(program));
  ASSERT_TRUE(strenv.compileInMemory());

  auto rootState = strenv.allocateSharedState("RootDomain");
  void *rootArgs[] = {rootState.get()};
  strenv.invoke("RootDomain_init", rootArgs);

  const auto &writes = strd::gpioSimWrites();
  ASSERT_FALSE(writes.empty());
  // The port's clock first, then Status's level as one store, mode last
  EXPECT_EQ(writes.front().address, 12288u);
  EXPECT_EQ(reg(12288) & 0x2u, 0x2u);
  EXPECT_EQ((reg(8204) >> 10) & 0x3u, 1u);  // Q5 mode output

  auto loopState = strenv.allocateSharedState("JitLoopDomain");
  void *loopArgs[] = {loopState.get()};
  size_t before = writes.size();
  strenv.invoke("JitLoopDomain_process", loopArgs);
  ASSERT_EQ(writes.size(), before + 1);
  // A Status write is one store to the set/clear register, never a read
  EXPECT_EQ(writes.back().address, 8200u);
  EXPECT_FALSE(writes.back().readModify);
  uint32_t value = writes.back().value;
  EXPECT_TRUE(value == (1u << 5) || value == (1u << 21)) << value;
}

TEST(JITGpioTest, MisusedPinsDoNotCompile) {
  Project project("misuse");
  project.write("SimMcu.stride", kSimMcu);
  project.write("SimBoard.stride", kSimBoard);
  project.write("SimRig.stride", kRigOnPortA);

  // Driving an input pin
  auto drivesInput = project.write("DrivesInput.stride", R"(
use JIT version 1.0 on SimRig
import DigitalIO
import SimRig
switch On { default: on domain: PlatformLoopDomain }
On >> DigitalIO::DigitalOut(pin: SimRig::Button);
)");
  strd::StrideEnvironment a;
  EXPECT_FALSE(a.generateIr(drivesInput));

  // A pin the device can't support
  project.write("BadRig.stride", R"(
package BadRig
import Gpio
import SimBoard
hardwareBoard BadRig { overlays: SimBoard }
gpioPin Sink { pin: SimBoard::P1 mode: Gpio::PinInput pull: Gpio::PullDown }
)");
  auto badPull = project.write("BadPull.stride", R"(
use JIT version 1.0 on BadRig
import DigitalIO
import BadRig
switch Level { default: off domain: PlatformLoopDomain }
BadRig::Sink >> DigitalIO::DigitalIn() >> Level;
)");
  strd::StrideEnvironment b;
  EXPECT_FALSE(b.generateIr(badPull));
}

// On an Arduino Uno the same kind of program compiles to direct register
// access at constant addresses, with no calls into the runtime's pin tables.
// A write of a constant level (the startup writes) is a single sbi/cbi. A
// write of a level known only at run time (Blink) is a read-modify-write of
// the constant address: LLVM merges the two branches into in/and/or/out.
TEST(JITGpioTest, AvrUnoWritesUseConstantRegisters) {
  Project project("uno");
  project.write("BlinkRig.stride", R"(
package BlinkRig

import Gpio
import ArduinoUno

hardwareBoard BlinkRig {
    overlays: ArduinoUno
}

gpioPin Button { pin: ArduinoUno::D2 mode: Gpio::PinInput pull: Gpio::PullUp activeLow: on }
gpioPin Status { pin: ArduinoUno::LED mode: Gpio::PinOutput initial: off }
gpioPin Relay { pin: ArduinoUno::D7 mode: Gpio::PinOutput outputType: Gpio::OutputOpenDrain }
)");
  auto program = project.write("Blink.stride", R"(
use AVR version 1.0 on BlinkRig

import DigitalIO
import BlinkRig

switch Blink { default: off domain: PlatformLoopDomain }
Blink >> Not() >> Blink;
Blink >> DigitalIO::Led(pin: BlinkRig::Status);
0.5 >> Sleep(domain: PlatformLoopDomain);

switch Pressed { default: off domain: PlatformTimerDomain }
BlinkRig::Button >> DigitalIO::DigitalIn() >> Pressed;
Pressed >> DigitalIO::DigitalOut(pin: BlinkRig::Relay);
)");

  strd::StrideEnvironment strenv;
  strenv.setTarget("avr", "atmega328p");
  strenv.setOptimizationLevel(2);
  ASSERT_TRUE(strenv.generateIr(program));
  strenv.emitDomainStateBuffers();

  std::error_code ec;
  auto objPath = std::filesystem::temp_directory_path(ec) / "stride_gpio_uno.o";
  std::filesystem::remove(objPath, ec);
  ASSERT_TRUE(strenv.emitObjectFile(objPath.string(), "avr", "atmega328p", "",
                                    "static", ""));

  std::string ir;
  {
    llvm::raw_string_ostream os(ir);
    strenv.mStrideEnv.TheModule->print(os, nullptr);
  }
  if (const char *dump = std::getenv("STRIDE_GPIO_DUMP_IR")) {
    std::ofstream(dump) << ir;
  }
  EXPECT_EQ(ir.find("__stride_avr_pin_"), std::string::npos) << ir;
  EXPECT_NE(ir.find("@__stride_avr_loop_sleep"), std::string::npos) << ir;
  EXPECT_EQ(ir.find("@__stride_avr_sleep_ms"), std::string::npos) << ir;
  EXPECT_NE(ir.find("store volatile i8"), std::string::npos) << ir;

  // The LED write in the loop: PORTB (data address 37) as a constant
  auto loop = ir.find("@AvrLoopDomain_process()");
  ASSERT_NE(loop, std::string::npos) << ir;
  std::string loopBody = ir.substr(loop, ir.find("\n}", loop) - loop);
#if LLVM_VERSION_MAJOR >= 17
  EXPECT_NE(loopBody.find("store volatile i8"), std::string::npos) << loopBody;
#else
  EXPECT_NE(loopBody.find("i16 37"), std::string::npos) << loopBody;
  EXPECT_NE(ir.find("store volatile i8"), std::string::npos) << ir;
#endif
#if LLVM_VERSION_MAJOR >= 17
  EXPECT_NE(loopBody.find("inttoptr (i16 37 to ptr)"), std::string::npos)
#else
  EXPECT_NE(ir.find("inttoptr (i16 37 to ptr)"), std::string::npos)
#endif
      << loopBody;

  std::ifstream in(objPath, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  in.close();
  std::filesystem::remove(objPath, ec);
  // Startup: Status starts low (cbi PORTB,5 = 0x982D), then becomes an
  // output (sbi DDRB,5 = 0x9A25). Stored little-endian.
  EXPECT_NE(bytes.find(std::string("\x2D\x98", 2)), std::string::npos)
      << "no cbi PORTB,5";
  EXPECT_NE(bytes.find(std::string("\x25\x9A", 2)), std::string::npos)
      << "no sbi DDRB,5";
}

namespace {

void verifyPicoGpioBoard(const char *boardName, const char *triple,
                         const char *cpu, const char *objFileName) {
  Project project(boardName);
  project.write("BlinkRig.stride",
                std::string(R"(
package BlinkRig

import Gpio
import )") + boardName + R"(

hardwareBoard BlinkRig {
    overlays: )" + boardName + R"(
}

gpioPin Button { pin: )"+ boardName + R"(::GP2 mode: Gpio::PinInput pull: Gpio::PullUp activeLow: on }
gpioPin Status { pin: )"+ boardName + R"(::LED mode: Gpio::PinOutput initial: off }
gpioPin Relay  { pin: )"+ boardName + R"(::GP3 mode: Gpio::PinOutput }
)");
  auto program = project.write("Blink.stride", R"(
use Pico version 1.0 on BlinkRig

import DigitalIO
import BlinkRig

switch Blink { default: off domain: PlatformLoopDomain }
Blink >> Not() >> Blink;
Blink >> DigitalIO::Led(pin: BlinkRig::Status);
0.5 >> Sleep(domain: PlatformLoopDomain);

switch Pressed { default: off domain: PlatformTimerDomain }
BlinkRig::Button >> DigitalIO::DigitalIn() >> Pressed;
Pressed >> DigitalIO::DigitalOut(pin: BlinkRig::Relay);
)");

  strd::StrideEnvironment strenv;
  strenv.setTarget(triple, cpu);
  strenv.setOptimizationLevel(2);
  ASSERT_TRUE(strenv.generateIr(program));
  strenv.emitDomainStateBuffers();

  std::error_code ec;
  auto objPath = std::filesystem::temp_directory_path(ec) / objFileName;
  std::filesystem::remove(objPath, ec);
  ASSERT_TRUE(strenv.emitObjectFile(objPath.string(), triple, cpu, "",
                                    "static", ""));
  EXPECT_TRUE(std::filesystem::exists(objPath));
  EXPECT_GT(std::filesystem::file_size(objPath), 0u);
  std::filesystem::remove(objPath, ec);

  std::string ir;
  {
    llvm::raw_string_ostream os(ir);
    strenv.mStrideEnv.TheModule->print(os, nullptr);
  }
  EXPECT_NE(ir.find("@__stride_pico_loop_sleep"), std::string::npos) << ir;
  EXPECT_NE(ir.find("store volatile i32"), std::string::npos) << ir;
  // RP2040 SIO GPIO_OUT_SET is 0xD0000014 (-805306348 / 3489660948);
  // RP2350 SIO GPIO_OUT_SET is 0xD0000018 (-805306344 / 3489660952).
  if (std::string(boardName) == "RaspberryPiPico2") {
    EXPECT_TRUE(ir.find("-805306344") != std::string::npos ||
                ir.find("3489660952") != std::string::npos)
        << ir;
  } else {
    EXPECT_TRUE(ir.find("-805306348") != std::string::npos ||
                ir.find("3489660948") != std::string::npos)
        << ir;
  }
  EXPECT_EQ(ir.find("double"), std::string::npos) << ir;
  if (std::string(triple).rfind("thumbv6m", 0) == 0) {
    EXPECT_EQ(ir.find("cmpxchg"), std::string::npos) << ir;
  }

  auto loop = ir.find("@PicoLoopDomain_process()");
  ASSERT_NE(loop, std::string::npos) << ir;
  std::string loopBody = ir.substr(loop, ir.find("\n}", loop) - loop);
  EXPECT_NE(loopBody.find("store volatile i32"), std::string::npos) << loopBody;
}

} // namespace

TEST(JITGpioTest, PicoWritesUseConstantSioRegisters) {
  verifyPicoGpioBoard("RaspberryPiPico", "thumbv6m-none-eabi", "cortex-m0plus",
                      "stride_gpio_pico.o");
}

TEST(JITGpioTest, Pico2WritesUseConstantSioRegisters) {
  verifyPicoGpioBoard("RaspberryPiPico2", "thumbv8m.main-none-eabihf",
                      "cortex-m33", "stride_gpio_pico2.o");
}

TEST(JITGpioTest, AvrSharedGpioPortAndCrossDomainSignalEmitAtomicInterruptGuards) {
  Project project("uno_isr");
  project.write("IsrRig.stride", R"(
package IsrRig

import Gpio
import ArduinoUno

hardwareBoard IsrRig {
    overlays: ArduinoUno
}

gpioPin IsrLed { pin: ArduinoUno::D8 mode: Gpio::PinOutput initial: off }
gpioPin LoopLed { pin: ArduinoUno::LED mode: Gpio::PinOutput initial: off }
)");
  // Both D8 (PB0) and LED (PB5) live on PortB (data address 37).
  // AvrTimer1IsrDomain writes IsrLed and increments 16-bit IsrTicks.
  // AvrLoopDomain writes LoopLed and reads 16-bit IsrTicks.
  auto program = project.write("IsrBlink.stride", R"(
use AVR version 1.0 on IsrRig

import DigitalIO
import IsrRig

signal IsrTicks { type: Int16 domain: AvrTimer1IsrDomain default: 0 }
switch FastToggle { domain: AvrTimer1IsrDomain default: off }
IsrTicks + 1 >> IsrTicks;
FastToggle >> Not() >> FastToggle;
FastToggle >> DigitalIO::DigitalOut(pin: IsrRig::IsrLed);

signal LoopCopy { type: Int16 domain: AvrLoopDomain default: 0 }
switch SlowToggle { domain: AvrLoopDomain default: off }
IsrTicks >> LoopCopy;
SlowToggle >> Not() >> SlowToggle;
SlowToggle >> DigitalIO::Led(pin: IsrRig::LoopLed);
)");

  strd::StrideEnvironment strenv;
  strenv.setTarget("avr", "atmega328p");
  strenv.setOptimizationLevel(2);
  ASSERT_TRUE(strenv.generateIr(program));

  std::string ir;
  {
    llvm::raw_string_ostream os(ir);
    strenv.mStrideEnv.TheModule->print(os, nullptr);
  }

  // AvrLoopDomain_process must save SREG (address 95 / 0x5F), execute "cli",
  // perform the volatile RMW/read, and restore SREG.
  auto loopPos = ir.find("@AvrLoopDomain_process(");
  ASSERT_NE(loopPos, std::string::npos) << ir;
  std::string loopBody = ir.substr(loopPos, ir.find("\n}", loopPos) - loopPos);
  EXPECT_NE(loopBody.find("asm sideeffect \"cli\""), std::string::npos)
      << loopBody;
  EXPECT_NE(loopBody.find("inttoptr (i16 95 to ptr)"), std::string::npos)
      << loopBody;
  EXPECT_NE(loopBody.find("load volatile i16, ptr @IsrTicks"), std::string::npos)
      << loopBody;

  // AvrTimer1IsrDomain_process must access @IsrTicks with volatile, without
  // redundant cli inside the ISR itself.
  auto isrPos = ir.find("@AvrTimer1IsrDomain_process(");
  ASSERT_NE(isrPos, std::string::npos) << ir;
  std::string isrBody = ir.substr(isrPos, ir.find("\n}", isrPos) - isrPos);
  EXPECT_NE(isrBody.find("store volatile i16"), std::string::npos) << isrBody;
  EXPECT_EQ(isrBody.find("asm sideeffect \"cli\""), std::string::npos)
      << isrBody;
}

TEST(JITGpioTest, PioEncoderEmits16BitInstructionTable) {
  Project project("pio_ws2812");
  auto program = project.write("PioTest.stride", R"(
_domainDefinition PioTxDomain {
    framework: _JitFramework
    parentDomain: RootDomain
    execution: ExecutionHardware
    requiresResources: [_Pio0_Sm0, _PioRegX]
    rate: 8000000
    stateMachines: [BitLoop]
}

signal Word { type: UInt32 domain: PioTxDomain default: 0 }
signal X { type: Int domain: PioTxDomain resources: [_PioRegX] default: 0 }

state BitLoop {
    streams: [
        PioPull(block: on) >> Word;
        7 >> X;
        Word >> PioShiftOut(bits: 1 delay: 2);
        0 >> PioDelay(cycles: 3);
        X - 1 >> X;
        [X, 0] >> Greater() >> BitLoop;
    ]
}
)");

  strd::StrideEnvironment strenv;
  ASSERT_TRUE(strenv.generateIr(program));

  auto *instrGlobal = strenv.mStrideEnv.TheModule->getNamedGlobal(
      "__stride_pio_PioTxDomain_instrs");
  ASSERT_NE(instrGlobal, nullptr);
  EXPECT_TRUE(instrGlobal->isConstant());

  auto *arr = llvm::dyn_cast<llvm::ConstantDataArray>(
      instrGlobal->getInitializer());
  ASSERT_NE(arr, nullptr);
  // 5 instructions:
  // [0] PULL block         (0b100_00000_1_0_1_00000 = 0x80A0)
  // [1] SET X, 7           (0b111_00000_001_00111 = 0xE027)
  // [2] OUT PINS, 1 [2]    (0b011_00010_000_00001 = 0x6201)
  // [3] NOP [2] (3 cycles) (0b101_00010_010_00_010 = 0xA242)
  // [4] JMP X--, 0         (0b000_00000_010_00000 = 0x0040)
  ASSERT_EQ(arr->getNumElements(), 5u);
  EXPECT_EQ(arr->getElementAsInteger(0), 0x80A0u);
  EXPECT_EQ(arr->getElementAsInteger(1), 0xE027u);
  EXPECT_EQ(arr->getElementAsInteger(2), 0x6201u);
  EXPECT_EQ(arr->getElementAsInteger(3), 0xA242u);
  EXPECT_EQ(arr->getElementAsInteger(4), 0x0040u);
}
