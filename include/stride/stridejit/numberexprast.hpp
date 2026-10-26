/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
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

#ifndef NUMBEREXPRAST_HPP
#define NUMBEREXPRAST_HPP

#include "stride/stridejit/exprast.hpp"

#include <map>
#include <memory>
#include <variant>
#include <vector>

namespace llvm {
class BasicBlock;
}

namespace strd {
class StrideCompiler;

class RealExprAST : public ExprAST {
  double Val;

public:
  RealExprAST(double Val) : Val(Val) {}
  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};

class IntExprAST : public ExprAST {
  int64_t Val;
  uint8_t NumBits{32};
  bool Signed{true};

public:
  IntExprAST(int64_t Val, int8_t numBits = 32, bool Signed = true)
      : Val(Val), NumBits(numBits), Signed(Signed) {}
  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};

class BoolExprAST : public ExprAST {
  bool Val;

public:
  BoolExprAST(int64_t Val) : Val(Val) {}
  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;
  std::string Type;
  std::vector<std::variant<size_t, std::string>> Indeces;

  enum Location {
    LOCATION_GLOBAL,
    LOCATION_STACK,
    LOCATION_HEAP,
  };

  Location varLocation = LOCATION_GLOBAL;

public:
  VariableExprAST(
      const std::string &Name,
      const std::vector<std::variant<size_t, std::string>> &Indeces =
          std::vector<std::variant<size_t, std::string>>{})
      : Name(Name), Indeces(Indeces) {}

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
  const std::string &getName() const { return Name; }
  std::vector<std::variant<size_t, std::string>> getIndices() const;
};

class MemberAccessAST : public ExprAST {
  std::string Name;
  std::string Property;

public:
  MemberAccessAST(const std::string &Name, const std::string &Property)
      : Name(Name), Property(Property) {}

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;
};

class ResetExprAST : public ExprAST {
  std::string Name;
  std::unique_ptr<ExprAST> Condition;
  std::vector<std::unique_ptr<ExprAST>> Expressions;

public:
  ResetExprAST(std::string Name, std::unique_ptr<ExprAST> Condition,
               std::vector<std::unique_ptr<ExprAST>> Expressions);

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;

  bool writesTo(const std::string &varName) const override;
};

enum class DownsampleMode {
  Decimate,       // Execute chain on phase-aligned ticks; hold previous output between ticks
  SlidingAverage, // Future: accumulate across ticks and divide
  LowPassFilter   // Future: apply anti-aliasing filter prior to decimation
};

enum class UpsampleMode {
  ZeroOrderHold,      // Execute chain M times per domain tick; upstream input stays constant
  LinearInterpolate,  // Future: linearly interpolate upstream input across M sub-ticks
  WindowedSinc        // Future: polyphase / windowed-sinc interpolation
};

struct RateScheduleSpec {
  double domainRate{0.0};
  double chainRate{0.0};

  // True when chainRate / domainRate (upsample) or domainRate / chainRate (downsample)
  // is an exact positive integer within floating-point tolerance (1e-9).
  bool isExactInteger{false};
  int64_t intFactor{1};

  // Non-integer rational / Bresenham phase parameters:
  // If both rates are exact integers (or scaled integers), we reduce by std::gcd
  // so stepD = domainRate / g, stepC = chainRate / g (exact integer arithmetic).
  // Otherwise we fall back to double phase accumulation.
  bool useInt64Phase{true};
  int64_t stepD{1};
  int64_t stepC{1};

  // Name of persistent counter / phase field in the domain's __state struct
  // (empty for exact-integer upsampling, which is stateless).
  std::string phaseStateFieldName;

  // Optional transition boundary signal name for future filtering / interpolation.
  std::string transitionSignalName;

  bool isDownsample() const {
    return domainRate > 0.0 && chainRate > 0.0 && chainRate < domainRate;
  }
  bool isUpsample() const {
    return domainRate > 0.0 && chainRate > 0.0 && chainRate > domainRate;
  }
  bool needsPersistentPhaseState() const {
    if (domainRate <= 0.0 || chainRate <= 0.0 || domainRate == chainRate)
      return false;
    if (isUpsample() && isExactInteger)
      return false; // Stateless 0..M-1 loop
    return true;
  }

  static RateScheduleSpec build(double domainRate, double chainRate,
                                const std::string &phaseFieldName = "",
                                const std::string &boundarySignal = "");
};

class DownsampleExprAST : public ExprAST {
  RateScheduleSpec Schedule;
  DownsampleMode Mode;
  std::vector<std::unique_ptr<ExprAST>> Expressions;

  void emitPreDecimateTick(StrideCompiler &state);
  void emitActiveTickBody(StrideCompiler &state);
  void emitTickAdvance(StrideCompiler &state, llvm::Value *phasePtr,
                       llvm::Value *currentPhase, llvm::BasicBlock *execEndBB,
                       llvm::BasicBlock *entryBB, llvm::BasicBlock *mergeBB);

public:
  DownsampleExprAST(RateScheduleSpec schedule,
                    std::vector<std::unique_ptr<ExprAST>> expressions,
                    DownsampleMode mode = DownsampleMode::Decimate);

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;

  bool writesTo(const std::string &varName) const override;
};

class UpsampleExprAST : public ExprAST {
  RateScheduleSpec Schedule;
  UpsampleMode Mode;
  std::vector<std::unique_ptr<ExprAST>> Expressions;

  void emitPreLoopCapture(StrideCompiler &state);
  void emitSubTickBody(StrideCompiler &state, llvm::Value *subTickIndex,
                       llvm::Value *subTickCount);
  void emitPostLoopUpdate(StrideCompiler &state);

public:
  UpsampleExprAST(RateScheduleSpec schedule,
                  std::vector<std::unique_ptr<ExprAST>> expressions,
                  UpsampleMode mode = UpsampleMode::ZeroOrderHold);

  std::pair<llvm::Value *, std::optional<llvm::Type *>>
  codegen(StrideCompiler &state) override;

  bool writesTo(const std::string &varName) const override;
};
} // namespace strd

#endif // NUMBEREXPRAST_HPP
