#include "helpers.hpp"
#include "numen/numen.hpp"
#include <catch2/catch_test_macros.hpp>
#include <locale>

constexpr auto GROUP = "[locale]";

namespace {

bool available(const char *name) {
  try {
    std::locale l{name};
    return true;
  } catch (...) { return false; }
}

} // namespace

TEST_CASE("Locale resolution never throws", GROUP) {
  REQUIRE_NOTHROW(numen::resolveLocale("xx_XX.bogus"));
  REQUIRE_NOTHROW(numen::resolveLocale("not a locale at all"));
  REQUIRE_NOTHROW(numen::resolveLocale(""));
  REQUIRE_NOTHROW(numen::resolveLocale());

  numen::Numen calc{};
  auto opts = numen::EvalOptions{.parseOptions = {.locale = "xx_XX.bogus"}};
  REQUIRE(calc.evaluate("1 + 1", opts) == "2");
}

TEST_CASE("Bare locale names resolve on codeset-indexed hosts", GROUP) {
  if (available("fr_FR.UTF-8")) {
    REQUIRE(numen::resolveLocale("fr_FR").name() != "C");
  } else {
    WARN("fr_FR.UTF-8 locale not available, skipping");
  }
}

TEST_CASE("Local decimal separator should be accepted", GROUP) {
  numen::Numen calc{};
  auto opts = numen::EvalOptions{.parseOptions = {.locale = "fr_FR"}};

  REQUIRE(calc.evaluate("15,23", opts) == "15,23");
  REQUIRE(calc.evaluate("15.23", opts) == "15,23");
  REQUIRE(calc.evaluate("min(1;4;8;-3)", opts) == "-3");
}

TEST_CASE("Results render with the locale's separators", GROUP) {
  numen::Numen calc{};
  auto fr = numen::EvalOptions{.parseOptions = {.locale = "fr_FR"}};
  auto us = numen::EvalOptions{.parseOptions = {.locale = "en_US"}};

  REQUIRE(calc.evaluate("1/3", fr) == "0,333333");
  REQUIRE(calc.evaluate("1234567.5", us) == "1,234,567.5");
  // en output still reads back in
  REQUIRE(calc.evaluate("1,234,567.5 * 1", us) == "1,234,567.5");

  // unicode space variants normalize to a plain space
  REQUIRE(calc.evaluate("1234567,5", fr) == "1 234 567,5");

  if (available("de_DE.UTF-8")) {
    auto de = numen::EvalOptions{.parseOptions = {.locale = "de_DE.UTF-8"}};
    test::assertExpr("1234567,5", "1.234.567,5", de);
    test::assertExpr("1/3", "0,333333", de);
  } else {
    WARN("de_DE.UTF-8 locale not available, skipping");
  }

  // grouping is not always by 3: en_IN is "12,34,567.5"
  if (available("en_IN.UTF-8")) {
    auto in = numen::EvalOptions{.parseOptions = {.locale = "en_IN.UTF-8"}};
    test::assertExpr("1234567.5", "12,34,567.5", in);
  } else {
    WARN("en_IN.UTF-8 locale not available, skipping");
  }
}

TEST_CASE("A space grouped number reads back as one number", GROUP) {
  numen::Numen calc{};
  auto fr = numen::EvalOptions{.parseOptions = {.locale = "fr_FR"}};
  auto us = numen::EvalOptions{.parseOptions = {.locale = "en_US"}};

  REQUIRE(calc.evaluate("1 234 567,891", fr) == "1 234 567,891");
  REQUIRE(calc.evaluate("1\u202f234\u202f567,8", fr) == "1 234 567,8");
  REQUIRE(calc.evaluate("1\u00a0234 + 1\u2009000", us) == "2,234");
  REQUIRE(calc.evaluate("1 234 + 1", us) == "1,235");
  REQUIRE(calc.evaluate("min(1 234; 5)", fr) == "5");
  REQUIRE(calc.evaluate("min(1 234, 5)", us) == "5");

  REQUIRE(calc.evaluate("1 2", us) == "12");
  REQUIRE(calc.evaluate("1 23", us) == "123");
  REQUIRE(calc.evaluate("1 2345", us) == "12,345");
  REQUIRE(calc.evaluate("1  234", us) == "1,234");
  REQUIRE(calc.evaluate("1 234e4", us) == "1");
  REQUIRE(calc.evaluate("1,5 234", fr) == "1,5");
  REQUIRE(calc.evaluate("1 234,5 678", fr) == "1 234,5");

  auto dt = numen::EvalOptions{.parseOptions = {.locale = "en_US"}, .timezone = test::zone("UTC")};
  REQUIRE(calc.parse<numen::DateTime>("2026-01-18 12:40", dt));
}
