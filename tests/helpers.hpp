#pragma once

#include "numen/numen.hpp"
#include "timezone.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <expected>
#include <string>
#include <string_view>

namespace Catch {
template <typename T, typename E> struct StringMaker<std::expected<T, E>> {
  static std::string convert(const std::expected<T, E> &res) {
    if (res) { return ::Catch::Detail::stringify(*res); }
    return "unexpected(" + ::Catch::Detail::stringify(res.error()) + ")";
  }
};
} // namespace Catch

namespace test {

// resolves links to the canonical zone, which raw locate_zone does not do with the date backend
inline const numen::tz::time_zone *zone(std::string_view name) { return TimezoneDB{}.query(name); }

// "now" is frozen so that tests depending on the current date stay valid
inline const numen::EvalOptions &frozenConfig() {
  static const numen::EvalOptions config = [] {
    std::chrono::year_month_day now{std::chrono::year{2026}, std::chrono::month{1}, std::chrono::day{18}};
    return numen::EvalOptions{
        .parseOptions =
            {
                // without this the ambient locale decides whether currencies auto-convert
                .locale = "en_US",
            },
        .now = std::chrono::sys_days(now),
        .timezone = test::zone("UTC"),

        // tests assert the full canonical form on purpose: localized output
        // depends on the host's locale data, relative output on the wall clock
        .dateTimeFormat = {.relative = false, .format = numen::DateTimeFormatOptions::TimeFormat::Neutral},
    };
  }();
  return config;
}

inline void assertExpr(std::string_view expr, std::string_view expected,
                       const numen::EvalOptions &opts = {}) {
  CAPTURE(expr);
  numen::Numen calc;
  auto res = calc.evaluate(expr, opts);
  if (!res) {
    FAIL_CHECK("evaluation failed: " << res.error());
    return;
  }
  CHECK(res.value() == expected);
}

inline void assertNumber(std::string_view expr, double expected, double margin = 1e-9,
                         const numen::EvalOptions &opts = {}) {
  CAPTURE(expr);
  numen::Numen calc;
  auto res = calc.compute(expr, opts);
  if (!res) {
    FAIL_CHECK("evaluation failed: " << res.error());
    return;
  }
  if (!res->isNumber()) {
    FAIL_CHECK("result is not a number");
    return;
  }
  CHECK_THAT(res->asNumber()->n, Catch::Matchers::WithinAbs(expected, margin));
}

} // namespace test
