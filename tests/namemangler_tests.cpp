#include "gtest/gtest.h"

#include "stride/stridejit/namemangler.hpp"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"

using namespace strd;

namespace {

TEST(NameMangler, PlainNameIsUnchanged) {
  NameMangler mangler;
  EXPECT_EQ(mangler.mangle("Abs"), "Abs");
  ASSERT_NE(mangler.resolve("Abs"), nullptr);
  EXPECT_EQ(mangler.resolve("Abs")->name, "Abs");
}

TEST(NameMangler, TypeSignatureSelectsVariant) {
  llvm::LLVMContext ctx;
  auto *i32 = llvm::Type::getInt32Ty(ctx);
  auto *f64 = llvm::Type::getDoubleTy(ctx);

  NameMangler mangler;
  auto absInt = mangler.mangle("Abs", {}, {i32, i32});
  auto absReal = mangler.mangle("Abs", {}, {f64, f64});
  EXPECT_EQ(absInt, "_S3Abs_TiiE");
  EXPECT_EQ(absReal, "_S3Abs_TddE");

  // Mangling again returns the same name and does not add a variant.
  EXPECT_EQ(mangler.mangle("Abs", {}, {i32, i32}), absInt);
  auto variants = mangler.overloads("Abs");
  ASSERT_EQ(variants.size(), 2u);
  EXPECT_EQ(variants[0]->mangled, absInt);
  EXPECT_EQ(variants[1]->mangled, absReal);

  const auto *symbol = mangler.resolve(absReal);
  ASSERT_NE(symbol, nullptr);
  EXPECT_EQ(symbol->name, "Abs");
  EXPECT_EQ(symbol->types, (std::vector<llvm::Type *>{f64, f64}));

  // Two variants: a plain-name lookup is ambiguous.
  EXPECT_FALSE(mangler.resolveUnique("Abs").has_value());
  EXPECT_EQ(mangler.lookup("Abs", {}, {f64, f64}), absReal);
  EXPECT_FALSE(mangler.lookup("Abs", {}, {i32, f64}).has_value());
}

TEST(NameMangler, SingleVariantResolvesFromPlainName) {
  llvm::LLVMContext ctx;
  NameMangler mangler;
  auto mangled =
      mangler.mangle("Sum", {}, {llvm::Type::getInt32Ty(ctx)});
  EXPECT_EQ(mangler.resolveUnique("Sum"), mangled);
  EXPECT_FALSE(mangler.resolveUnique("Missing").has_value());
}

TEST(NameMangler, ScopeIsLengthPrefixed) {
  NameMangler mangler;
  // Without length prefixes these would both encode as "a_b".
  auto first = mangler.mangle("b", {"a_"});
  auto second = mangler.mangle("_b", {"a"});
  EXPECT_EQ(first, "_S2a_1b");
  EXPECT_EQ(second, "_S1a2_b");
  EXPECT_NE(first, second);
  EXPECT_EQ(mangler.resolve(first)->scope, std::vector<std::string>{"a_"});
}

TEST(NameMangler, TypeCodes) {
  llvm::LLVMContext ctx;
  EXPECT_EQ(NameMangler::typeCode(llvm::Type::getInt1Ty(ctx)), "b");
  EXPECT_EQ(NameMangler::typeCode(llvm::Type::getInt32Ty(ctx)), "i");
  EXPECT_EQ(NameMangler::typeCode(llvm::Type::getInt64Ty(ctx)), "l");
  EXPECT_EQ(NameMangler::typeCode(llvm::Type::getDoubleTy(ctx)), "d");
  EXPECT_EQ(NameMangler::typeCode(llvm::PointerType::get(ctx, 0)), "p");
  EXPECT_EQ(NameMangler::typeCode(
                llvm::ArrayType::get(llvm::Type::getInt1Ty(ctx), 9)),
            "A9_b");
  auto *named = llvm::StructType::create(ctx, {llvm::Type::getInt32Ty(ctx)},
                                         "String");
  EXPECT_EQ(NameMangler::typeCode(named), "S6String");
  EXPECT_EQ(NameMangler::typeCode(nullptr), "u");
}

} // namespace
