#pragma once

#include "numen/numen.hpp"
#include <cmath>
#include <compare>
#include <limits>
#include <locale>
#include <optional>
#include <string>

namespace numen {

// from the wide numpunct facet: the narrow one truncates multi-byte separators
struct NumberLocale {
  std::string decimalPoint = ".";
  std::string groupSep = ",";
  std::string grouping = "\3"; // numpunct semantics: sizes right to left, last repeats

  static NumberLocale from(const std::locale &loc);
};

// Every number is a double, which is the trade numbat makes too: a utility
// calculator wants boring, predictable arithmetic, and the places a double falls
// short are well past anything anyone types. Overflow shows as inf.
class Value {
public:
  Value() = default;
  Value(double n) : m_n(n) {}

  // mantissa * 10^scale, scaled in one step so a literal is correctly rounded
  static Value scaled(double mantissa, int scale);

  double toDouble() const { return m_n; }

  template <typename T> T to() const { return static_cast<T>(m_n); }

  bool isZero() const { return m_n == 0; }
  bool isNaN() const { return std::isnan(m_n); }
  bool isInteger() const { return std::isfinite(m_n) && m_n == std::trunc(m_n); }

  Value operator-() const { return Value{-m_n}; }
  Value operator+(const Value &rhs) const { return Value{m_n + rhs.m_n}; }
  Value operator-(const Value &rhs) const { return Value{m_n - rhs.m_n}; }
  Value operator*(const Value &rhs) const { return Value{m_n * rhs.m_n}; }
  Value operator/(const Value &rhs) const;

  Value mod(const Value &rhs) const;
  Value pow(const Value &rhs) const { return Value{std::pow(m_n, rhs.m_n)}; }

  Value operator<<(const Value &rhs) const;
  Value operator>>(const Value &rhs) const;
  Value operator&(const Value &rhs) const { return Value{static_cast<double>(toInt() & rhs.toInt())}; }
  Value operator|(const Value &rhs) const { return Value{static_cast<double>(toInt() | rhs.toInt())}; }

  bool operator==(const Value &rhs) const { return m_n == rhs.m_n; }
  std::partial_ordering operator<=>(const Value &rhs) const { return m_n <=> rhs.m_n; }

  // fixedDecimals caps the fraction where the unit dictates its own precision,
  // as money does
  std::string render(NumberOutputFormat format = NumberOutputFormat::Decimal,
                     std::optional<int> fixedDecimals = std::nullopt, const NumberLocale &nloc = {}) const;

private:
  // digits below the radix point are dropped, as the bitwise operators always
  // have. a magnitude past the integer range has none to land on, so it saturates
  long long toInt() const {
    constexpr auto lo = std::numeric_limits<long long>::min();
    constexpr auto hi = std::numeric_limits<long long>::max();

    if (std::isnan(m_n)) return 0;
    if (m_n >= static_cast<double>(hi)) return hi;
    if (m_n <= static_cast<double>(lo)) return lo;

    return static_cast<long long>(m_n);
  }

  long long shiftCount(const Value &rhs) const;
  std::string renderDecimal(const NumberLocale &nloc) const;
  std::string renderBase(NumberOutputFormat format) const;

  double m_n = 0;
};

} // namespace numen
