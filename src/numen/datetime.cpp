#include <chrono>
#include <cmath>
#include <limits>
#include <format>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include "datetime.hpp"
#include "numen/numen.hpp"
#include "timezone.hpp"

namespace numen {

namespace detail {

Duration subtractDates(const DateTime &lhs, const DateTime &rhs) {
  using namespace std::chrono;

  auto lhsDays = floor<days>(lhs.time);
  auto rhsDays = floor<days>(rhs.time);

  // calendar difference a -> b, assuming a <= b
  auto amd = year_month_day{floor<days>(lhsDays)};
  auto bmd = year_month_day{floor<days>(rhsDays)};

  auto atod = lhs.time - lhsDays;
  auto btod = rhs.time - rhsDays;

  auto m = (bmd.year() / bmd.month()) - (amd.year() / amd.month()); // chrono::months, exact
  if (bmd.day() < amd.day()) --m;                                   // last month isn't complete yet

  auto anchor = amd + m; // same clamped month-shift you already have
  if (!anchor.ok()) anchor = anchor.year() / anchor.month() / last;

  auto d = sys_days{bmd} - sys_days{anchor}; // exact leftover days

  auto y = m / 12;
  auto mo = m % 12;

  const seconds secs =
      duration_cast<seconds>(days{d}) - seconds{std::abs(duration_cast<seconds>(atod - btod).count())};

  return Duration{.years = std::chrono::years{std::abs(y.count())},
                  .months = std::chrono::months{std::abs(mo.count())},
                  .seconds = seconds{std::abs(secs.count())}};
}

TimePoint checkedTimePoint(std::chrono::sys_seconds t) {
  // pinned to nanoseconds rather than the clock's tick, which varies per stdlib
  using Nanos = std::chrono::nanoseconds;
  constexpr std::chrono::seconds LIMIT{std::numeric_limits<Nanos::rep>::max() / Nanos::period::den -
                                       std::chrono::seconds{std::chrono::days{1}}.count()};

  if (t.time_since_epoch() > LIMIT || t.time_since_epoch() < -LIMIT) {
    throw std::runtime_error("Date is out of the representable range");
  }

  return TimePoint{t};
}

static TimePoint parseDateTimeLiteral(const DateTimeLiteral &d, const tz::time_zone &tz, TimePoint now) {
  std::chrono::year_month_day today{std::chrono::floor<std::chrono::days>(now)};

  auto process = [&](auto &&date) {
    tz::local_seconds t{std::chrono::local_days{date}.time_since_epoch()};

    if (auto time = d.time) {
      if (auto h = time->hours) t += *h;
      if (auto min = time->minutes) t += *min;
      if (auto secs = time->seconds) t += *secs;
    }

    return checkedTimePoint(tz.to_sys(t));
  };

  return std::visit(
      [&](const auto &v) {
        using T = std::remove_cvref_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::chrono::weekday>) {
          const std::chrono::year_month_weekday date{d.year.value_or(today.year()),
                                                     d.month.value_or(today.month()), v[0]};
          return process(date);
        } else if constexpr (std::is_same_v<T, std::chrono::day>) {
          const std::chrono::year_month_day date{d.year.value_or(today.year()),
                                                 d.month.value_or(today.month()), v};
          return process(date);
        }
      },
      d.day.value_or(today.day()));
}

DateTime parseDateTime(const DateString &d, const tz::time_zone &userTz, TimePoint now) {
  auto tz = d.timezone
                .and_then([](auto &&t) -> std::optional<const tz::time_zone *> {
                  return TimezoneDB{}.query(t.name);
                })
                .value_or(&userTz);

  auto visitor = [&](const auto &value) -> TimePoint {
    using T = std::remove_cvref_t<decltype(value)>;
    if constexpr (std::is_same_v<T, DateTimeLiteral>) {
      return parseDateTimeLiteral(value, *tz, now);
    } else if constexpr (std::is_same_v<T, RelativeDateTimeLiteral>) {
      return std::visit(
          [&](auto &&anchor) {
            using A = std::remove_cvref_t<decltype(anchor)>;
            const int sign = value.direction == RelativeDateTimeLiteral::Direction::Past ? -1 : 1;
            if constexpr (std::is_same_v<A, std::chrono::weekday>) {
              // TODO: implement weekday delta
              return now;
            } else {
              static_assert(std::is_same_v<A, Duration>);

              if (auto &y = anchor.years) now = shift<std::chrono::years>(now, sign * *y);
              if (auto &m = anchor.months) now = shift<std::chrono::months>(now, sign * *m);
              if (auto &s = anchor.seconds) now = now + sign * *s;

              // round to local day
              if (value.time || value.precision == DateTimePrecision::Date) {
                // Convert to local time first, then extract the date in the local timezone
                const auto localTime = tz->to_local(now);
                const tz::local_days localDays = std::chrono::floor<std::chrono::days>(localTime);
                const tz::year_month_day date{localDays};
                now = tz->to_sys(tz::local_seconds{tz::local_days{date}.time_since_epoch()});
              }

              // we apply the duration first, then override the time of the current day
              if (auto t = value.time) {
                if (auto h = t->hours) now += *h;
                if (auto m = t->minutes) now += *m;
                if (auto s = t->seconds) now += *s;
              }

              return now;
            }
          },
          value.delta);
    } else {
      return now;
    }
  };

  auto instant = std::visit(visitor, d.value);

  return DateTime{
      .time = instant, .tz = tz, .offset = d.timezone ? d.timezone->offset : std::chrono::seconds{}};
}

} // namespace detail

bool DateTime::isCurrentTimezone() const { return tz == tz::get_tzdb().current_zone(); }

std::string DateTime::toTimezoneString() const { return Timezone{tz, offset}.toString(); }

bool Timezone::isUser() const { return tz == tz::current_zone(); }

bool Timezone::isLocalTime() const {
  auto now = std::chrono::system_clock::now();
  return tz->get_info(now).offset + offset == tz::current_zone()->get_info(now).offset;
}

std::string Timezone::toString() const {
  std::string out{};

  out += TimezoneDB::canonicalName(*tz);

  if (offset.count() != 0) {
    auto hours = offset.count() / 3600;
    auto minutes = std::abs((offset.count() - hours * 3600) / 60);

    if (minutes != 0) {
      out += std::format("{:+}:{}", hours, minutes);
    } else {
      out += std::format("{:+}", hours);
    }
  }

  return out;
}

std::string DateTime::toRFC3339() const {
  // discard subsecond
  const auto tp = std::chrono::time_point_cast<std::chrono::seconds>(time);
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", tp);
}

// TODO: if we want really idomatic display for dates, we probably
// want to integrate https://cldr.unicode.org/translation/date-time/date-time-patterns
// For now we don't, we just use the basic localized form
std::string DateTime::toString(const DateTimeFormatOptions &opts) const {
  constexpr auto fl = [](auto &&t) { return std::chrono::floor<std::chrono::days>(t); };
  auto now = std::chrono::system_clock::now();
  // relative elision is decided in the displayed frame, so what is dropped is
  // exactly what would have rendered as redundant (midnight, today's date)
  // whole seconds: %S would otherwise print the clock's fractional digits
  constexpr auto toLocal = [](const auto &zone, auto &&t) {
    return std::chrono::floor<std::chrono::seconds>(
        std::chrono::local_time{zone->to_local(t).time_since_epoch()});
  };
  const auto displayTz = tz ? tz : tz::current_zone();
  const auto localTime = toLocal(displayTz, time + offset);
  const auto localNow = toLocal(tz::current_zone(), now);
  const bool isSameLocalDay = fl(localNow) == fl(localTime);
  const bool hasTime = (localTime - fl(localTime)).count() != 0;
  const bool localized = opts.format == DateTimeFormatOptions::TimeFormat::Local;
  const auto loc = localized ? resolveLocale(opts.locale) : std::locale::classic();
  std::string out{};

  // we still want to show the date for today if there is no time
  if (!opts.relative || !isSameLocalDay || !hasTime) {
    if (localized) {
      out += std::format(loc, "{:L%x}", localTime);
    } else {
      out += std::format("{:%F}", localTime);
    }
  }

  if (!opts.relative || hasTime) {
    if (!out.empty()) out += " ";
    if (localized) {
      out += std::format(loc, "{:L%X}", localTime);
    } else {
      out += std::format("{:%H:%M:%OS}", localTime);
    }
  }

  if (tz && opts.withTz) {
    if (!out.empty()) out += " ";
    out += std::format("({})", toTimezoneString());
  }

  return out;
}

} // namespace numen
