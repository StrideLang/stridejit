#ifndef STRIDE_PIOENCODER_HPP
#define STRIDE_PIOENCODER_HPP

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/langerror.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace strd {

class StrideCompiler;

struct PioProgram {
  std::string domainName;
  int pioBlock{0};
  int stateMachine{0};
  std::vector<uint16_t> instructions;
  uint8_t wrapBottom{0};
  uint8_t wrapTop{0};
};

class PioEncoder {
public:
  // RP2040 / RP2350 16-bit PIO instruction encoders
  static uint16_t encodeJmp(uint8_t condition, uint8_t targetAddr,
                            uint8_t delaySideSet = 0);
  static uint16_t encodeWait(bool polarity, uint8_t source, uint8_t index,
                             uint8_t delaySideSet = 0);
  static uint16_t encodeIn(uint8_t source, uint8_t bitCount,
                           uint8_t delaySideSet = 0);
  static uint16_t encodeOut(uint8_t dest, uint8_t bitCount,
                            uint8_t delaySideSet = 0);
  static uint16_t encodePush(bool ifFull, bool block,
                             uint8_t delaySideSet = 0);
  static uint16_t encodePull(bool ifEmpty, bool block,
                             uint8_t delaySideSet = 0);
  static uint16_t encodeMov(uint8_t dest, uint8_t op, uint8_t source,
                            uint8_t delaySideSet = 0);
  static uint16_t encodeNop(uint8_t delaySideSet = 0);
  static uint16_t encodeIrq(bool clear, bool wait, uint8_t index,
                            uint8_t delaySideSet = 0);
  static uint16_t encodeSet(uint8_t dest, uint8_t data,
                            uint8_t delaySideSet = 0);

  // Returns true if domainDecl has execution: ExecutionHardware and requires a
  // PIO resource (_Pio0 or _Pio0_Sm0..3).
  static bool isPioDomain(const std::shared_ptr<DeclarationNode> &domainDecl);

  // Compiles a PIO domain's streams and stateMachines into 16-bit PIO
  // instructions, appending LangError::PioInstructionOverflow if > 32.
  static PioProgram
  compileDomain(const std::shared_ptr<DeclarationNode> &domainDecl,
                ASTNode tree, std::vector<LangError> *errors = nullptr);

  // Emits @__stride_pio_<Domain>_instrs ([N x i16]),
  // @__stride_pio_<Domain>_instr_count (i32),
  // @__stride_pio_<Domain>_wrap_bottom (i32), and
  // @__stride_pio_<Domain>_wrap_top (i32) into state.TheModule.
  static void emitGlobals(const PioProgram &program, StrideCompiler &state);
};

} // namespace strd

#endif // STRIDE_PIOENCODER_HPP
