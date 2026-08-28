#include "numen/numen.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Should reject ill-formed in strict mode") {
  numen::Numen calc{};
  auto opts = numen::EvalOptions{.parseOptions = {.strict = true}};
  auto eval = [&](auto &&str) { return calc.evaluate(str, opts); };

  numen::EvalOptions us{.parseOptions = {.strict = true, .locale = "en_US"}};
  REQUIRE(calc.evaluate("5 323", us) == "5,323");
  REQUIRE(calc.evaluate("5 32", us) == "532");
  REQUIRE(calc.evaluate("5  32", us) == "532");
  REQUIRE_FALSE(calc.evaluate("what time is it currently", opts));
  REQUIRE_FALSE(eval("time in ewoeoweiwewo"));
  REQUIRE_FALSE(eval("3 in jan"));
  REQUIRE_FALSE(eval("5 bears plus 2 rabbits"));
  REQUIRE_FALSE(eval("how many m in km"));
}
