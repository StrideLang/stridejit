/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

    Copyright (C) 2017-2026 Andres Cabrera
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

    Author: Andres Cabrera
*/

#ifndef BUFFERSPEC_HPP
#define BUFFERSPEC_HPP

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "stride/parser/ast.h"

namespace strd {
class DeclarationNode;

/// Resolved layout and trigger accounting of a `buffer` declaration.
/// Everything the compiler needs is computed here once, so validation,
/// storage allocation, push codegen and the reaction frame view agree.
struct BufferSpec {
  enum class Mode { Sliding, Fifo, OverlapAdd, PingPong };
  enum class Trigger { Full, Half, Quarter, Periodic };

  std::string name;
  int capacity{0};
  // Readable window N, public as `Buf.frameSize`
  int frameSize{0};
  // Samples between two built-in triggers (H)
  int hop{0};
  Mode mode{Mode::Sliding};
  Trigger trigger{Trigger::Full};
  // Switch registered through the `trigger` property. Empty when the buffer
  // only uses its built-in trigger mode.
  std::string triggerBlock;
  // Decided by the compiler: storage is doubled when a frame can straddle the
  // end of the ring, so every frame view is contiguous.
  bool mirrored{false};
  std::vector<std::string> errors;

  static BufferSpec fromDeclaration(std::shared_ptr<DeclarationNode> decl,
                                    ASTNode tree);

  bool valid() const { return errors.empty(); }
  bool hasTriggerBlock() const { return !triggerBlock.empty(); }
  int storageSize() const { return mirrored ? 2 * capacity : capacity; }

  // Private state, synthesized next to the storage
  std::string indexName() const { return name + "_index"; }
  std::string countName() const { return name + "_count"; }
  std::string sinceName() const { return name + "_since"; }
  std::string flushName() const { return name + "_flush"; }
  std::string readyName() const { return name + "_ready"; }

  // Resolved sizes, readable as `Buf.<property>` through constants named
  // `<Buffer>_<property>`
  std::vector<std::pair<std::string, int>> constantProperties() const {
    return {{"capacity", capacity}, {"frameSize", frameSize}, {"hop", hop}};
  }
  // Whether the JIT implements reading `Buf.<property>`. Which properties
  // user code may read at all is decided by the schema (propertyAccess).
  bool isReadableByJit(const std::string &property) const;

  // A property as the `buffer` type's schema declares it (typeProperty,
  // inherited types included). `public` and `writeable` default to on.
  struct PropertyAccess {
    bool declared{false};
    bool isPublic{true};
    bool writeable{true};
  };
  static PropertyAccess propertyAccess(const std::string &property,
                                       ASTNode tree);
  static std::vector<std::string> publicProperties(ASTNode tree);
};

} // namespace strd

#endif // BUFFERSPEC_HPP
