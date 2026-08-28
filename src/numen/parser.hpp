#pragma once
#include "numen/unit.hpp"
#include "numen/numen.hpp"
#include "concepts.hpp"
#include "lexer.hpp"
#include <cassert>
#include "date-string.hpp"
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

using namespace numen; // NOLINT(google-global-names-in-headers)

struct Expression;

using NumberString = numen::Value;

struct BinaryExpression {
  std::string op;
  std::unique_ptr<Expression> lhs;
  std::unique_ptr<Expression> rhs;
};

struct TimezoneOffset {
  std::string_view name;
  std::chrono::seconds offset = std::chrono::seconds(0);
};

struct NamedUnitTerm {
  std::string_view name;
  int exponent = 1;
};

struct NamedUnit {
  std::vector<NamedUnitTerm> terms;

  // only a single token can be resolved against a sibling
  bool isSimple() const { return terms.size() == 1 && terms.front().exponent == 1; }
  std::string_view simpleName() const { return terms.front().name; }
};

struct NamedNumberFormat {
  std::string_view name;
};

using ConversionTarget = std::variant<NamedUnit, TimezoneOffset, NamedNumberFormat>;

struct ConversionExpression {
  std::unique_ptr<Expression> lhs;

  // target can be many things, what is applicable
  // is only known when interpreting as we find out
  // what the type of lhs is.
  // For instance `pt` can be a unit or refer to pacific time,
  // the latter only makes sense if lhs is of a date time type.
  struct {
    std::optional<NamedUnit> unit;
    std::optional<TimezoneOffset> tz;
    std::optional<NamedNumberFormat> fmt;
  } target;
};

// now = "time" | "date" | "now"
// time_lit = <hour>[:<min>:<sec>][am|pm]
// date = [now|time_lit] [in <TZ>]

struct ParsedTime {
  std::optional<std::chrono::hours> hours;
  std::optional<std::chrono::minutes> minutes;
  std::optional<std::chrono::seconds> seconds;
};

struct DateTimeLiteral {
  std::optional<std::variant<std::chrono::day, std::chrono::weekday>> day;
  std::optional<std::chrono::month> month;
  std::optional<std::chrono::year> year;
  std::optional<ParsedTime> time;
};

template <typename T> struct Scanned {
  T data;
  std::size_t tokenCount = 0;
};

struct RelativeDateTimeLiteral {
  std::variant<Duration, std::chrono::weekday> delta;
  enum class Direction : std::uint8_t { Past, Future } direction;
  DateTimePrecision precision = DateTimePrecision::DateTime;

  // additional time override for the current day (after duration is added)
  std::optional<ParsedTime> time;
};

using DateTimeValue = std::variant<DateTimeLiteral, RelativeDateTimeLiteral, std::string_view>;

struct DateString {
  DateTimeValue value;
  std::optional<TimezoneOffset> timezone;
};

struct UnaryExpression {
  std::string_view op;
  std::unique_ptr<Expression> lhs;
};

struct PostfixExpression {
  std::unique_ptr<Expression> lhs;
  std::string_view op;
};

struct FunctionCall {
  std::string_view name;
  std::vector<std::unique_ptr<Expression>> args;
};

struct UnitExpression {
  NamedUnit unit;
  std::unique_ptr<Expression> expr;
};

struct PercentExpression {
  std::unique_ptr<Expression> expr;
};

struct Expression {
  std::variant<BinaryExpression, UnaryExpression, PostfixExpression, NumberString, DateString, UnitExpression,
               ConversionExpression, Duration, FunctionCall, PercentExpression>
      data;

  const BinaryExpression *asBinaryExpression() const { return as<BinaryExpression>(); }

  const UnaryExpression *asUnaryExpression() const { return as<UnaryExpression>(); }

  const ConversionExpression *asConversion() const { return as<ConversionExpression>(); }

  const FunctionCall *asFunction() const { return as<FunctionCall>(); }

  template <concepts::VariantAlternative<decltype(data)> T> T *as() { return std::get_if<T>(&data); }
  template <concepts::VariantAlternative<decltype(data)> T> const T *as() const {
    return std::get_if<T>(&data);
  }
  template <concepts::VariantAlternative<decltype(data)> T> bool is() const {
    return std::holds_alternative<T>(data);
  }

  template <concepts::VariantAlternative<decltype(data)> T> bool contains() const {
    return std::visit(
        [&](const auto &node) {
          using U = std::remove_cvref_t<decltype(node)>;
          if constexpr (std::is_same_v<T, U>) { return true; }
          if constexpr (std::is_same_v<U, UnaryExpression>) { return node.lhs->template contains<T>(); }
          if constexpr (std::is_same_v<U, BinaryExpression>) {
            return node.lhs->template contains<T>() || node.rhs->template contains<T>();
          }
          if constexpr (std::is_same_v<U, ConversionExpression>) { return node.lhs->template contains<T>(); }
          if constexpr (std::is_same_v<U, UnitExpression>) { return node.expr->template contains<T>(); }
          if constexpr (std::is_same_v<U, PercentExpression>) { return node.expr->template contains<T>(); }
          return false;
        },
        data);
  }
};

struct AST {
  std::unique_ptr<Expression> root;
};

class Parser {
public:
  Parser(std::string_view data, const UnitDatabase &unitDb, const ParseOptions &options);

  std::optional<NamedUnit> parseUnit(bool validate = true);
  std::optional<NamedUnit> parseConversionTarget();
  std::optional<int> parseUnitExponent();

  bool isTimezoneToken(std::string_view name);
  std::optional<DateTimeLiteral> parseYYYYMMDD();
  std::optional<DateTimeLiteral> parseNaturalDateLiteral();
  std::optional<DateTimeValue> parseDate();
  std::optional<ParsedTime> parseTime(bool afterDate = false);
  std::optional<TimezoneOffset> parseTimezone();
  std::optional<NamedNumberFormat> parseNumberFormat();
  std::optional<RelativeDateTimeLiteral> parseRelativeDateTimeLiteral();
  std::optional<DateString> parseRFC3339();

  std::optional<std::chrono::seconds> parseTimezoneOffset();

  template <typename F> std::optional<std::string_view> greedyParse(int n, F fn) {
    assert(n >= 0);
    for (int i = 0; i != n; ++i) {
      auto str = m_lexer.peakString(n - i);
      if (str && fn(*str)) {
        const int consumable = n - i;
        for (int j = 0; j != consumable; ++j)
          m_lexer.next();
        return str;
      }
    }
    return std::nullopt;
  }

  AST parse();

protected:
  bool isPostfixPercent(const Lexer::Token &pct);
  std::optional<Scanned<numen::Duration>> scanDuration();
  std::unique_ptr<Expression> parseTerm();
  std::unique_ptr<Expression> pratParse(int minPrec = 0);
  std::unique_ptr<Expression> parseMul() { return pratParse(4); }
  std::optional<numen::Value> parseNumber();
  bool isFunctionParameterSeparator(std::string_view tok) const;

private:
  std::locale m_locale;
  const std::numpunct<char> &m_numpunct;
  Lexer m_lexer;
  DateStringVocab m_dateStringVocab;
  const UnitDatabase &m_unitDb;
  ParseOptions m_opts;

  // flags
  bool m_inFunction = false;
};
