#pragma once
#include "numen/unit.hpp"
#include "numen/tz.hpp"
#include <chrono>
#include <compare>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include "abstract-currency-provider.hpp"

namespace numen {

using TimePoint = std::chrono::time_point<std::chrono::system_clock>;

enum class DateTimePrecision { DateTime, Time, Date, Month, Year };

struct DateTimeFormatOptions {
  // If relative is set to true, only "meaningful" part of the date time
  // are rendered: if there is no time, only the date part is shown, if date is the
  // same as the current local date, only the time is shown, etc...
  bool relative = true;

  // Whether to render the timezone
  // The timezone string can be obtained separately using `toTimezoneString`.
  bool withTz = true;

  // Neutral is the canonical, locale-independent form (ISO-like), Local follows
  // the locale's date and time conventions.
  enum class TimeFormat { Neutral, Local };

  TimeFormat format = TimeFormat::Local;

  // Locale used when format is Local. If not specified, the environment's
  // locale is used.
  std::optional<std::string> locale;
};

struct Timezone {
  const tz::time_zone *tz = nullptr;
  std::chrono::seconds offset = std::chrono::seconds(0);

  // Whether this timezone has the same local time as the
  // user timezone.
  bool isLocalTime() const;

  // Whether the current timezone is the same as this timezone.
  bool isUser() const;

  std::string toString() const;
};

struct DateTime {
  TimePoint time;
  const tz::time_zone *tz = nullptr;
  std::chrono::seconds offset = std::chrono::seconds(0);

  DateTimePrecision format = DateTimePrecision::DateTime;

  auto operator<=>(const DateTime &rhs) const { return time <=> rhs.time; }
  bool operator==(const DateTime &rhs) const { return time == rhs.time; }

  std::string toString(const DateTimeFormatOptions &opts = {}) const;

  // Human readable representation of the timezone + optional additonal offset
  // attached to this DateTime object.
  std::string toTimezoneString() const;

  // Whether the timezone attached to this DateTime (if any) is the same
  // as the current timezone. Note that the concept of current timezone
  // is not impacted by any timezone override that may have been set during
  // calculation. It always refer to the current timezone.
  bool isCurrentTimezone() const;
};

struct Duration {
  std::optional<std::chrono::years> years;
  std::optional<std::chrono::months> months;

  // anything that is not a calendar unit can collapse as seconds
  std::optional<std::chrono::seconds> seconds;
  std::optional<std::chrono::nanoseconds> subsecond;

  // every producer returns this, so `subsecond` is always under a second and
  // always shares the sign of `seconds`. read the fields directly.
  Duration normalised() const {
    constexpr auto perSecond = std::chrono::nanoseconds{std::chrono::seconds{1}}.count();

    auto s = seconds.value_or(std::chrono::seconds{0}).count();
    auto ns = subsecond.value_or(std::chrono::nanoseconds{0}).count();

    s += ns / perSecond;
    ns %= perSecond;

    if (s > 0 && ns < 0) { --s, ns += perSecond; }
    if (s < 0 && ns > 0) { ++s, ns -= perSecond; }

    Duration n = *this;
    n.seconds = std::chrono::seconds{s};
    n.subsecond = std::chrono::nanoseconds{ns};

    return n;
  }

  std::chrono::seconds total() const {
    return years.value_or(std::chrono::years(0)) + months.value_or(std::chrono::months{0}) +
           seconds.value_or(std::chrono::seconds{0});
  }

  Duration operator+(const Duration &rhs) const {
    Duration n;
    n.years = years.value_or(std::chrono::years{0}) + rhs.years.value_or(std::chrono::years{0});
    n.months = months.value_or(std::chrono::months{0}) + rhs.months.value_or(std::chrono::months{0});
    n.seconds = seconds.value_or(std::chrono::seconds{0}) + rhs.seconds.value_or(std::chrono::seconds{0});
    n.subsecond =
        subsecond.value_or(std::chrono::nanoseconds{0}) + rhs.subsecond.value_or(std::chrono::nanoseconds{0});
    return n.normalised();
  }

  Duration operator-(const Duration &rhs) const {
    Duration n;
    n.years = years.value_or(std::chrono::years{0}) - rhs.years.value_or(std::chrono::years{0});
    n.months = months.value_or(std::chrono::months{0}) - rhs.months.value_or(std::chrono::months{0});
    n.seconds = seconds.value_or(std::chrono::seconds{0}) - rhs.seconds.value_or(std::chrono::seconds{0});
    n.subsecond =
        subsecond.value_or(std::chrono::nanoseconds{0}) - rhs.subsecond.value_or(std::chrono::nanoseconds{0});
    return n.normalised();
  }

  auto operator<=>(const Duration &rhs) const {
    if (auto order = total() <=> rhs.total(); std::is_neq(order)) return order;
    return subsecond.value_or(std::chrono::nanoseconds{0}) <=>
           rhs.subsecond.value_or(std::chrono::nanoseconds{0});
  }

  bool operator==(const Duration &rhs) const {
    return total() == rhs.total() && subsecond.value_or(std::chrono::nanoseconds{0}) ==
                                         rhs.subsecond.value_or(std::chrono::nanoseconds{0});
  }

  // what evaluate() prints, e.g. "1 yr 2 months 3 days 4 hr"
  std::string toString() const;
};

// the single place a value plus a duration unit becomes a Duration
std::optional<Duration> durationFrom(double value, const UnitDef &unit);

struct Time {
  std::chrono::seconds seconds;
};

enum class NumberOutputFormat { Decimal, Hexadecimal, Binary, Octal };

struct Number {
  // unit may remain ambigious until more information is known.
  // To deal with that, the attached unit can either be a fully
  // qualified unit or a "raw" unit, that is the unit-like string
  // as it was parsed.
  // For instance: in "1m to s" the "m" unit can refer to "meters" or "minutes".
  // In this case, the second operand is what will allow disambiguation since both
  // are durations. Until we are able to consider the conversion operation, "1m" is
  // ambiguous.

  double n;

  // the rendered magnitude without its unit, e.g. "1.5" for "1.5 km"
  std::string text;

  NumberOutputFormat format;

  struct Unit {
    // owned: a view into the expression would dangle for callers of compute()
    std::string raw;
    std::optional<CompoundUnit> resolved;

    const UnitDef *def() const { return resolved ? resolved->sole() : nullptr; }
  };

  std::optional<Unit> unit;

  // n holds the fraction, so "50%" is 0.5
  bool isPercentage = false;

  bool operator==(const Number &rhs) const { return n == rhs.n; }
  std::partial_ordering operator<=>(const Number &rhs) const { return n <=> rhs.n; }

  // what evaluate() prints: "1.5km", "6m²", "$12.5". a unit that never
  // resolved falls back to the way it was typed
  std::string toString() const;
};

struct Boolean {
  bool value;
  auto operator<=>(const Boolean &rhs) const = default;

  std::string toString() const;
};

using ValueType = std::variant<Number, DateTime, Boolean, Duration>;

template <class T> struct ConversionOf {
  std::optional<T> from;
  T to;
};

struct Conversion {
  std::variant<ConversionOf<Number::Unit>, ConversionOf<Timezone>> sides;

  // locale currency conversion
  bool implicit = false;

  template <class T> const ConversionOf<T> *as() const { return std::get_if<ConversionOf<T>>(&sides); }
};

template <class T> std::string_view valueName() {
  if constexpr (std::is_same_v<T, Duration>) {
    return "Duration";
  } else if constexpr (std::is_same_v<T, Number>) {
    return "Number";
  } else if constexpr (std::is_same_v<T, DateTime>) {
    return "DateTime";
  } else {
    static_assert(std::is_same_v<T, Boolean>);
    return "Boolean";
  }
}

struct ComputedValue {
  ValueType value;

  std::optional<Conversion> conversion;

  bool isNumber() const { return std::holds_alternative<Number>(value); }

  bool isDateTime() const { return std::holds_alternative<DateTime>(value); }

  const Number *asNumber() const { return std::get_if<Number>(&value); }
  Number *asNumber() { return std::get_if<Number>(&value); }
  const DateTime *asDateTime() const { return std::get_if<DateTime>(&value); }

  const Duration *asDuration() { return std::get_if<Duration>(&value); }
  const Duration *asDuration() const { return std::get_if<Duration>(&value); }

  std::string_view valueTypeName() const {
    return std::visit([](const auto &v) { return valueName<std::remove_cvref_t<decltype(v)>>(); }, value);
  }

  // the default rendering; pass EvalConfig::effectiveDateTimeFormat() to get
  // exactly what evaluate() returns for date time values
  std::string toString(const DateTimeFormatOptions &dateTimeFormat = {}) const;
};

struct ParseOptions {
  // return an error if an unknown token is encountered, instead of skipping it.
  bool strict = false;

  /**
   * Locale to use for implicit conversions, the accepted decimal separator and
   * the one results are rendered with. If not specified, the default locale is used.
   */
  std::optional<std::string> locale;

  std::locale effectiveLocale() const {
    return locale.transform([](auto &&str) { return std::locale{str}; }).value_or(std::locale{""});
  }
};

struct EvalOptions {
  ParseOptions parseOptions;

  /**
   * Timepoint that should be used as the current time.
   * You most likely don't want to change this, the main use case is testing.
   */
  std::optional<TimePoint> now;

  const tz::time_zone *timezone;

  /**
   * If the expression is only a currency, it will automatically be
   * converted to the locale currency.
   * e.g if locale is set to fr_FR, "100 usd" will automatically convert
   * to euro.
   */
  bool implicitCurrencyConversion = true;

  bool implicitTimezoneConversion = true;

  /**
   * Units that declare an implicit target convert to it when they are the
   */
  bool implicitUnitConversion = true;

  /**
   * How evaluate() renders date time values. Localized by default; set
   * `format` to Neutral for the canonical, locale-independent form.
   * If `locale` is left unset here, the config-level `locale` above is used.
   */
  DateTimeFormatOptions dateTimeFormat;

  // `dateTimeFormat` with its locale defaulted from the config-level `locale`:
  // the options evaluate() actually renders date time values with
  DateTimeFormatOptions effectiveDateTimeFormat() const {
    auto fmt = dateTimeFormat;
    if (!fmt.locale) fmt.locale = parseOptions.locale;
    return fmt;
  }
};

template <typename T> struct IsDuration : std::false_type {};

template <typename Rep, typename Period>
struct IsDuration<std::chrono::duration<Rep, Period>> : std::true_type {};

template <typename T> inline constexpr bool is_duration_v = IsDuration<T>::value;

class Numen {

public:
  Numen();

  std::expected<std::string, std::string> evaluate(std::string_view expr, const EvalOptions &opts = {});
  std::expected<ComputedValue, std::string> compute(std::string_view expr, const EvalOptions &opts = {});

  template <typename T>
  std::expected<T, std::string> parse(std::string_view expr, const EvalOptions &opts = {}) {
    if constexpr (is_duration_v<T>) {
      return parse<Duration>(expr, opts)
          .transform([](const Duration &value) {
            constexpr auto into = [](auto &&d) { return std::chrono::duration_cast<T>(d); };
            T d{0};
            if (auto y = value.years) d += into(*y);
            if (auto m = value.months) d += into(*m);
            if (auto s = value.seconds) d += into(*s);
            if (auto ns = value.subsecond) d += into(*ns);
            return d;
          })
          .or_else([&](auto &&) {
            return parse<Number>(expr, opts).transform([](const Number &n) {
              return T{static_cast<long long>(n.n)};
            });
          });
    } else if constexpr (std::is_same_v<T, bool>) {
      return parse<Boolean>(expr, opts).transform([](const auto &value) { return value.value; });
    } else if constexpr (std::is_arithmetic_v<T>) {
      return parse<Number>(expr, opts).transform([](const auto &value) { return static_cast<T>(value.n); });
    } else if constexpr (std::is_same_v<T, std::chrono::system_clock::time_point>) {
      return parse<DateTime>(expr, opts).transform([](const auto &value) {
        return static_cast<T>(value.time);
      });
    } else {
      auto res = compute(expr, opts);

      if (!res) return std::unexpected(res.error());

      if (auto v = std::get_if<T>(&res.value().value)) return *v;

      return std::unexpected(std::format("Could not parse expression as {}, got {} instead", valueName<T>(),
                                         res->valueTypeName()));
    }
  }

  void printAST(const std::string &expr) const;

  void setCurrencyProvider(std::unique_ptr<AbstractCurrencyProvider> provider) {
    m_currencyProvider = std::move(provider);
    m_unitDb.setCurrencyProvider(*m_currencyProvider);
  }

  void updateRates() { m_currencyProvider->updateRates(); }

  AbstractCurrencyProvider &currencyProvider() { return *m_currencyProvider; }
  const AbstractCurrencyProvider &currencyProvider() const { return *m_currencyProvider; }

private:
  std::unique_ptr<AbstractCurrencyProvider> m_currencyProvider;
  UnitDatabase m_unitDb;
};

} // namespace numen

// every value formats as its toString(), so std::format("{}", value) is
// evaluate() for the part of the result you did not render yourself
template <typename T>
  requires std::is_same_v<T, numen::Number> || std::is_same_v<T, numen::DateTime> ||
           std::is_same_v<T, numen::Duration> || std::is_same_v<T, numen::Boolean> ||
           std::is_same_v<T, numen::ComputedValue>
// NOLINTNEXTLINE(bugprone-std-namespace-modification)
struct std::formatter<T> : std::formatter<std::string> {
  auto format(const T &value, std::format_context &ctx) const {
    return std::formatter<std::string>::format(value.toString(), ctx);
  }
};
