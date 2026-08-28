#include "numen/numen.hpp"
#include <catch2/catch_test_macros.hpp>

constexpr auto GROUP = "[locale]";

TEST_CASE("Local decimal separator should be accepted", GROUP) {
  numen::Numen calc{};
  auto opts = numen::EvalOptions{.parseOptions = {.locale = "fr_FR"}};

  REQUIRE(calc.evaluate("15,23", opts) == "15.23");
  REQUIRE(calc.evaluate("15.23", opts) == "15.23");
  REQUIRE(calc.evaluate("min(1;4;8;-3)", opts) == "-3");
}
