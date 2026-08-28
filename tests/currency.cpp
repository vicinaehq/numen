#include "numen/numen.hpp"
#include <catch2/catch_test_macros.hpp>
#include "region-currency.hpp"
#include "mock-currency-provider.hpp"

TEST_CASE("Locale to currency mapping") {
  using numen::currencyForLocale;
  using numen::currencyForRegion;

  REQUIRE(currencyForRegion("FR") == "EUR");
  REQUIRE(currencyForRegion("fr") == "EUR");
  REQUIRE(currencyForRegion("US") == "USD");
  REQUIRE_FALSE(currencyForRegion("AQ").has_value());
  REQUIRE_FALSE(currencyForRegion("FRA").has_value());

  REQUIRE(currencyForLocale("fr_FR") == "EUR");
  REQUIRE(currencyForLocale("en-US") == "USD");
  REQUIRE(currencyForLocale("fr_FR.UTF-8@euro") == "EUR");
  REQUIRE(currencyForLocale("en_US.utf8") == "USD");
  REQUIRE(currencyForLocale("uz-Cyrl-UZ") == "UZS");
  REQUIRE(currencyForLocale("ja_JP") == "JPY");
  REQUIRE_FALSE(currencyForLocale("fr").has_value());
  REQUIRE_FALSE(currencyForLocale("C").has_value());
  REQUIRE_FALSE(currencyForLocale("POSIX").has_value());
  REQUIRE_FALSE(currencyForLocale("").has_value());
}

TEST_CASE("currency results snap to the minor units of the target", "[currency]") {
  auto calc = test::mockCalc();

  // eur takes two decimals, jpy and krw none
  CHECK(calc.evaluate("100 usd to eur") == "€92.35");
  CHECK(calc.evaluate("100 usd to jpy") == "¥15,789");
  CHECK(calc.evaluate("100 usd to krw") == "₩123,457");
}

TEST_CASE("rounding money is a formatting concern, not a value one", "[currency]") {
  auto calc = test::mockCalc();

  auto res = calc.compute("100 usd to eur");
  REQUIRE(res);

  // what gets shown sits on the minor units
  CHECK(res->asNumber()->text == "92.35");

  // and the full precision survives into further arithmetic
  CHECK(calc.evaluate("(100 usd to eur) * 100") == "€9,234.57");
}

TEST_CASE("a currency is not padded out to its minor units", "[currency]") {
  auto calc = test::mockCalc();

  // the minor units cap the precision, they do not pad it: rendering money
  // properly is the caller's job, and half of the convention reads worse than none
  CHECK(calc.evaluate("1 usd") == "$1");
  CHECK(calc.evaluate("0.9 usd") == "$0.9");
  CHECK(calc.evaluate("1 usd / 2") == "$0.5");
  CHECK(calc.evaluate("1 km") == "1km");
}

TEST_CASE("a currency amount lands on its minor units even without a conversion", "[currency]") {
  auto calc = test::mockCalc();

  CHECK(calc.evaluate("0.22392323090 usd") == "$0.22");
  CHECK(calc.evaluate("10.005 usd") == "$10.01");
  CHECK(calc.evaluate("1 usd / 3") == "$0.33");

  // jpy has no minor units at all
  numen::EvalOptions jp{.parseOptions{.locale = "ja_JP"}};
  CHECK(calc.evaluate("1 jpy / 3", jp) == "¥0");
  CHECK(calc.evaluate("1234.7 jpy", jp) == "¥1,235");

  // a plain unit is untouched by any of this
  CHECK(calc.evaluate("1 km / 3") == "0.333333km");
}

TEST_CASE("minor units are declared on the unit itself", "[currency]") {
  auto calc = test::mockCalc();

  // btc is a builtin with satoshi precision, so a tiny amount keeps its
  // digits instead of rounding to the two decimals fiat gets
  CHECK(calc.evaluate("1 eur to btc") == "₿0.00001267");
  CHECK(calc.evaluate("1 jpy to btc") == "₿0.00000007");
  CHECK(calc.evaluate("1 usd to btc") == "₿0.0000117");
  CHECK(calc.evaluate("0.123456789 btc to btc") == "₿0.12345679");
}

TEST_CASE("a ticker the provider quotes a rate for is a currency", "[currency]") {
  auto calc = test::mockCalc();

  // no builtin knows "xmr" as a currency, only the provider does
  CHECK(calc.evaluate("100 usd to xmr") == "0.5XMR");
  CHECK(calc.evaluate("1 xmr to usd") == "$200");
  CHECK(calc.evaluate("1 XMR to usd") == "$200");
  CHECK(calc.evaluate("1 xmr to eur") == "€184.69");

  // a bare amount localizes like any other currency does
  CHECK(calc.evaluate("2 xmr", {.parseOptions{.locale = "en_US"}}) == "$400");

  // provider currencies carry no minor units of their own, so they get a
  // fixed high precision rather than the fiat default
  CHECK(calc.evaluate("1 usd to shib") == "100,000SHIB");
  CHECK(calc.evaluate("1 shib / 3 to shib") == "0.33333333SHIB");
  CHECK(calc.evaluate("1000000 shib to btc") == "₿0.000117");

  // while the target's minor units still rule once it is fiat
  CHECK(calc.evaluate("1 shib to usd") == "$0");
  CHECK(calc.evaluate("1000 shib to usd") == "$0.01");

  // a builtin always wins over a ticker of the same name: "m" stays a meter
  // even though the provider quotes a rate for it
  CHECK(calc.evaluate("1 m to cm") == "100cm");
  CHECK_FALSE(calc.evaluate("1 m to usd"));

  // without a provider quoting it, the token is not a unit at all
  auto bare = numen::Numen{}.compute("2 xmr");
  REQUIRE(bare);
  CHECK_FALSE(bare->asNumber()->unit);
  CHECK_FALSE(calc.evaluate("1 usd to zzz"));
}

TEST_CASE("the provider supplies the rates a conversion uses", "[currency]") {
  numen::Numen calc;
  calc.setCurrencyProvider(std::make_unique<test::MockCurrencyProvider>());
  auto &provider = dynamic_cast<test::MockCurrencyProvider &>(calc.currencyProvider());

  CHECK(calc.evaluate("100 usd to eur") == "€92.35");
  CHECK(calc.evaluate("100 usd to gbp") == "£78.91");

  // an unknown code has no rate, so the conversion cannot be done
  CHECK_FALSE(calc.evaluate("100 usd to zzz"));
  CHECK(provider.updateCount() == 0);
  calc.updateRates();
  CHECK(provider.updateCount() == 1);
}

TEST_CASE("a rate applies inside a composed unit too", "[currency][compound]") {
  auto calc = test::mockCalc();

  CHECK(calc.evaluate("100 usd / 2 hr to eur/hr") == "€46.172835/h");
  CHECK(calc.evaluate("1000 usd / 1 kg to eur/kg") == "€923.4567/kg");

  // the hour is a factor on both sides, so only the rate is left to apply
  CHECK(calc.evaluate("60 usd / 1 hr to eur/min") == "€0.923457/min");

  // money per something is a rate, money times money is nothing at all
  CHECK_FALSE(calc.evaluate("1 usd * 1 usd"));
  CHECK_FALSE(calc.evaluate("1 usd / 1 hr to eur*eur"));

  // the same currency cancels before any rate is consulted, which is why this
  // works even on a calculator with no provider at all
  CHECK(calc.evaluate("100 usd / 2 hr to usd/hr") == "$50/h");
  CHECK(numen::Numen{}.evaluate("100 usd / 2 hr to usd/hr") == "$50/h");

  CHECK_FALSE(calc.evaluate("100 usd / 2 hr to zzz/hr"));
}

TEST_CASE("currency with $ in the name should work (crypto tickers)", "[currency]") {
  auto calc = test::mockCalc();

  // as per the mock, $ticker = 0.5usd
  CHECK(calc.evaluate("100 $ticker to usd") == "$50");
  CHECK(calc.evaluate("100 $TICKER to usd") == "$50");
}

TEST_CASE("a sum is as implicit as a literal, only a 'to' pins the currency", "[currency]") {
  auto calc = test::mockCalc();
  numen::EvalOptions fr{.parseOptions{.locale = "fr_FR"}};

  CHECK(calc.evaluate("100 usd", fr) == "€92,35");
  CHECK(calc.evaluate("50 usd + 50 usd", fr) == "€92,35");
  CHECK(calc.evaluate("(50 usd to usd) + 50 usd", fr) == "$100");
}
