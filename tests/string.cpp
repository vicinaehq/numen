#include "helpers.hpp"
#include "numen/numen.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("empty string literals evaluate to empty text") {
  test::assertExpr(R"("")", "");
  test::assertExpr("''", "");
}

TEST_CASE("string literals preserve whitespace") {
  test::assertExpr(R"("  a  b  ")", "  a  b  ");
  test::assertExpr("'\ttab'", "\ttab");
}

TEST_CASE("String literal alone should be marked as non converted") {
  numen::Numen calc{};
  auto res = calc.compute("'this is a string literal'");
  REQUIRE(res);
  REQUIRE(res->asStr());
  REQUIRE(!res->conversion);
}

TEST_CASE("Transformed string should be marked as converted") {
  numen::Numen calc{};
  auto res = calc.compute("'this is a string literal' to upper");
  REQUIRE(res);
  REQUIRE(res->asStr());
  REQUIRE(res->conversion);
}

TEST_CASE("Converter call form marks the conversion and names the converter") {
  numen::Numen calc{};
  auto res = calc.compute("upper('hello')");
  REQUIRE(res);
  REQUIRE(res->asStr());
  CHECK(*res->asStr() == "HELLO");
  REQUIRE(res->conversion);
  REQUIRE(res->conversion->as<std::string>());
  CHECK(res->conversion->as<std::string>()->to == "upper");
}

TEST_CASE("Regular functions do not mark a conversion") {
  numen::Numen calc{};
  auto res = calc.compute("sqrt(4)");
  REQUIRE(res);
  REQUIRE(!res->conversion);
}
