#include "timezone.hpp"
#include "numen/numen.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

constexpr auto TAG = "[timezone]";

TEST_CASE("should find timezone using fully qualifed name", TAG) {
  TimezoneDB db;
  auto tz = db.query("America/New_York");

  REQUIRE(tz);
}

TEST_CASE("should not find timezone using wrong qualifed name", TAG) {
  TimezoneDB db;
  auto tz = db.query("Americo/New_Yorgue");

  REQUIRE(!tz);
}

TEST_CASE("city name alone should match unique timezone", TAG) {
  TimezoneDB db;
  auto tz = db.query("New_York");

  REQUIRE(tz);
  REQUIRE(tz->name() == "America/New_York");
}

TEST_CASE("city name alone should match unique timezone, case insensitively", TAG) {
  TimezoneDB db;
  auto tz = db.query("new_york");

  REQUIRE(tz);
  REQUIRE(tz->name() == "America/New_York");
}

TEST_CASE("city name alone should match unique timezone, case insentively and "
          "without underscores",
          TAG) {
  TimezoneDB db;
  auto tz = db.query("new york");

  REQUIRE(tz);
  REQUIRE(tz->name() == "America/New_York");
}

TEST_CASE("city name alone should match unique timezone, case insentively and "
          "without underscores, and with any amount of spaces",
          TAG) {
  TimezoneDB db;
  auto tz = db.query("   new     york ");

  REQUIRE(tz);
  REQUIRE(tz->name() == "America/New_York");
}

TEST_CASE("city name for a timezone with many /", TAG) {
  using SI = std::initializer_list<std::string_view>;

  TimezoneDB db;
  auto tz = db.query("buenos aires");

  REQUIRE(tz);
  REQUIRE(std::ranges::contains(SI{"America/Argentina/Buenos_Aires", "America/Buenos_Aires"}, tz->name()));
}

TEST_CASE("should handle famous european timezones", TAG) {
  TimezoneDB db;

  {
    auto tz = db.query("berlin");
    REQUIRE(tz);
    CHECK(tz->name() == "Europe/Berlin");
  }
  {
    auto tz = db.query("paris");
    REQUIRE(tz);
    CHECK(tz->name() == "Europe/Paris");
  }

  {
    auto tz = db.query("rome");
    REQUIRE(tz);
    CHECK(tz->name() == "Europe/Rome");
  }
}

#ifndef NUMEN_USE_DATE_TZ
TEST_CASE("Resolve timezone links", TAG) {
  TimezoneDB db;

  for (const auto &link : numen::tz::get_tzdb().links) {
    if (std::ranges::any_of(TimezoneDB::customLinks(), [&](auto &&l) { return l.name == link.name(); }))
      continue;
    CAPTURE(link.name());
    auto tz = db.query(link.name());
    CHECK(tz);
    // bare legacy names (Greenwich) are places first
    if (tz && link.name().contains('/')) { CHECK(tz->name() == link.target()); }
  }
}
#endif

TEST_CASE("resolve custom tz link", TAG) {
  TimezoneDB db;

  {
    auto tz = db.query("nyc");
    REQUIRE(tz);
    CHECK(tz->name() == "America/New_York");
  }

  {
    auto tz = db.query("est");
    REQUIRE(tz);
    CHECK(tz->name() == "America/New_York");
  }

  {
    auto tz = db.query("PT");
    REQUIRE(tz);
    CHECK(tz->name() == "America/Los_Angeles");
  }

  {
    auto tz = db.query("MT");
    REQUIRE(tz);
    CHECK(tz->name() == "America/Denver");
  }

  {
    auto tz = db.query("CT");
    REQUIRE(tz);
    CHECK(tz->name() == "America/Chicago");
  }

  {
    auto tz = db.query("ET");
    REQUIRE(tz);
    CHECK(tz->name() == "America/New_York");
  }
}

TEST_CASE("resolve places from the geo database", TAG) {
  TimezoneDB db;

  auto expect = [&](std::string_view query, std::string_view zone) {
    CAPTURE(query);
    auto tz = db.query(query);
    REQUIRE(tz);
    CHECK(TimezoneDB::canonicalName(*tz) == zone);
  };

  // cities that are not zone names
  expect("beijing", "Asia/Shanghai");
  expect("san francisco", "America/Los_Angeles");
  expect("munich", "Europe/Berlin");
  expect("München", "Europe/Berlin");
  expect("bombay", "Asia/Kolkata");
  expect("Washington, D.C.", "America/New_York");

  expect("Москва", "Europe/Moscow");
  expect("МОСКВА", "Europe/Moscow");
  expect("北京", "Asia/Shanghai");
  expect("Αθήνα", "Europe/Athens");
  expect("Sài Gòn", "Asia/Ho_Chi_Minh");
  expect("washington", "America/New_York");
  expect("washington state", "America/Los_Angeles");

  // states, provinces, countries
  expect("texas", "America/Chicago");
  expect("bavaria", "Europe/Berlin");
  expect("japan", "Asia/Tokyo");
  expect("usa", "America/New_York");
  expect("uk", "Europe/London");
  expect("brazil", "America/Sao_Paulo");

  // most populous wins, unless qualified
  expect("paris", "Europe/Paris");
  expect("paris texas", "America/Chicago");
  expect("paris tx", "America/Chicago");
  expect("paris france", "Europe/Paris");
  expect("portland", "America/Los_Angeles");
  expect("portland maine", "America/New_York");
  expect("london", "Europe/London");
  expect("london ontario", "America/Toronto");
  expect("san jose", "America/Los_Angeles");
  expect("san jose costa rica", "America/Costa_Rica");
  expect("la", "America/Los_Angeles");

  // "to" and "of" are towns
  CHECK(!db.query("to"));
  CHECK(!db.query("of"));
  CHECK(!db.query("in"));
  CHECK(!db.query("paris in"));
  CHECK(!db.query("nowhere at all"));
}

TEST_CASE("timezone isUser utility should work properly") {
  // we need proper testing and mocking of the current user timezone actually...

  {
    auto lhs = numen::Timezone{.tz = numen::tz::locate_zone("Asia/Tokyo")};

    REQUIRE(!lhs.isUser());
    REQUIRE(!lhs.isLocalTime());
  }
}
