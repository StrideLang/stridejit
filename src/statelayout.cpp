#include "stride/stridejit/statelayout.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

using namespace strd;

const StateLayout::Field *StateLayout::find(const std::string &name) const {
  for (const auto &field : fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

static std::string jsonString(const std::string &text) {
  std::string out = "\"";
  for (char c : text) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char escaped[8];
        std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
        out += escaped;
      } else {
        out += c;
      }
    }
  }
  return out + "\"";
}

static const char *kindName(StateLayout::Field::Kind kind) {
  switch (kind) {
  case StateLayout::Field::Kind::Switch:
    return "switch";
  case StateLayout::Field::Kind::Integer:
    return "integer";
  case StateLayout::Field::Kind::Real:
    return "real";
  case StateLayout::Field::Kind::String:
    return "string";
  default:
    return "other";
  }
}

std::string StateLayout::toJson() const {
  std::ostringstream os;
  os << "{\n  \"domain\": " << jsonString(domain)
     << ",\n  \"target\": " << jsonString(targetTriple)
     << ",\n  \"littleEndian\": " << (littleEndian ? "true" : "false")
     << ",\n  \"pointerSize\": " << pointerSize << ",\n  \"size\": " << size
     << ",\n  \"alignment\": " << alignment << ",\n  \"fields\": [";
  for (size_t i = 0; i < fields.size(); ++i) {
    const Field &f = fields[i];
    os << (i ? ",\n" : "\n") << "    {\"name\": " << jsonString(f.name)
       << ", \"kind\": \"" << kindName(f.kind) << "\", \"offset\": " << f.offset
       << ", \"count\": " << f.count << ", \"elementSize\": " << f.elementSize
       << ", \"valueSize\": " << f.valueSize;
    if (f.kind == Field::Kind::Integer) {
      os << ", \"signed\": " << (f.isSigned ? "true" : "false");
    }
    if (f.kind == Field::Kind::String) {
      os << ", \"lengthOffset\": " << f.lengthOffset
         << ", \"lengthSize\": " << f.lengthSize
         << ", \"charsOffset\": " << f.charsOffset
         << ", \"capacity\": " << f.capacity;
    }
    os << "}";
  }
  os << "\n  ]\n}\n";
  return os.str();
}

// The unsigned integer of `width` bytes at bytes, in the layout's order
static uint64_t readUnsigned(const uint8_t *bytes, uint64_t width,
                             bool littleEndian) {
  uint64_t value = 0;
  for (uint64_t i = 0; i < width; ++i) {
    uint64_t byte = littleEndian ? bytes[i] : bytes[width - 1 - i];
    value |= byte << (8 * i);
  }
  return value;
}

static int64_t signExtend(uint64_t value, uint64_t width) {
  if (width >= 8) {
    return static_cast<int64_t>(value);
  }
  const uint64_t sign = uint64_t{1} << (8 * width - 1);
  return static_cast<int64_t>((value ^ sign) - sign);
}

// Reads a value whose String characters, if any, are in charsBytes (which
// is bytes itself for a state block)
static StateValue readValue(bool littleEndian, const StateLayout::Field &field,
                            const uint8_t *bytes, size_t size,
                            const uint8_t *charsBytes, size_t charsSize,
                            uint64_t index) {
  StateLayout layout;
  layout.littleEndian = littleEndian;
  if (!bytes || index >= field.count) {
    return std::monostate{};
  }
  const uint64_t at = field.offset + index * field.elementSize;
  auto fits = [&](uint64_t offset, uint64_t length) {
    return offset <= size && length <= size - offset;
  };
  switch (field.kind) {
  case StateLayout::Field::Kind::Switch:
    if (!fits(at, 1)) {
      return std::monostate{};
    }
    return (bytes[at] & 1) != 0;
  case StateLayout::Field::Kind::Integer: {
    if (field.valueSize == 0 || field.valueSize > 8 ||
        !fits(at, field.valueSize)) {
      return std::monostate{};
    }
    uint64_t raw = readUnsigned(bytes + at, field.valueSize, layout.littleEndian);
    return field.isSigned ? signExtend(raw, field.valueSize)
                          : static_cast<int64_t>(raw);
  }
  case StateLayout::Field::Kind::Real: {
    if ((field.valueSize != 4 && field.valueSize != 8) ||
        !fits(at, field.valueSize)) {
      return std::monostate{};
    }
    uint64_t raw = readUnsigned(bytes + at, field.valueSize, layout.littleEndian);
    if (field.valueSize == 4) {
      uint32_t bits = static_cast<uint32_t>(raw);
      float value;
      std::memcpy(&value, &bits, sizeof(value));
      return static_cast<double>(value);
    }
    double value;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }
  case StateLayout::Field::Kind::String: {
    const uint64_t lengthAt = at + field.lengthOffset;
    const uint64_t charsAt = field.charsOffset + index * field.capacity;
    if (field.capacity == 0 || field.lengthSize == 0 ||
        field.lengthSize > 8 || !fits(lengthAt, field.lengthSize) || !charsBytes ||
        charsAt > charsSize || field.capacity > charsSize - charsAt) {
      return std::monostate{};
    }
    int64_t length = signExtend(
        readUnsigned(bytes + lengthAt, field.lengthSize, layout.littleEndian),
        field.lengthSize);
    length = std::clamp<int64_t>(length, 0,
                                 static_cast<int64_t>(field.capacity));
    return std::string(reinterpret_cast<const char *>(charsBytes + charsAt),
                       static_cast<size_t>(length));
  }
  default:
    return std::monostate{};
  }
}

StateValue strd::readStateValue(const StateLayout &layout,
                                const StateLayout::Field &field,
                                const uint8_t *bytes, size_t size,
                                uint64_t index) {
  return readValue(layout.littleEndian, field, bytes, size, bytes, size, index);
}

StateValue strd::readGlobalValue(const ProgramLayout &layout,
                                 const GlobalLayout &global,
                                 const uint8_t *bytes, size_t size,
                                 const uint8_t *chars, size_t charsSize,
                                 uint64_t index) {
  return readValue(layout.littleEndian, global.field, bytes, size, chars,
                   charsSize, index);
}

const StateLayout *ProgramLayout::findDomain(const std::string &name) const {
  for (const auto &domain : domains) {
    if (domain.domain == name) {
      return &domain;
    }
  }
  return nullptr;
}

const GlobalLayout *ProgramLayout::findGlobal(const std::string &name) const {
  for (const auto &global : globals) {
    if (global.field.name == name) {
      return &global;
    }
  }
  return nullptr;
}

std::string ProgramLayout::toJson() const {
  std::ostringstream os;
  os << "{\n\"target\": " << jsonString(targetTriple)
     << ",\n\"littleEndian\": " << (littleEndian ? "true" : "false")
     << ",\n\"pointerSize\": " << pointerSize << ",\n\"domains\": [";
  for (size_t i = 0; i < domains.size(); ++i) {
    os << (i ? ",\n" : "\n") << domains[i].toJson();
  }
  os << "],\n\"globals\": [";
  for (size_t i = 0; i < globals.size(); ++i) {
    const GlobalLayout &g = globals[i];
    const auto &f = g.field;
    os << (i ? ",\n" : "\n") << "  {\"name\": " << jsonString(f.name)
       << ", \"symbol\": " << jsonString(g.symbol) << ", \"size\": " << g.size
       << ", \"kind\": \"" << kindName(f.kind) << "\", \"count\": " << f.count
       << ", \"elementSize\": " << f.elementSize
       << ", \"valueSize\": " << f.valueSize;
    if (f.kind == StateLayout::Field::Kind::Integer) {
      os << ", \"signed\": " << (f.isSigned ? "true" : "false");
    }
    if (f.kind == StateLayout::Field::Kind::String) {
      os << ", \"lengthOffset\": " << f.lengthOffset
         << ", \"lengthSize\": " << f.lengthSize
         << ", \"charsSymbol\": " << jsonString(g.charsSymbol)
         << ", \"capacity\": " << f.capacity;
    }
    os << "}";
  }
  os << "\n]\n}\n";
  return os.str();
}
