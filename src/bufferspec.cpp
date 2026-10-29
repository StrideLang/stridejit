#include "stride/stridejit/bufferspec.hpp"

#include "stride/parser/declarationnode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

namespace strd {

namespace {

// Resolves `Buffer::<Name>` (or a bare `<Name>`) to an index into `names`.
// Returns -1 for anything else.
int resolveBufferConstant(ASTNode node, const std::vector<std::string> &names) {
  if (!node || node->getNodeType() != AST::Entity) {
    return -1;
  }
  auto ns = node->getNamespaceList();
  if (!ns.empty() && !(ns.size() == 1 && ns[0] == "Buffer")) {
    return -1;
  }
  auto name = ASTQuery::getNodeName(node);
  for (size_t i = 0; i < names.size(); i++) {
    if (names[i] == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// typeProperty declarations of the `buffer` schema type, inherited included
std::vector<std::shared_ptr<DeclarationNode>> schemaProperties(ASTNode tree) {
  std::vector<std::shared_ptr<DeclarationNode>> properties;
  for (const auto &port : ASTQuery::getPortsForType("buffer", {}, tree)) {
    if (port && port->getNodeType() == AST::Declaration) {
      properties.push_back(std::static_pointer_cast<DeclarationNode>(port));
    }
  }
  return properties;
}

std::string schemaPropertyName(const std::shared_ptr<DeclarationNode> &port) {
  auto nameNode = port->getPropertyValue("name");
  return nameNode && nameNode->getNodeType() == AST::String
             ? std::static_pointer_cast<ValueNode>(nameNode)->getStringValue()
             : std::string();
}

bool schemaSwitch(const std::shared_ptr<DeclarationNode> &port,
                  const std::string &flag) {
  auto node = port->getPropertyValue(flag);
  if (node && node->getNodeType() == AST::Switch) {
    return std::static_pointer_cast<ValueNode>(node)->getSwitchValue();
  }
  return true; // `public` and `writeable` default to on
}

} // namespace

BufferSpec BufferSpec::fromDeclaration(std::shared_ptr<DeclarationNode> decl,
                                       ASTNode tree) {
  BufferSpec spec;
  if (!decl) {
    spec.errors.push_back("No buffer declaration");
    return spec;
  }
  spec.name = decl->getName();
  std::string where = decl->getFilename() + ":" +
                      std::to_string(decl->getLine()) + " buffer '" +
                      spec.name + "': ";
  auto error = [&](const std::string &text) {
    spec.errors.push_back(where + text);
  };
  // Schema defaults fill unset properties with `none`
  auto property = [&](const std::string &name) -> ASTNode {
    auto node = decl->getPropertyValue(name);
    return node && node->getNodeType() != AST::None ? node : nullptr;
  };
  auto evalInt = [&](const std::string &name, int &out) {
    auto node = property(name);
    if (!node) {
      return false;
    }
    std::vector<LangError> errors;
    auto value = ASTFunctions::evaluateConstInteger(node, {}, tree, &errors);
    if (!errors.empty()) {
      error("'" + name + "' must be a constant integer expression");
      return false;
    }
    out = static_cast<int>(value);
    return true;
  };

  // Read-only properties (ready, count) belong to the buffer, not to its
  // declaration
  for (const auto &port : schemaProperties(tree)) {
    auto propertyName = schemaPropertyName(port);
    if (!propertyName.empty() && !schemaSwitch(port, "writeable") &&
        property(propertyName)) {
      error("'" + propertyName + "' is read-only and can't be declared");
    }
  }

  if (!evalInt("capacity", spec.capacity)) {
    if (!property("capacity")) {
      error("'capacity' is required");
    }
    return spec;
  }
  if (spec.capacity <= 0) {
    error("'capacity' must be greater than 0");
    return spec;
  }

  if (auto modeNode = property("mode")) {
    int mode = resolveBufferConstant(
        modeNode,
        {"ModeSliding", "ModeFifo", "ModeOverlapAdd", "ModePingPong"});
    if (mode < 0) {
      error("unknown mode '" + AST::toText(modeNode) +
            "'. Use one of the Buffer::Mode* constants");
      return spec;
    }
    spec.mode = static_cast<Mode>(mode);
    if (spec.mode == Mode::Fifo || spec.mode == Mode::OverlapAdd) {
      error("Buffer::" + ASTQuery::getNodeName(modeNode) +
            " is not implemented yet");
      return spec;
    }
  }

  int hopProperty = 0;
  bool hasHop = evalInt("hop", hopProperty);
  if (auto triggerNode = property("triggerMode")) {
    int trigger = resolveBufferConstant(
        triggerNode,
        {"TriggerFull", "TriggerHalf", "TriggerQuarter", "TriggerPeriodic"});
    if (trigger < 0) {
      error("unknown triggerMode '" + AST::toText(triggerNode) +
            "'. Use one of the Buffer::Trigger* constants");
      return spec;
    }
    spec.trigger = static_cast<Trigger>(trigger);
  } else if (spec.mode == Mode::PingPong) {
    spec.trigger = Trigger::Half;
  } else if (hasHop) {
    spec.trigger = Trigger::Periodic;
  }

  spec.frameSize =
      spec.mode == Mode::PingPong ? spec.capacity / 2 : spec.capacity;
  evalInt("frameSize", spec.frameSize);

  switch (spec.trigger) {
  case Trigger::Full:
    spec.hop = spec.capacity;
    break;
  case Trigger::Half:
    if (spec.capacity % 2 != 0) {
      error("Buffer::TriggerHalf needs an even capacity");
    }
    spec.hop = spec.capacity / 2;
    break;
  case Trigger::Quarter:
    if (spec.capacity % 4 != 0) {
      error("Buffer::TriggerQuarter needs a capacity that is a multiple of 4");
    }
    spec.hop = spec.capacity / 4;
    break;
  case Trigger::Periodic:
    spec.hop = hasHop ? hopProperty : spec.frameSize;
    break;
  }
  if (hasHop && spec.trigger != Trigger::Periodic && hopProperty != spec.hop) {
    error("'hop' is " + std::to_string(hopProperty) +
          " but the trigger mode advances by " + std::to_string(spec.hop) +
          ". Use Buffer::TriggerPeriodic to set the hop");
  }
  if (!(spec.capacity >= spec.frameSize && spec.frameSize >= spec.hop &&
        spec.hop > 0)) {
    error("needs capacity >= frameSize >= hop > 0 (capacity " +
          std::to_string(spec.capacity) + ", frameSize " +
          std::to_string(spec.frameSize) + ", hop " +
          std::to_string(spec.hop) + ")");
  }
  if (spec.mode == Mode::PingPong &&
      (spec.trigger != Trigger::Half || spec.frameSize * 2 != spec.capacity)) {
    error("Buffer::ModePingPong reads half the capacity on "
          "Buffer::TriggerHalf");
  }

  if (auto triggerBlock = property("trigger")) {
    if (triggerBlock->getNodeType() != AST::Entity) {
      error("'trigger' must name a switch");
    } else {
      spec.triggerBlock = ASTQuery::getNodeName(triggerBlock);
      auto blockDecl =
          ASTQuery::findDeclarationByName(spec.triggerBlock, {}, tree);
      if (!blockDecl || (blockDecl->getEntityType() != "switch" &&
                         blockDecl->getEntityType() != "trigger")) {
        error("trigger '" + spec.triggerBlock + "' must be a declared switch");
      }
    }
  }

  // Frames start at multiples of hop. They never cross the end of the ring
  // when they don't overlap and the hop divides the capacity. A partial flush
  // restarts the ring at 0, which keeps that alignment.
  spec.mirrored = spec.mode == Mode::Sliding &&
                  !(spec.frameSize == spec.hop &&
                    spec.capacity % spec.hop == 0);
  return spec;
}

bool BufferSpec::isReadableByJit(const std::string &property) const {
  if (property == "ready" || property == "count") {
    return true;
  }
  for (const auto &constant : constantProperties()) {
    if (constant.first == property) {
      return true;
    }
  }
  return false;
}

BufferSpec::PropertyAccess
BufferSpec::propertyAccess(const std::string &property, ASTNode tree) {
  PropertyAccess access;
  for (const auto &port : schemaProperties(tree)) {
    if (schemaPropertyName(port) == property) {
      access.declared = true;
      access.isPublic = schemaSwitch(port, "public");
      access.writeable = schemaSwitch(port, "writeable");
      break;
    }
  }
  return access;
}

std::vector<std::string> BufferSpec::publicProperties(ASTNode tree) {
  std::vector<std::string> names;
  for (const auto &port : schemaProperties(tree)) {
    auto propertyName = schemaPropertyName(port);
    if (!propertyName.empty() && schemaSwitch(port, "public")) {
      names.push_back(propertyName);
    }
  }
  return names;
}

} // namespace strd
