#ifndef STRIDE_STATELAYOUT_HPP
#define STRIDE_STATELAYOUT_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace strd {

// Where each variable of a domain's state lives in the state block, for the
// target the program was compiled for (byte order, pointer size, field
// offsets and widths). With it, the state's bytes can be read without the
// compiler or the JIT: from the JIT's own memory, a simulator's memory, or a
// dump sent by a board over serial. No LLVM types appear here, so a host
// tool can use it on its own.
struct StateLayout {
  struct Field {
    enum class Kind { Switch, Integer, Real, String, Other };
    std::string name;
    Kind kind{Kind::Other};
    bool isSigned{true};       // Integer: the data type's sign
    uint64_t offset{0};        // first element
    uint64_t count{1};         // elements, 1 for a scalar
    uint64_t elementSize{0};   // bytes from one element to the next
    uint64_t valueSize{0};     // bytes of the value itself (1 for a Switch)
    // String: element i's length is a signed integer of lengthSize bytes at
    // offset + i * elementSize + lengthOffset, and its characters start at
    // charsOffset + i * capacity. capacity is 0 for a String without
    // characters of its own (a constant), which cannot be read from bytes.
    uint64_t lengthOffset{0};
    uint64_t lengthSize{0};
    uint64_t charsOffset{0};
    uint64_t capacity{0};
  };

  std::string domain;
  std::string targetTriple; // empty: the host
  bool littleEndian{true};
  uint64_t pointerSize{8};
  uint64_t size{0};      // bytes of the state struct
  uint64_t alignment{1}; // the block's alignment, including the cache line
  std::vector<Field> fields; // in offset order

  const Field *find(const std::string &name) const;
  // The layout as JSON, for tools that read state dumps.
  std::string toJson() const;
};

// A value read from state bytes: nothing (unreadable), a Switch, an integer,
// a real or a String's text.
using StateValue =
    std::variant<std::monostate, bool, int64_t, double, std::string>;

// Reads element `index` of field from a state block of `size` bytes laid
// out as described by layout. Returns std::monostate when the field cannot
// be read (out of range, Other, a String without its own characters).
StateValue readStateValue(const StateLayout &layout,
                          const StateLayout::Field &field,
                          const uint8_t *bytes, size_t size,
                          uint64_t index = 0);

// A variable that lives in a global of its own rather than in a domain's
// state block (a subdomain's shared variables, for instance). Its address
// comes from the symbol table: the JIT's, or the firmware's ELF. Its field
// offsets are relative to the global; a String's characters are in a
// second global, charsSymbol, at charsOffset 0.
struct GlobalLayout {
  StateLayout::Field field;
  std::string symbol;
  std::string charsSymbol; // String: the global holding its characters
  uint64_t size{0};        // bytes of the global
};

// Everything needed to read a program's variables from memory: the target's
// byte order and pointer size, each domain's state block, and the globals.
struct ProgramLayout {
  std::string targetTriple; // empty: the host
  bool littleEndian{true};
  uint64_t pointerSize{8};
  std::vector<StateLayout> domains;
  std::vector<GlobalLayout> globals;

  const StateLayout *findDomain(const std::string &name) const;
  const GlobalLayout *findGlobal(const std::string &name) const;
  std::string toJson() const;
};

// Reads element `index` of a global from its bytes (`size`) and, for a
// String, the bytes of its characters global (`chars`, `charsSize`).
StateValue readGlobalValue(const ProgramLayout &layout,
                           const GlobalLayout &global, const uint8_t *bytes,
                           size_t size, const uint8_t *chars = nullptr,
                           size_t charsSize = 0, uint64_t index = 0);

} // namespace strd

#endif // STRIDE_STATELAYOUT_HPP
