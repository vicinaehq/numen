#include "value.hpp"
#include "unicode.hpp"
#include <algorithm>
#include <cctype>
#include <climits>
#include <format>
#include <locale>
#include <ranges>
#include <stdexcept>

namespace numen {

namespace {

constexpr int MAX_DECIMALS = 6;

// past this a long long cannot hold the value, so it is printed as a plain decimal
constexpr double LLONG_RANGE = 9223372036854775808.0;

constexpr double SCIENTIFIC_THRESHOLD = 1e40;

void stripTrailingZeroes(std::string &s) {
  auto last = s.find_last_not_of('0');
  s.resize(s[last] == '.' ? last : last + 1);
}

std::string groupDigits(std::string_view digits, const NumberLocale &nloc) {
  std::string out{digits};
  if (nloc.grouping.empty() || nloc.groupSep.empty()) return out;

  std::size_t pos = digits.size();
  std::size_t gi = 0;

  // separators land right to left, so earlier insert positions stay valid
  while (true) {
    const char g = nloc.grouping[std::min(gi++, nloc.grouping.size() - 1)];
    if (g <= 0 || g == CHAR_MAX || pos <= static_cast<std::size_t>(g)) break;
    pos -= static_cast<std::size_t>(g);
    out.insert(pos, nloc.groupSep);
  }

  return out;
}

// s is in the classic form: sign, integer digits, '.' fraction, exponent
std::string localize(std::string_view s, const NumberLocale &nloc) {
  const std::size_t sign = s.starts_with('-') ? 1 : 0;

  std::size_t end = sign;
  while (end < s.size() && std::isdigit(static_cast<unsigned char>(s[end]))) ++end;

  std::string out{s.substr(0, sign)};
  out += groupDigits(s.substr(sign, end - sign), nloc);

  if (end < s.size() && s[end] == '.') {
    out += nloc.decimalPoint;
    ++end;
  }

  out += s.substr(end);
  return out;
}

// the one base std::format has no specifier for on a long long
std::string toBinary(unsigned long long i) {
  if (i == 0) return "0";

  std::string out;
  for (; i > 0; i /= 2) {
    out += (i % 2 == 0) ? '0' : '1';
  }
  std::ranges::reverse(out);

  return out;
}

} // namespace

NumberLocale NumberLocale::from(const std::locale &loc) {
  const auto &np = std::use_facet<std::numpunct<wchar_t>>(loc);
  const auto utf8 = [](wchar_t c) {
    std::string out;
    appendUtf8(out, static_cast<uint32_t>(c));
    return out;
  };

  // unicode space variants (U+202F on glibc for fr) become a plain space
  const auto sep = static_cast<uint32_t>(np.thousands_sep());
  const bool spaceLike =
      sep == 0xA0 || (sep >= 0x2000 && sep <= 0x200A) || sep == 0x202F || sep == 0x205F;

  NumberLocale out;
  out.decimalPoint = utf8(np.decimal_point());
  out.groupSep = spaceLike ? " " : utf8(np.thousands_sep());

  // the C locale doesn't group; results always do
  if (auto g = np.grouping(); !g.empty()) out.grouping = g;

  return out;
}

Value Value::scaled(double mantissa, int scale) {
  if (scale == 0) return Value{mantissa};
  if (scale > 0) return Value{mantissa * std::pow(10.0, scale)};

  return Value{mantissa / std::pow(10.0, -scale)};
}

Value Value::operator/(const Value &rhs) const {
  if (rhs.isZero()) throw std::runtime_error("Division by zero");

  return Value{m_n / rhs.m_n};
}

Value Value::mod(const Value &rhs) const {
  if (rhs.isZero()) throw std::runtime_error("Modulo by zero");

  return Value{std::fmod(m_n, rhs.m_n)};
}

long long Value::shiftCount(const Value &rhs) const {
  auto by = rhs.toInt();
  if (by < 0 || by > 63) throw std::runtime_error("Shift count out of range");

  return by;
}

Value Value::operator<<(const Value &rhs) const {
  auto shifted = static_cast<unsigned long long>(toInt()) << shiftCount(rhs);
  return Value{static_cast<double>(static_cast<long long>(shifted))};
}

Value Value::operator>>(const Value &rhs) const {
  return Value{static_cast<double>(toInt() >> shiftCount(rhs))};
}

std::string Value::renderBase(NumberOutputFormat format) const {
  auto i = toInt();
  const bool negative = i < 0;
  // the sign comes off in unsigned, where the most negative value cannot overflow
  auto magnitude = static_cast<unsigned long long>(i);
  if (negative) magnitude = 0ULL - magnitude;

  auto [prefix, body] = [&]() -> std::pair<const char *, std::string> {
    switch (format) {
    case NumberOutputFormat::Hexadecimal:
      return {"0x", std::format("{:x}", magnitude)};
    case NumberOutputFormat::Octal:
      return {"0o", std::format("{:o}", magnitude)};
    default:
      return {"0b", toBinary(magnitude)};
    }
  }();

  return std::format("{}{}{}", negative ? "-" : "", prefix, body);
}

std::string Value::renderDecimal(const NumberLocale &nloc) const {
  if (std::isinf(m_n)) return m_n < 0 ? "-inf" : "inf";

  if (std::abs(m_n) >= SCIENTIFIC_THRESHOLD)
    return localize(std::format("{:.{}e}", m_n, MAX_DECIMALS), nloc);

  if (isInteger()) {
    return localize(std::abs(m_n) < LLONG_RANGE ? std::format("{}", static_cast<long long>(m_n))
                                                : std::format("{:.0f}", m_n),
                    nloc);
  }

  auto out = std::format("{:.{}f}", m_n, MAX_DECIMALS);

  // a non zero value rounding to all zeroes would read as an exact 0
  if (out.find_first_of("123456789") == std::string::npos) return localize(std::format("{:g}", m_n), nloc);

  stripTrailingZeroes(out);
  return localize(out, nloc);
}

std::string Value::render(NumberOutputFormat format, std::optional<int> fixedDecimals,
                          const NumberLocale &nloc) const {
  if (format != NumberOutputFormat::Decimal) return renderBase(format);
  if (!fixedDecimals) return renderDecimal(nloc);

  auto out = std::format("{:.{}f}", m_n, *fixedDecimals);
  if (*fixedDecimals > 0) stripTrailingZeroes(out);

  return localize(out, nloc);
}

} // namespace numen
