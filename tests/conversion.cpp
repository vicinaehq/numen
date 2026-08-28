#include "helpers.hpp"
#include "mock-currency-provider.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace numen;

namespace {

ComputedValue compute(std::string_view expr) {
  auto res = test::mockCalc().compute(expr, test::frozenConfig());
  REQUIRE(res);
  return *res;
}

} // namespace

TEST_CASE("a unit conversion names both of its sides", "[conversion]") {
  auto c = compute("100 m to in");
  REQUIRE(c.conversion);
  auto u = c.conversion->as<Number::Unit>();
  REQUIRE(u);
  REQUIRE(u->from);
  CHECK(u->from->raw == "m");
  CHECK(u->to.raw == "in");
  CHECK_FALSE(c.conversion->implicit);
  CHECK_FALSE(c.conversion->as<Timezone>());
}

TEST_CASE("a chain keeps the first question and the last answer", "[conversion]") {
  auto c = compute("100 m to ft to in");
  auto u = c.conversion->as<Number::Unit>();
  REQUIRE(u);
  CHECK(u->from->raw == "m");
  CHECK(u->to.raw == "in");
}

TEST_CASE("a timezone conversion records both zones", "[conversion]") {
  auto c = compute("now to pst");
  auto t = c.conversion->as<Timezone>();
  REQUIRE(t);
  REQUIRE(t->from);
  CHECK(t->from->tz == test::frozenConfig().timezone);
  CHECK(t->to.toString() == "America/Los_Angeles");
}

TEST_CASE("formatting passes a conversion through", "[conversion]") {
  auto c = compute("150 usd to jpy to binary");
  auto u = c.conversion->as<Number::Unit>();
  REQUIRE(u);
  CHECK(u->from->raw == "usd");
  CHECK(u->to.raw == "jpy");
}

TEST_CASE("a source with nothing of the target's kind has no question side", "[conversion]") {
  auto c = compute("now to unix");
  auto u = c.conversion->as<Number::Unit>();
  REQUIRE(u);
  CHECK_FALSE(u->from);
  CHECK(u->to.raw == "second");
}

TEST_CASE("the locale currency conversion is flagged implicit", "[conversion]") {
  auto opts = test::frozenConfig();
  opts.parseOptions.locale = "fr_FR";
  auto res = test::mockCalc().compute("100 usd", opts);
  REQUIRE(res);
  REQUIRE(res->conversion);
  CHECK(res->conversion->implicit);
  auto u = res->conversion->as<Number::Unit>();
  REQUIRE(u);
  CHECK(u->from->raw == "usd");
  CHECK(u->to.raw == "EUR");
}

TEST_CASE("plain results carry no conversion", "[conversion]") {
  CHECK_FALSE(compute("1 + 2").conversion);
  CHECK_FALSE(compute("100 m").conversion);
  CHECK_FALSE(compute("255 to hex").conversion);
  CHECK_FALSE(compute("(100 m to ft) + 1 m").conversion);
}
