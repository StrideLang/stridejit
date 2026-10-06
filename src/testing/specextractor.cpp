#include "stride/testing/specextractor.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/streamnode.h"
#include "stride/utils/astquery.h"
#include <algorithm>
#include <optional>

namespace strd::test {

namespace {

inline bool isValueNodeType(AST::Token token) {
    return token == AST::Real || token == AST::Int || token == AST::String || token == AST::Switch;
}

inline std::shared_ptr<ValueNode> findValueNode(const ASTNode& node) {
    if (!node) return nullptr;
    if (isValueNodeType(node->getNodeType())) {
        return std::static_pointer_cast<ValueNode>(node);
    }
    for (const auto& child : node->getChildren()) {
        auto res = findValueNode(child);
        if (res) return res;
    }
    return nullptr;
}

inline std::optional<AssertionKind> parseAssertionKind(const std::string& name) {
    if (name == "ExpectEqual") return AssertionKind::Equal;
    if (name == "ExpectNear") return AssertionKind::Near;
    if (name == "ExpectTrue") return AssertionKind::True;
    if (name == "ExpectFalse") return AssertionKind::False;
    if (name == "ExpectGt") return AssertionKind::GreaterThan;
    if (name == "ExpectGe") return AssertionKind::GreaterThanOrEqual;
    if (name == "ExpectLt") return AssertionKind::LessThan;
    if (name == "ExpectLe") return AssertionKind::LessThanOrEqual;
    return std::nullopt;
}

void flattenStream(const ASTNode& node, std::vector<ASTNode>& elements) {
    if (!node) return;
    if (node->getNodeType() == AST::Stream) {
        auto streamNode = std::static_pointer_cast<StreamNode>(node);
        flattenStream(streamNode->getLeft(), elements);
        flattenStream(streamNode->getRight(), elements);
    } else {
        elements.push_back(node);
    }
}

inline void extractValuesFromNode(const ASTNode& node, std::vector<SignalScalarValue>& values) {
    if (!node) return;
    if (node->getNodeType() == AST::List) {
        for (const auto& item : node->getChildren()) {
            extractValuesFromNode(item, values);
        }
        return;
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

void extractNodeInfo(const ASTNode& elem, SignalStreamVector& streamVec) {
    if (!elem) return;

    if (elem->getNodeType() == AST::List) {
        for (const auto& item : elem->getChildren()) {
            if (!item) continue;
            if (item->getNodeType() == AST::List) {
                extractValuesFromNode(item, streamVec.values);
            } else {
                auto itemVal = findValueNode(item);
                if (itemVal && (item->getNodeType() == AST::Real || item->getNodeType() == AST::Int || item->getNodeType() == AST::Switch)) {
                    if (itemVal->getNodeType() == AST::Real) {
                        streamVec.values.push_back(itemVal->getRealValue());
                    } else if (itemVal->getNodeType() == AST::Int) {
                        streamVec.values.push_back(static_cast<int32_t>(itemVal->getIntValue()));
                    } else if (itemVal->getNodeType() == AST::Switch) {
                        streamVec.values.push_back(itemVal->getSwitchValue());
                    }
                } else if (item->getNodeType() == AST::Declaration) {
                    auto declSub = std::static_pointer_cast<DeclarationNode>(item);
                    auto kindOpt = parseAssertionKind(declSub->getObjectType());
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
    } else if (elem->getNodeType() == AST::Declaration) {
        auto declSub = std::static_pointer_cast<DeclarationNode>(elem);
        auto kindOpt = parseAssertionKind(declSub->getObjectType());
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
    } else if (isValueNodeType(elem->getNodeType())) {
        auto valNode = std::static_pointer_cast<ValueNode>(elem);
        if (elem->getNodeType() == AST::Real) {
            streamVec.values.push_back(valNode->getRealValue());
        } else if (elem->getNodeType() == AST::Int) {
            streamVec.values.push_back(static_cast<int32_t>(valNode->getIntValue()));
        } else if (elem->getNodeType() == AST::Switch) {
            streamVec.values.push_back(valNode->getSwitchValue());
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

} // namespace

std::vector<FunctionTestSpec> SpecExtractor::extractTests(const ASTNode& tree) {
    std::vector<FunctionTestSpec> specs;
    if (!tree) return specs;

    for (const auto& child : tree->getChildren()) {
        if (!child) continue;
        if (child->getNodeType() == AST::Declaration) {
            auto decl = std::static_pointer_cast<DeclarationNode>(child);
            if (decl->getObjectType() == "functionTest") {
                specs.push_back(extractSingleTest(child));
            }
        }
    }
    return specs;
}

std::vector<FunctionTestSpec> SpecExtractor::extractTestsFromFile(const std::string& filePath) {
    auto tree = AST::parseFile(filePath.c_str());
    return extractTests(tree);
}

FunctionTestSpec SpecExtractor::extractSingleTest(const ASTNode& testNode) {
    FunctionTestSpec spec;
    if (!testNode) return spec;

    if (testNode->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(testNode);
        spec.testName = decl->getName();

        // Target function / domain / module
        auto targetVal = decl->getPropertyValue("target");
        if (!targetVal) {
            targetVal = decl->getPropertyValue("function");
        }
        if (targetVal) {
            auto valNode = findValueNode(targetVal);
            if (valNode) {
                spec.functionName = valNode->getStringValue();
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

    // Calculate total ticks (max tick count across prepare streams)
    size_t maxTicks = 1;
    for (const auto& st : spec.prepareStreams) {
        maxTicks = std::max(maxTicks, st.size());
    }
    spec.tickCount = maxTicks;

    return spec;
}

void SpecExtractor::extractStreams(const ASTNode& blockNode, std::vector<SignalStreamVector>& streams) {
    if (!blockNode) return;

    for (const auto& child : blockNode->getChildren()) {
        if (!child) continue;

        SignalStreamVector streamVec;
        std::vector<ASTNode> elements;
        flattenStream(child, elements);

        for (const auto& elem : elements) {
            extractNodeInfo(elem, streamVec);
        }

        if (!streamVec.portName.empty() || !streamVec.values.empty() || streamVec.kind == AssertionKind::True || streamVec.kind == AssertionKind::False) {
            streams.push_back(streamVec);
        }
    }
}

} // namespace strd::test
