#include "stride/stridejit/pioencoder.hpp"
#include "stride/stridejit/stridecompiler.hpp"

#include "stride/parser/expressionnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astquery.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace strd {

uint16_t PioEncoder::encodeJmp(uint8_t condition, uint8_t targetAddr,
                               uint8_t delaySideSet) {
  return static_cast<uint16_t>((0u << 13) | ((delaySideSet & 0x1Fu) << 8) |
                               ((condition & 0x7u) << 5) |
                               (targetAddr & 0x1Fu));
}

uint16_t PioEncoder::encodeWait(bool polarity, uint8_t source, uint8_t index,
                                uint8_t delaySideSet) {
  return static_cast<uint16_t>((1u << 13) | ((delaySideSet & 0x1Fu) << 8) |
                               ((polarity ? 1u : 0u) << 7) |
                               ((source & 0x3u) << 5) | (index & 0x1Fu));
}

uint16_t PioEncoder::encodeIn(uint8_t source, uint8_t bitCount,
                              uint8_t delaySideSet) {
  uint8_t bc = (bitCount >= 32 || bitCount == 0) ? 0u : (bitCount & 0x1Fu);
  return static_cast<uint16_t>((2u << 13) | ((delaySideSet & 0x1Fu) << 8) |
                               ((source & 0x7u) << 5) | bc);
}

uint16_t PioEncoder::encodeOut(uint8_t dest, uint8_t bitCount,
                               uint8_t delaySideSet) {
  uint8_t bc = (bitCount >= 32 || bitCount == 0) ? 0u : (bitCount & 0x1Fu);
  return static_cast<uint16_t>((3u << 13) | ((delaySideSet & 0x1Fu) << 8) |
                               ((dest & 0x7u) << 5) | bc);
}

uint16_t PioEncoder::encodePush(bool ifFull, bool block,
                                uint8_t delaySideSet) {
  return static_cast<uint16_t>((4u << 13) | ((delaySideSet & 0x1Fu) << 8) |
                               (0u << 7) | ((ifFull ? 1u : 0u) << 6) |
                               ((block ? 1u : 0u) << 5));
}

uint16_t PioEncoder::encodePull(bool ifEmpty, bool block,
                                uint8_t delaySideSet) {
  return static_cast<uint16_t>((4u << 13) | ((delaySideSet & 0x1Fu) << 8) |
                               (1u << 7) | ((ifEmpty ? 1u : 0u) << 6) |
                               ((block ? 1u : 0u) << 5));
}

uint16_t PioEncoder::encodeMov(uint8_t dest, uint8_t op, uint8_t source,
                               uint8_t delaySideSet) {
  return static_cast<uint16_t>((5u << 13) | ((delaySideSet & 0x1Fu) << 8) |
                               ((dest & 0x7u) << 5) | ((op & 0x3u) << 3) |
                               (source & 0x7u));
}

uint16_t PioEncoder::encodeNop(uint8_t delaySideSet) {
  return encodeMov(/*Y=*/2, /*None=*/0, /*Y=*/2, delaySideSet);
}

uint16_t PioEncoder::encodeIrq(bool clear, bool wait, uint8_t index,
                               uint8_t delaySideSet) {
  return static_cast<uint16_t>((6u << 13) | ((delaySideSet & 0x1Fu) << 8) |
                               ((clear ? 1u : 0u) << 6) |
                               ((wait ? 1u : 0u) << 5) | (index & 0x1Fu));
}

uint16_t PioEncoder::encodeSet(uint8_t dest, uint8_t data,
                               uint8_t delaySideSet) {
  return static_cast<uint16_t>((7u << 13) | ((delaySideSet & 0x1Fu) << 8) |
                               ((dest & 0x7u) << 5) | (data & 0x1Fu));
}

namespace {

std::string stripQualifier(std::string name) {
  size_t pos = name.rfind("::");
  return pos != std::string::npos ? name.substr(pos + 2) : name;
}

int64_t readIntProp(const std::shared_ptr<FunctionNode> &fn,
                    const std::string &propName, int64_t defVal) {
  if (!fn) {
    return defVal;
  }
  auto p = fn->getPropertyValue(propName);
  if (p && p->getNodeType() == AST::Int) {
    return std::static_pointer_cast<ValueNode>(p)->getIntValue();
  }
  return defVal;
}

bool readBoolProp(const std::shared_ptr<FunctionNode> &fn,
                  const std::string &propName, bool defVal) {
  if (!fn) {
    return defVal;
  }
  auto p = fn->getPropertyValue(propName);
  if (p && p->getNodeType() == AST::Switch) {
    return std::static_pointer_cast<ValueNode>(p)->getSwitchValue();
  }
  if (p && p->getNodeType() == AST::Int) {
    return std::static_pointer_cast<ValueNode>(p)->getIntValue() != 0;
  }
  return defVal;
}

uint8_t computeDelaySideSet(const std::shared_ptr<FunctionNode> &fn) {
  int64_t delay = std::clamp<int64_t>(readIntProp(fn, "delay", 0), 0, 31);
  auto ss = fn ? fn->getPropertyValue("sideSet") : nullptr;
  if (ss && ss->getNodeType() == AST::Int) {
    int64_t sideVal = std::static_pointer_cast<ValueNode>(ss)->getIntValue();
    return static_cast<uint8_t>(((sideVal & 0x1) << 4) | (delay & 0xF));
  }
  if (ss && ss->getNodeType() == AST::Switch) {
    bool sideVal = std::static_pointer_cast<ValueNode>(ss)->getSwitchValue();
    return static_cast<uint8_t>(((sideVal ? 1u : 0u) << 4) | (delay & 0xF));
  }
  return static_cast<uint8_t>(delay & 0x1F);
}

} // namespace

bool PioEncoder::isPioDomain(
    const std::shared_ptr<DeclarationNode> &domainDecl) {
  if (!domainDecl) {
    return false;
  }
  auto execProp = domainDecl->getPropertyValue("execution");
  if (!execProp || execProp->getNodeType() == AST::None) {
    return false;
  }
  bool isHw = false;
  if (auto origin = execProp->getCompilerProperty("resolvedFrom")) {
    if (origin->getNodeType() == AST::Declaration &&
        std::static_pointer_cast<DeclarationNode>(origin)->getName() ==
            "ExecutionHardware") {
      isHw = true;
    }
  } else if (execProp->getNodeType() == AST::Int) {
    isHw = std::static_pointer_cast<ValueNode>(execProp)->getIntValue() == 4;
  } else {
    isHw = stripQualifier(ASTQuery::getNodeName(execProp)) == "ExecutionHardware";
  }
  if (!isHw) {
    return false;
  }
  auto reqProp = domainDecl->getPropertyValue("requiresResources");
  if (!reqProp || reqProp->getNodeType() != AST::List) {
    return false;
  }
  for (const auto &item : reqProp->getChildren()) {
    std::string rName = stripQualifier(ASTQuery::getNodeName(item));
    if (rName.rfind("_Pio", 0) == 0) {
      return true;
    }
  }
  return false;
}

PioProgram
PioEncoder::compileDomain(const std::shared_ptr<DeclarationNode> &domainDecl,
                          ASTNode tree, std::vector<LangError> *errors) {
  PioProgram prog;
  if (!domainDecl) {
    return prog;
  }
  prog.domainName = domainDecl->getName();

  if (auto reqProp = domainDecl->getPropertyValue("requiresResources")) {
    if (reqProp->getNodeType() == AST::List) {
      for (const auto &item : reqProp->getChildren()) {
        std::string rName = stripQualifier(ASTQuery::getNodeName(item));
        if (rName == "_Pio0_Sm1") {
          prog.stateMachine = 1;
        } else if (rName == "_Pio0_Sm2") {
          prog.stateMachine = 2;
        } else if (rName == "_Pio0_Sm3") {
          prog.stateMachine = 3;
        }
      }
    }
  }

  // Map signals in this domain to PIO scratch registers X (1) and Y (2)
  std::map<std::string, uint8_t> scratchRegMap;
  if (tree) {
    for (const auto &node : tree->getChildren()) {
      if (node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() != "signal") {
        continue;
      }
      std::string dom =
          stripQualifier(CodeAnalysis::getNodeDomainName(decl, {}, tree));
      if (dom != prog.domainName) {
        continue;
      }
      bool mapped = false;
      if (auto resProp = decl->getPropertyValue("resources")) {
        auto checkR = [&](const ASTNode &r) {
          std::string rn = stripQualifier(ASTQuery::getNodeName(r));
          if (rn == "_PioRegX") {
            scratchRegMap[decl->getName()] = 1;
            mapped = true;
          } else if (rn == "_PioRegY") {
            scratchRegMap[decl->getName()] = 2;
            mapped = true;
          }
        };
        if (resProp->getNodeType() == AST::List) {
          for (const auto &c : resProp->getChildren()) {
            checkR(c);
          }
        } else {
          checkR(resProp);
        }
      }
      if (!mapped) {
        if (decl->getName() == "X" || decl->getName() == "RegX") {
          scratchRegMap[decl->getName()] = 1;
        } else if (decl->getName() == "Y" || decl->getName() == "RegY") {
          scratchRegMap[decl->getName()] = 2;
        }
      }
    }
  }

  struct PendingJmp {
    size_t instrIndex;
    uint8_t condition;
    std::string targetState;
    uint8_t delaySideSet;
  };
  std::vector<PendingJmp> pendingJmps;
  std::map<std::string, uint8_t> stateAddresses;
  std::set<std::string> stateNames;

  auto getStateStreams = [&](const std::shared_ptr<DeclarationNode> &sDecl) {
    std::vector<std::shared_ptr<StreamNode>> out;
    if (!sDecl) {
      return out;
    }
    if (auto sp = sDecl->getPropertyValue("streams")) {
      if (sp->getNodeType() == AST::List) {
        for (const auto &c : sp->getChildren()) {
          if (c && c->getNodeType() == AST::Stream) {
            out.push_back(std::static_pointer_cast<StreamNode>(c));
          }
        }
      } else if (sp->getNodeType() == AST::Stream) {
        out.push_back(std::static_pointer_cast<StreamNode>(sp));
      }
    }
    if (out.empty()) {
      for (const auto &desc :
           ASTQuery::getContainedStreamEntities(sDecl, {}, tree)) {
        for (const auto &st : desc.streams) {
          out.push_back(st);
        }
      }
    }
    return out;
  };

  // Collect state machine declarations if any
  std::vector<std::shared_ptr<DeclarationNode>> stateDecls;
  if (tree) {
    if (auto smProp = domainDecl->getPropertyValue("stateMachines")) {
      if (smProp->getNodeType() == AST::List) {
        std::set<std::string> visitedStates;
        std::function<void(const std::string &)> collectStateChain =
            [&](const std::string &sName) {
              if (sName.empty() || !visitedStates.insert(sName).second) {
                return;
              }
              auto sDecl = ASTQuery::findDeclarationByName(sName, {}, tree);
              if (!sDecl || sDecl->getEntityType() != "state") {
                return;
              }
              stateNames.insert(sName);
              stateDecls.push_back(sDecl);
              // Find transition targets in sDecl's streams
              for (const auto &st : getStateStreams(sDecl)) {
                std::function<void(const ASTNode &)> findTargets =
                    [&](const ASTNode &n) {
                      if (!n) {
                        return;
                      }
                      if (n->getNodeType() == AST::Stream) {
                        auto sn = std::static_pointer_cast<StreamNode>(n);
                        findTargets(sn->getLeft());
                        findTargets(sn->getRight());
                        return;
                      }
                      if (n->getNodeType() == AST::Entity) {
                        std::string cand =
                            stripQualifier(ASTQuery::getNodeName(n));
                        auto tDecl =
                            ASTQuery::findDeclarationByName(cand, {}, tree);
                        if (tDecl && tDecl->getEntityType() == "state") {
                          collectStateChain(cand);
                        }
                      }
                    };
                findTargets(st);
              }
            };
        for (const auto &smNode : smProp->getChildren()) {
          collectStateChain(stripQualifier(ASTQuery::getNodeName(smNode)));
        }
      }
    }
  }

  auto encodeStream = [&](const std::shared_ptr<StreamNode> &stream,
                          bool &hasDecrementX, bool &hasDecrementY) {
    if (!stream) {
      return;
    }
    // Flatten stream chain: A >> B >> C
    std::vector<ASTNode> chain;
    ASTNode curr = stream;
    while (curr && curr->getNodeType() == AST::Stream) {
      auto sn = std::static_pointer_cast<StreamNode>(curr);
      chain.push_back(sn->getLeft());
      curr = sn->getRight();
    }
    if (curr) {
      chain.push_back(curr);
    }

    // Check for scratch register assignment: K >> RegX; or RegX - 1 >> RegX;
    if (chain.size() == 2 && chain[1] &&
        chain[1]->getNodeType() == AST::Entity) {
      std::string dstName = stripQualifier(ASTQuery::getNodeName(chain[1]));
      if (scratchRegMap.count(dstName)) {
        uint8_t regDest = scratchRegMap[dstName];
        if (chain[0] && chain[0]->getNodeType() == AST::Int) {
          int64_t val =
              std::static_pointer_cast<ValueNode>(chain[0])->getIntValue();
          prog.instructions.push_back(
              encodeSet(regDest, static_cast<uint8_t>(val & 0x1F), 0));
          return;
        }
        if (chain[0] && chain[0]->getNodeType() == AST::Expression) {
          auto expr = std::static_pointer_cast<ExpressionNode>(chain[0]);
          if (expr->getExpressionTypeString() == "Subtract") {
            if (regDest == 1) {
              hasDecrementX = true;
            } else if (regDest == 2) {
              hasDecrementY = true;
            }
            return;
          }
        }
      }
    }
    // Check for state transition: Cond >> TargetState; or [A, B] >> Cond() >> TargetState;
    if (chain.size() >= 2 && chain.back() &&
        chain.back()->getNodeType() == AST::Entity) {
      std::string dstName = stripQualifier(ASTQuery::getNodeName(chain.back()));
      if (stateNames.count(dstName)) {
        ASTNode condNode = chain[chain.size() - 2];
        uint8_t jmpCond = 0; // unconditional
        if (condNode && condNode->getNodeType() == AST::Function) {
          std::string fn = stripQualifier(
              std::static_pointer_cast<FunctionNode>(condNode)->getName());
          if (fn == "Equal") {
            jmpCond = hasDecrementY ? 3 : 1; // !Y or !X
          } else if (fn == "NotEqual") {
            jmpCond = 5; // X != Y
          } else if (fn == "Greater") {
            jmpCond = hasDecrementY ? 4 : 2; // Y-- or X--
          }
        } else if (condNode && condNode->getNodeType() == AST::Expression) {
          auto expr = std::static_pointer_cast<ExpressionNode>(condNode);
          std::string et = expr->getExpressionTypeString();
          if (et == "LogicalAnd" || et == "Greater") {
            jmpCond = hasDecrementY ? 4 : 2; // X-- or Y--
          } else if (et == "Equal" || et == "LogicalNot") {
            jmpCond = 1; // !X
          } else if (et == "NotEqual") {
            jmpCond = 5; // X != Y
          }
        } else if (condNode && condNode->getNodeType() == AST::Entity) {
          std::string srcName = stripQualifier(ASTQuery::getNodeName(condNode));
          if (srcName != "on" && !stateNames.count(srcName)) {
            jmpCond = 6; //JMP PIN
          }
        }
        pendingJmps.push_back(
            {prog.instructions.size(), jmpCond, dstName, 0});
        prog.instructions.push_back(encodeJmp(jmpCond, 0, 0));
        return;
      }
    }

    for (size_t i = 0; i < chain.size(); ++i) {
      const auto &member = chain[i];
      if (!member || member->getNodeType() != AST::Function) {
        continue;
      }
      auto fn = std::static_pointer_cast<FunctionNode>(member);
      std::string name = stripQualifier(fn->getName());
      uint8_t ds = computeDelaySideSet(fn);
      ASTNode prevNode = (i > 0) ? chain[i - 1] : nullptr;

      if (name == "PioPull") {
        bool block = readBoolProp(fn, "block", true);
        bool ifEmpty = readBoolProp(fn, "ifEmpty", false);
        prog.instructions.push_back(encodePull(ifEmpty, block, ds));
      } else if (name == "PioPush") {
        bool block = readBoolProp(fn, "block", true);
        bool ifFull = readBoolProp(fn, "ifFull", false);
        prog.instructions.push_back(encodePush(ifFull, block, ds));
      } else if (name == "PioShiftOut") {
        uint8_t bits =
            static_cast<uint8_t>(std::clamp<int64_t>(readIntProp(fn, "bits", 1), 1, 32));
        prog.instructions.push_back(encodeOut(/*PINS=*/0, bits, ds));
      } else if (name == "PioShiftIn") {
        uint8_t bits =
            static_cast<uint8_t>(std::clamp<int64_t>(readIntProp(fn, "bits", 1), 1, 32));
        prog.instructions.push_back(encodeIn(/*PINS=*/0, bits, ds));
      } else if (name == "PioSetPins" || name == "DigitalOut" ||
                 name == "Led" || name == "_GpioWrite") {
        int64_t val = 0;
        if (prevNode && prevNode->getNodeType() == AST::Int) {
          val = std::static_pointer_cast<ValueNode>(prevNode)->getIntValue();
        } else if (prevNode && prevNode->getNodeType() == AST::Switch) {
          val = std::static_pointer_cast<ValueNode>(prevNode)->getSwitchValue()
                    ? 1
                    : 0;
        }
        prog.instructions.push_back(
            encodeSet(/*PINS=*/0, static_cast<uint8_t>(val & 0x1F), ds));
      } else if (name == "PioDelay") {
        int64_t cycles = readIntProp(fn, "cycles", -1);
        if (cycles < 0 && prevNode && prevNode->getNodeType() == AST::Int) {
          cycles = std::static_pointer_cast<ValueNode>(prevNode)->getIntValue();
        }
        if (cycles <= 0) {
          cycles = 1;
        }
        uint8_t delayBits =
            static_cast<uint8_t>(std::clamp<int64_t>(cycles - 1, 0, 31));
        if (ds != 0) {
          delayBits = ds;
        }
        prog.instructions.push_back(encodeNop(delayBits));
      } else if (name == "PioWaitPin") {
        bool polarity = true;
        if (prevNode && prevNode->getNodeType() == AST::Switch) {
          polarity =
              std::static_pointer_cast<ValueNode>(prevNode)->getSwitchValue();
        } else if (prevNode && prevNode->getNodeType() == AST::Int) {
          polarity =
              std::static_pointer_cast<ValueNode>(prevNode)->getIntValue() != 0;
        }
        uint8_t pinIdx =
            static_cast<uint8_t>(readIntProp(fn, "pin", 0) & 0x1F);
        prog.instructions.push_back(
            encodeWait(polarity, /*PIN=*/1, pinIdx, ds));
      } else if (name == "PioIrq") {
        int64_t idx = 0;
        if (prevNode && prevNode->getNodeType() == AST::Int) {
          idx = std::static_pointer_cast<ValueNode>(prevNode)->getIntValue();
        }
        bool wait = readBoolProp(fn, "wait", false);
        bool clear = readBoolProp(fn, "clear", false);
        prog.instructions.push_back(
            encodeIrq(clear, wait, static_cast<uint8_t>(idx & 0x1F), ds));
      }
    }
  };

  // 1. Encode top-level streams belonging to domainDecl
  if (tree) {
    bool decX = false;
    bool decY = false;
    for (const auto &node : tree->getChildren()) {
      if (node->getNodeType() != AST::Stream) {
        continue;
      }
      auto stream = std::static_pointer_cast<StreamNode>(node);
      std::string streamDom;
      std::function<void(const ASTNode &)> findDom = [&](const ASTNode &n) {
        if (!n || !streamDom.empty()) {
          return;
        }
        if (n->getNodeType() == AST::Stream) {
          auto sn = std::static_pointer_cast<StreamNode>(n);
          findDom(sn->getRight());
          findDom(sn->getLeft());
          return;
        }
        std::string d =
            stripQualifier(CodeAnalysis::getNodeDomainName(n, {}, tree));
        if (!d.empty()) {
          streamDom = d;
        }
      };
      findDom(stream);
      if (streamDom == prog.domainName) {
        encodeStream(stream, decX, decY);
      }
    }
  }

  // 2. Encode state machine states in order
  for (const auto &sDecl : stateDecls) {
    stateAddresses[sDecl->getName()] =
        static_cast<uint8_t>(prog.instructions.size() & 0x1F);
    bool decX = false;
    bool decY = false;
    for (const auto &st : getStateStreams(sDecl)) {
      encodeStream(st, decX, decY);
    }
  }

  // Patch pending JMP target addresses
  for (const auto &pj : pendingJmps) {
    auto it = stateAddresses.find(pj.targetState);
    uint8_t targetAddr = (it != stateAddresses.end()) ? it->second : 0;
    if (pj.instrIndex < prog.instructions.size()) {
      prog.instructions[pj.instrIndex] =
          encodeJmp(pj.condition, targetAddr, pj.delaySideSet);
    }
  }

  prog.wrapBottom = 0;
  prog.wrapTop = prog.instructions.empty()
                     ? 0
                     : static_cast<uint8_t>(
                           std::min<size_t>(prog.instructions.size() - 1, 31));

  if (prog.instructions.size() > 32 && errors) {
    LangError err;
    err.type = LangError::PioInstructionOverflow;
    domainDecl->copyLocationTo(err);
    err.errorTokens = {prog.domainName,
                       std::to_string(prog.instructions.size()), "_Pio0"};
    errors->push_back(err);
  }

  return prog;
}

void PioEncoder::emitGlobals(const PioProgram &program,
                             StrideCompiler &state) {
  if (!state.TheModule || !state.TheContext || program.domainName.empty()) {
    return;
  }
  llvm::Type *i16Ty = llvm::Type::getInt16Ty(*state.TheContext);
  llvm::Type *i32Ty = llvm::Type::getInt32Ty(*state.TheContext);

  std::vector<llvm::Constant *> words;
  words.reserve(program.instructions.size());
  for (uint16_t inst : program.instructions) {
    words.push_back(llvm::ConstantInt::get(i16Ty, inst, false));
  }

  llvm::ArrayType *arrTy =
      llvm::ArrayType::get(i16Ty, program.instructions.size());
  llvm::Constant *initArr = llvm::ConstantArray::get(arrTy, words);

  std::string instrSym = "__stride_pio_" + program.domainName + "_instrs";
  if (auto *existing = state.TheModule->getNamedGlobal(instrSym)) {
    existing->eraseFromParent();
  }
  new llvm::GlobalVariable(*state.TheModule, arrTy, /*isConstant=*/true,
                           llvm::GlobalValue::ExternalLinkage, initArr,
                           instrSym);

  auto emitI32Const = [&](const std::string &suffix, uint32_t val) {
    std::string sym = "__stride_pio_" + program.domainName + "_" + suffix;
    if (auto *existing = state.TheModule->getNamedGlobal(sym)) {
      existing->eraseFromParent();
    }
    new llvm::GlobalVariable(*state.TheModule, i32Ty, /*isConstant=*/true,
                             llvm::GlobalValue::ExternalLinkage,
                             llvm::ConstantInt::get(i32Ty, val, false), sym);
  };

  emitI32Const("instr_count",
               static_cast<uint32_t>(program.instructions.size()));
  emitI32Const("wrap_bottom", program.wrapBottom);
  emitI32Const("wrap_top", program.wrapTop);
  emitI32Const("sm", static_cast<uint32_t>(program.stateMachine));
}

} // namespace strd
