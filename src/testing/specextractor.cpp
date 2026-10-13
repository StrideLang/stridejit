#include "stride/testing/specextractor.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"
#include <algorithm>
#include <optional>

namespace strd::test {

namespace {

inline bool isValueNodeType(AST::Token token) {
  return token == AST::Real || token == AST::Int || token == AST::String ||
         token == AST::Switch;
}

inline std::shared_ptr<ValueNode> findValueNode(const ASTNode &node) {
  if (!node)
    return nullptr;
  if (isValueNodeType(node->getNodeType())) {
    return std::static_pointer_cast<ValueNode>(node);
  }
  for (const auto &child : node->getChildren()) {
    auto res = findValueNode(child);
    if (res)
      return res;
  }
  return nullptr;
}

inline std::optional<AssertionKind>
parseAssertionKind(const std::string &name) {
  if (name == "ExpectEqual")
    return AssertionKind::Equal;
  if (name == "ExpectNear")
    return AssertionKind::Near;
  if (name == "ExpectTrue")
    return AssertionKind::True;
  if (name == "ExpectFalse")
    return AssertionKind::False;
  if (name == "ExpectGt")
    return AssertionKind::GreaterThan;
  if (name == "ExpectGe")
    return AssertionKind::GreaterThanOrEqual;
  if (name == "ExpectLt")
    return AssertionKind::LessThan;
  if (name == "ExpectLe")
    return AssertionKind::LessThanOrEqual;
  return std::nullopt;
}

void flattenStream(const ASTNode &node, std::vector<ASTNode> &elements) {
  if (!node)
    return;
  if (node->getNodeType() == AST::Stream) {
    auto streamNode = std::static_pointer_cast<StreamNode>(node);
    flattenStream(streamNode->getLeft(), elements);
    flattenStream(streamNode->getRight(), elements);
  } else {
    elements.push_back(node);
  }
}

inline void extractValuesFromNode(const ASTNode &node,
                                  std::vector<SignalScalarValue> &values) {
  if (!node)
    return;
  if (node->getNodeType() == AST::List) {
    for (const auto &item : node->getChildren()) {
      extractValuesFromNode(item, values);
    }
    return;
  }
  if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->getExpressionType() == ExpressionNode::UnaryMinus) {
      std::vector<SignalScalarValue> innerVals;
      extractValuesFromNode(expr->getValue(), innerVals);
      for (auto &v : innerVals) {
        std::visit(
            [](auto &&arg) {
              using T = std::decay_t<decltype(arg)>;
              if constexpr (std::is_same_v<T, double> ||
                            std::is_same_v<T, int32_t> ||
                            std::is_same_v<T, int64_t>) {
                arg = -arg;
              }
            },
            v);
        values.push_back(v);
      }
      return;
    } else if (expr->getExpressionType() == ExpressionNode::LogicalNot) {
      std::vector<SignalScalarValue> innerVals;
      extractValuesFromNode(expr->getValue(), innerVals);
      for (auto &v : innerVals) {
        if (std::holds_alternative<bool>(v)) {
          values.push_back(!std::get<bool>(v));
        } else {
          values.push_back(v);
        }
      }
      return;
    }
  }
  auto itemVal = findValueNode(node);
  if (itemVal) {
    if (itemVal->getNodeType() == AST::Real) {
      values.push_back(itemVal->getRealValue());
    } else if (itemVal->getNodeType() == AST::Int) {
      values.push_back(static_cast<int32_t>(itemVal->getIntValue()));
    } else if (itemVal->getNodeType() == AST::Switch) {
      values.push_back(itemVal->getSwitchValue());
    }
  }
}

void extractNodeInfo(const ASTNode &elem, SignalStreamVector &streamVec) {
  if (!elem)
    return;

  if (elem->getNodeType() == AST::List) {
    bool hasNonValue = false;
    for (const auto &item : elem->getChildren()) {
      if (!item)
        continue;
      if (item->getNodeType() == AST::Declaration ||
          item->getNodeType() == AST::Function) {
        hasNonValue = true;
        break;
      }
      auto itemVal = findValueNode(item);
      if (!itemVal && item->getNodeType() != AST::List) {
        hasNonValue = true;
        break;
      }
    }

    if (!hasNonValue) {
      // Pure values list (e.g. prepare streams like [ [ 1.0, 2.0 ] ] or [ 1.0, 2.0 ])
      bool anyChildIsList = false;
      for (const auto &item : elem->getChildren()) {
        if (item && item->getNodeType() == AST::List) {
          anyChildIsList = true;
          break;
        }
      }

      if (anyChildIsList) {
        streamVec.isArrayPort = true;
        for (const auto &item : elem->getChildren()) {
          std::vector<SignalScalarValue> tickVals;
          extractValuesFromNode(item, tickVals);
          streamVec.tickValues.push_back(tickVals);
          if (streamVec.values.empty()) {
            streamVec.values = tickVals;
          }
        }
      } else {
        streamVec.isArrayPort = false;
        for (const auto &item : elem->getChildren()) {
          std::vector<SignalScalarValue> vals;
          extractValuesFromNode(item, vals);
          if (!vals.empty()) {
            streamVec.values.push_back(vals[0]);
            streamVec.tickValues.push_back({vals[0]});
          }
        }
      }
    } else {
      // Mixed list (e.g. [ Output, [ 10.0 ] ] in validate)
      for (const auto &item : elem->getChildren()) {
        if (!item)
          continue;
        if (item->getNodeType() == AST::List) {
          bool innerHasList = false;
          for (const auto &c : item->getChildren()) {
            if (c && c->getNodeType() == AST::List) {
              innerHasList = true;
              break;
            }
          }

          if (innerHasList) {
            streamVec.isArrayPort = true;
            for (const auto &c : item->getChildren()) {
              std::vector<SignalScalarValue> tickVals;
              extractValuesFromNode(c, tickVals);
              streamVec.tickValues.push_back(tickVals);
              if (streamVec.values.empty()) {
                streamVec.values = tickVals;
              }
            }
          } else {
            streamVec.isArrayPort = false;
            for (const auto &c : item->getChildren()) {
              std::vector<SignalScalarValue> vals;
              extractValuesFromNode(c, vals);
              if (!vals.empty()) {
                streamVec.values.push_back(vals[0]);
                streamVec.tickValues.push_back({vals[0]});
              }
            }
          }
        } else {
          std::vector<SignalScalarValue> itemVals;
          extractValuesFromNode(item, itemVals);
          if (!itemVals.empty()) {
            for (const auto &v : itemVals) {
              streamVec.values.push_back(v);
              streamVec.tickValues.push_back({v});
            }
          } else if (item->getNodeType() == AST::Declaration) {
            auto declSub = std::static_pointer_cast<DeclarationNode>(item);
            auto kindOpt = parseAssertionKind(declSub->getEntityType());
            if (!kindOpt.has_value()) {
              kindOpt = parseAssertionKind(declSub->getName());
            }
            if (kindOpt.has_value()) {
              streamVec.kind = kindOpt.value();
              auto epsProp = declSub->getPropertyValue("epsilon");
              auto epsVal = findValueNode(epsProp);
              if (epsVal) {
                streamVec.epsilon = epsVal->getRealValue();
              }
            } else {
              streamVec.portName = declSub->getName();
            }
          } else if (item->getNodeType() == AST::Function) {
            auto funcNode = std::static_pointer_cast<FunctionNode>(item);
            auto kindOpt = parseAssertionKind(funcNode->getName());
            if (kindOpt.has_value()) {
              streamVec.kind = kindOpt.value();
              auto epsProp = funcNode->getPropertyValue("epsilon");
              auto epsVal = findValueNode(epsProp);
              if (epsVal) {
                streamVec.epsilon = epsVal->getRealValue();
              }
            } else {
              streamVec.portName = funcNode->getName();
            }
          } else {
            std::string nodeName = ASTQuery::getNodeName(item);
            auto kindOpt = parseAssertionKind(nodeName);
            if (kindOpt.has_value()) {
              streamVec.kind = kindOpt.value();
            } else if (!nodeName.empty()) {
              streamVec.portName = nodeName;
            }
          }
        }
      }
    }
  } else if (elem->getNodeType() == AST::Declaration) {
    auto declSub = std::static_pointer_cast<DeclarationNode>(elem);
    auto kindOpt = parseAssertionKind(declSub->getEntityType());
    if (!kindOpt.has_value()) {
      kindOpt = parseAssertionKind(declSub->getName());
    }

    if (kindOpt.has_value()) {
      streamVec.kind = kindOpt.value();
      auto epsProp = declSub->getPropertyValue("epsilon");
      auto epsVal = findValueNode(epsProp);
      if (epsVal) {
        streamVec.epsilon = epsVal->getRealValue();
      }
    } else {
      streamVec.portName = declSub->getName();
    }
  } else if (elem->getNodeType() == AST::Function) {
    auto funcNode = std::static_pointer_cast<FunctionNode>(elem);
    auto kindOpt = parseAssertionKind(funcNode->getName());
    if (kindOpt.has_value()) {
      streamVec.kind = kindOpt.value();
      auto epsProp = funcNode->getPropertyValue("epsilon");
      auto epsVal = findValueNode(epsProp);
      if (epsVal) {
        streamVec.epsilon = epsVal->getRealValue();
      }
    } else {
      streamVec.portName = funcNode->getName();
    }
  } else {
    std::vector<SignalScalarValue> itemVals;
    extractValuesFromNode(elem, itemVals);
    if (!itemVals.empty()) {
      for (const auto &v : itemVals) {
        streamVec.values.push_back(v);
        streamVec.tickValues.push_back({v});
      }
    } else {
      std::string nodeName = ASTQuery::getNodeName(elem);
      auto kindOpt = parseAssertionKind(nodeName);
      if (kindOpt.has_value()) {
        streamVec.kind = kindOpt.value();
      } else if (!nodeName.empty()) {
        streamVec.portName = nodeName;
      }
    }
  }
}

} // namespace

std::vector<FunctionTestSpec> SpecExtractor::extractTests(const ASTNode &tree) {
  std::vector<FunctionTestSpec> specs;
  if (!tree)
    return specs;

  for (const auto &child : tree->getChildren()) {
    if (!child)
      continue;
    if (child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getEntityType() == "functionTest") {
        specs.push_back(extractSingleTest(child));
      }
    }
  }
  return specs;
}

std::vector<FunctionTestSpec>
SpecExtractor::extractTestsFromFile(const std::string &filePath) {
  auto tree = AST::parseFile(filePath.c_str());
  return extractTests(tree);
}

FunctionTestSpec SpecExtractor::extractSingleTest(const ASTNode &testNode) {
  FunctionTestSpec spec;
  if (!testNode)
    return spec;

  if (testNode->getNodeType() == AST::Declaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(testNode);
    spec.testName = decl->getName();

    // Target function / domain / module
    auto targetVal = decl->getPropertyValue("target");
    if (!targetVal) {
      targetVal = decl->getPropertyValue("function");
    }
    if (!targetVal) {
      targetVal = decl->getPropertyValue("domain");
    }
    if (targetVal) {
      auto valNode = findValueNode(targetVal);
      if (valNode) {
        spec.functionName = valNode->getStringValue();
        if (spec.functionName.empty()) {
          spec.functionName = ASTQuery::getNodeName(valNode);
        }
      } else {
        spec.functionName = ASTQuery::getNodeName(targetVal);
      }
    }

    // Timeout
    auto timeoutVal = decl->getPropertyValue("timeout");
    if (timeoutVal) {
      auto valNode = findValueNode(timeoutVal);
      if (valNode) {
        spec.timeoutSeconds = valNode->getRealValue();
      }
    }

    // Prepare streams
    auto prepNode = decl->getPropertyValue("prepare");
    if (prepNode) {
      extractStreams(prepNode, spec.prepareStreams);
    }

    // Validate streams
    auto valNode = decl->getPropertyValue("validate");
    if (valNode) {
      extractStreams(valNode, spec.validateStreams);
    }
  } else {
    spec.testName = ASTQuery::getNodeName(testNode);
  }

  // Calculate total ticks (max tick count across prepare streams and validate streams)
  size_t maxTicks = 1;
  for (const auto &st : spec.prepareStreams) {
    maxTicks = std::max(maxTicks, st.size());
  }
  for (const auto &st : spec.validateStreams) {
    maxTicks = std::max(maxTicks, st.size());
  }
  spec.tickCount = maxTicks;

  return spec;
}

void SpecExtractor::extractStreams(const ASTNode &streamListNode,
                                   std::vector<SignalStreamVector> &streams) {
  if (!streamListNode)
    return;

  for (const auto &child : streamListNode->getChildren()) {
    if (!child)
      continue;

    SignalStreamVector streamVec;
    std::vector<ASTNode> elements;
    flattenStream(child, elements);

    for (const auto &elem : elements) {
      extractNodeInfo(elem, streamVec);
    }

    if (!streamVec.portName.empty() || !streamVec.values.empty() ||
        streamVec.kind == AssertionKind::True ||
        streamVec.kind == AssertionKind::False) {
      streams.push_back(streamVec);
    }
  }
}

} // namespace strd::test
