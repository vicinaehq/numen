#include "parser.hpp"
#include "numen/numen.hpp"
#include "numen/unit.hpp"
#include "timezone.hpp"
#include <iostream>
#include <ranges>
#include "utils.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <locale>
#include <sstream>

struct OperatorDefinition {
  std::string_view id;
  std::vector<std::string_view> aliases;
  int precedence;
  bool rightAssociative = false;
};

constexpr int EXPONENT_PRECEDENCE = 5;

const auto &operators() {
  // clang-format off
  const static auto OPERATORS = std::to_array<OperatorDefinition>({
     OperatorDefinition{.id = "==", .aliases = {"=="}, .precedence = 1},
     OperatorDefinition{.id = "!=", .aliases = {"!="}, .precedence = 1},
     OperatorDefinition{.id = ">", .aliases = {">"}, .precedence = 1},
     OperatorDefinition{.id = ">=", .aliases = {">="}, .precedence = 1},
     OperatorDefinition{.id = "<", .aliases = {"<"}, .precedence = 1},
     OperatorDefinition{.id = "<=", .aliases = {"<="}, .precedence = 1},

     OperatorDefinition{.id = ">>", .aliases = {">>"}, .precedence = 2},
     OperatorDefinition{.id = ">>", .aliases = {">>"}, .precedence = 2},
     OperatorDefinition{.id = "<<", .aliases = {"<<"}, .precedence = 2},
     OperatorDefinition{.id = "|", .aliases = {"|"}, .precedence = 2},
     OperatorDefinition{.id = "&", .aliases = {"&"}, .precedence = 2},
     OperatorDefinition{.id = "+", .aliases = {"+", "add", "plus"}, .precedence = 3},
     OperatorDefinition{.id = "-", .aliases = {"-", "minus"}, .precedence = 3},
     OperatorDefinition{.id = "*", .aliases = {"*", "mul"}, .precedence = 4},
     OperatorDefinition{.id = "/", .aliases = {"/", "div"}, .precedence = 4},
     OperatorDefinition{.id = "%", .aliases = {"%", "mod", "modulo"}, .precedence = 4},
     OperatorDefinition{.id = "^", .aliases = {"^", "**", "pow", "power"}, .precedence = EXPONENT_PRECEDENCE,
                        .rightAssociative = true}
  });
  // clang-format on

  return OPERATORS;
}

struct ConstantDef {
  std::string_view name;
  double n;
  bool caseSensitive = false;
};

// clang-format off
constexpr auto CONSTANTS = std::to_array<ConstantDef>({
		{"pi", std::numbers::pi},
		{"e", std::numbers::e, false},
		{"phi", std::numbers::phi},
});
// clang-format on

namespace {

struct ParsedDouble {
  double value = 0;
  bool whole = false;
};

// we avoid using from_chars because some of the overloads require very recent toolchains (most notably on
// macOS)
ParsedDouble parseDouble(std::string_view s) {
  ParsedDouble out;
  std::istringstream in{std::string{s}};
  in.imbue(std::locale::classic());
  in >> out.value;
  out.whole = !in.fail() && in.peek() == std::char_traits<char>::eof();
  return out;
}

bool isInIgnoreCase(std::string_view s, auto &&range) {
  return std::ranges::any_of(range, [&](auto &&v) { return equalsIgnoreCase(s, std::string_view{v}); });
}

bool isMeridiemMarker(std::string_view s) { return isInIgnoreCase(s, std::initializer_list{"am", "pm"}); }
bool isOrdinalSuffix(std::string_view s) {
  return isInIgnoreCase(s, std::initializer_list{"st", "rd", "nd", "th"});
}

constexpr bool isConstant(std::string_view v) {
  return std::ranges::any_of(CONSTANTS, [&](auto &&def) { return equalsIgnoreCase(def.name, v); });
}

bool isOperatorToken(std::string_view tok) {
  return std::ranges::any_of(operators(), [&](auto &&op) { return std::ranges::contains(op.aliases, tok); });
}

std::optional<double> parseConstant(std::string_view tok) {
  auto it = std::ranges::find_if(CONSTANTS, [&](const ConstantDef &def) {
    return def.caseSensitive ? (tok == def.name) : equalsIgnoreCase(def.name, tok);
  });

  if (it == CONSTANTS.end()) return std::nullopt;
  return it->n;
}

constexpr std::string_view NBSP = "\u00a0";
constexpr std::string_view FIGURE_SPACE = "\u2007";
constexpr std::string_view THIN_SPACE = "\u2009";
constexpr std::string_view NARROW_NBSP = "\u202f";

// what formatted text separates thousands with
constexpr auto UNICODE_SPACE_SEPS = std::to_array({NBSP, FIGURE_SPACE, THIN_SPACE, NARROW_NBSP});

std::unique_ptr<Expression> makeNumberExpr(numen::Value n) {
  return std::make_unique<Expression>(NumberString{n});
}

std::unique_ptr<Expression> makeBinExpr(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs,
                                        const std::string &op) {
  auto expr = std::make_unique<Expression>();

  return std::make_unique<Expression>(
      BinaryExpression{.op = op, .lhs = std::move(lhs), .rhs = std::move(rhs)});
}

std::unique_ptr<Expression> makeUnit(std::unique_ptr<Expression> inner, const NamedUnit &unit) {
  return std::make_unique<Expression>(UnitExpression{
      .unit = unit,
      .expr = std::move(inner),
  });
}

// a clock component only reads as one inside its own range, so anything else is
// not a time and must not be narrowed into one
std::optional<unsigned> clockComponent(double value, unsigned limit) {
  if (value < 0 || value > limit || value != std::trunc(value)) return std::nullopt;
  return static_cast<unsigned>(value);
}

std::optional<std::chrono::year> asYear(double value) {
  // FIXME: maybe do not make this super arbitrary
  constexpr int minAllowed = 1000;
  constexpr int lowest = static_cast<int>(std::chrono::year::min());
  constexpr int highest = static_cast<int>(std::chrono::year::max());

  if (value >= minAllowed && value >= lowest && value <= highest) {
    return std::chrono::year{static_cast<int>(value)};
  }

  return std::nullopt;
}
} // namespace

Parser::Parser(std::string_view data, const UnitDatabase &unitDb, const ParseOptions &opts)
    : m_locale(opts.effectiveLocale()), m_numpunct(std::use_facet<std::numpunct<char>>(m_locale)),
      m_lexer(data, m_numpunct), m_unitDb(unitDb), m_opts(opts) {}

AST Parser::parse() {
  AST ast;
  ast.root = pratParse();
  return ast;
}

bool Parser::isTimezoneToken(std::string_view name) {
  // TODO: do something thorough
  return TimezoneDB{}.query(name);
}

std::optional<NamedUnit> Parser::parseUnit(bool validate) {
  auto head = m_lexer.peak();
  auto start = m_lexer.cursor();

  if (!head) return std::nullopt;

  // constants always have priority over currency symbol
  const bool isNotUnitLike = m_dateStringVocab.asMonth(head->raw) || m_dateStringVocab.asWeekday(head->raw) ||
                             isConstant(head->raw) || m_unitDb.findCompounds(head->raw).empty();

  if ((validate || head->type != Lexer::TokenType::String) && isNotUnitLike) { return std::nullopt; }

  m_lexer.next();
  NamedUnit target{.terms = {NamedUnitTerm{.name = head->raw}}};

  // shorthand squaring/cubing. We don't support other exponents are they are not typically
  // used as shorthand.
  if (auto num = m_lexer.peakAs<Lexer::Number>(); num && num->n >= 2 && num->n <= 3) {
    bool qualifies = true;

    // the thousand shorthand 'k' should not directly follow the '2' or '3' otherwise we can't interpret it as
    // an exponent
    if (auto next = m_lexer.peak(1); next && equalsIgnoreCase(next->raw, "k")) { qualifies = false; }

    if (qualifies) {
      target.terms.front().exponent = num->n.to<int>();
      m_lexer.next();
    }
  }

  if (auto tok = m_lexer.peak(); tok && tok->raw == "(") {
    m_lexer.setCursor(start);
    return std::nullopt;
  }

  return target;
}

// "^2" right after a unit name, as typed in a target where no operator applies
std::optional<int> Parser::parseUnitExponent() {
  auto op = m_lexer.peak();
  if (!op || op->raw != "^") return std::nullopt;

  auto tok = m_lexer.peak(1);
  if (!tok || !std::holds_alternative<Lexer::Number>(tok->data)) return std::nullopt;

  auto n = std::get<Lexer::Number>(tok->data).n;
  if (!n.isInteger()) return std::nullopt;

  m_lexer.next();
  m_lexer.next();
  return n.to<int>();
}

// only a real unit token may follow the slash, so "1 km to m / 2" still divides
// the result rather than naming metres per two
std::optional<NamedUnit> Parser::parseConversionTarget() {
  auto target = parseUnit(false); // FIXME: we should not need to disable validation here

  if (!target) return std::nullopt;

  if (target->terms.front().exponent == 1) {
    target->terms.front().exponent = parseUnitExponent().value_or(1);
  }

  while (auto op = m_lexer.peak()) {
    if (op->raw != "/" && op->raw != "*") break;

    auto next = m_lexer.peak(1);
    if (!next || next->type != Lexer::TokenType::String) break;
    if (m_unitDb.findCompounds(next->raw).empty()) break;

    m_lexer.next();
    m_lexer.next();
    const int sign = op->raw == "/" ? -1 : 1;
    const int exponent = parseUnitExponent().value_or(1);
    target->terms.push_back(NamedUnitTerm{.name = next->raw, .exponent = sign * exponent});
  }

  return target;
}

// Jan 18 2021
// 18 Jan 2021
// 18 Jan
// Jan 18
// a month can never appear in its numeric form here (very convenient)
// <weekday_name | day_of_month> <month_name> <year> => 18 Jan 2024
// <month_name> <weekday_name | day_of_month> <year> => Jan 18 2024
std::optional<DateTimeLiteral> Parser::parseNaturalDateLiteral() {
  std::optional<std::chrono::weekday> weekday;
  std::optional<std::chrono::day> day;
  std::optional<std::chrono::month> month;
  std::optional<std::chrono::year> year;
  int i = 0;
  int extraAdvance = 0;

  const auto skipFiller = [&]() {
    ++i;
    ++extraAdvance;
  };

  while (i < 3 + extraAdvance) {
    auto tok = m_lexer.peak(i);
    if (!tok) break;

    if (tok->raw == "," || equalsIgnoreCase(tok->raw, "the")) {
      skipFiller();
      continue;
    } else if (auto it = std::get_if<Lexer::Number>(&tok->data)) {
      auto value = it->n.toDouble();

      if (auto next = m_lexer.peak(i + 1); next && isMeridiemMarker(next->raw)) break;

      if (auto dayOfMonth = clockComponent(value, 31); (!day && !weekday) && dayOfMonth && value >= 1) {
        if (auto next = m_lexer.peak(i + 1); next && isOrdinalSuffix(next->raw)) skipFiller();

        // 3 of January, 3rd of January...
        if (auto next = m_lexer.peak(i + 1); next && next->raw == "of") skipFiller();

        day = std::chrono::day{*dayOfMonth};
      } else if (auto parsed = asYear(value)) {
        year = *parsed;
      } else {
        break;
      }
    } else if (auto m = m_dateStringVocab.asMonth(tok->raw)) {
      month = *m;
    } else if (auto week = m_dateStringVocab.asWeekday(tok->raw)) {
      weekday = *week;
    } else {
      break;
    }

    ++i;
  }

  const bool hasDay = weekday || day;

  if (month && year && !hasDay) { day = std::chrono::day{1}; }

  // the month is what makes this a date at all: without it "0 9" and "2001 5"
  // are just two numbers that happen to sit next to each other
  if (month && (hasDay || year)) {
    DateTimeLiteral lit;

    lit.month = month;
    lit.year = year;

    if (weekday) lit.day = weekday;
    if (day) lit.day = day;

    m_lexer.advance(i);

    return lit;
  }

  return std::nullopt;
}

// parse YYYY/MM/DD or MM/DD/YYYY or DD/MM/YYYY
// requires that the '/' separators are strictly adjacent
// to the numbers (no spaces) in order to be considered a date literal
std::optional<DateTimeLiteral> Parser::parseYYYYMMDD() {
  auto ns1 = m_lexer.peak(0);
  constexpr auto isDateDelim = [](auto c) { return c == "/" || c == "-"; };

  if (!ns1 || ns1->type != Lexer::TokenType::Number) { return std::nullopt; }

  if (auto tok = m_lexer.peak(1); !tok || !ns1->isAdjacent(*tok) || !isDateDelim(tok->raw)) {
    return std::nullopt;
  }

  auto ns2 = m_lexer.peak(2);

  if (!ns2 || ns2->type != Lexer::TokenType::Number) { return std::nullopt; }

  if (auto tok = m_lexer.peak(3); !tok || !ns2->isAdjacent(*tok) || !isDateDelim(tok->raw)) {
    return std::nullopt;
  }

  auto ns3 = m_lexer.peak(4);

  if (!ns3 || ns3->type != Lexer::TokenType::Number) { return std::nullopt; }

  auto [n1, n2, n3] =
      std::tuple{ns1->asNumber()->n.toDouble(), ns2->asNumber()->n.toDouble(), ns3->asNumber()->n.toDouble()};
  auto ns = std::initializer_list{n1, n2, n3};
  auto strs = std::initializer_list{ns1->raw, ns2->raw, ns3->raw};

  for (const auto &[n, raw] : std::views::zip(ns, strs)) {
    if (n < 0) return std::nullopt;
    if (n <= 9 && !raw.starts_with('0'))
      return std::nullopt; // avoid conflict with regular arithemetic, e.g '2024-1-18'
  }

  const auto isMonth = [](auto n) { return n >= 1 && n <= 12; };
  const auto isDay = [](auto n) { return n >= 1 && n <= 31; };
  const auto isYear = [](auto n) { return n >= 0 && n <= 2100; };
  const auto commit = [&](DateTimeLiteral lit) {
    for (int i = 0; i != 5; ++i) {
      m_lexer.next();
    }
    return lit;
  };

  DateTimeLiteral d;

  // YYYY/MM/DD
  if (isYear(n1) && isMonth(n2) && isDay(n3)) {
    d.year = std::chrono::year(static_cast<int>(n1));
    d.month = std::chrono::month(static_cast<unsigned>(n2));
    d.day = std::chrono::day(static_cast<unsigned>(n3));
    return commit(d);
  }

  // DD/MM/YYYY
  if (isDay(n1) && isMonth(n2) && isYear(n3)) {
    d.day = std::chrono::day(static_cast<unsigned>(n1));
    d.month = std::chrono::month(static_cast<unsigned>(n2));
    d.year = std::chrono::year(static_cast<int>(n3));
    return commit(d);
  }

  // MM/DD/YYYY
  if (isMonth(n1) && isDay(n2) && isYear(n3)) {
    d.month = std::chrono::month(static_cast<unsigned>(n1));
    d.day = std::chrono::day(static_cast<unsigned>(n2));
    d.year = std::chrono::year(static_cast<int>(n3));
    return commit(d);
  }

  return std::nullopt;
}

constexpr auto RESERVED_TIME_TOKENS =
    std::to_array<std::string_view>({"tomorrow", "yesterday", "tomorrow", "now", "time", "date", "ago"});

std::optional<RelativeDateTimeLiteral> Parser::parseRelativeDateTimeLiteral() {
  if (auto duration = scanDuration()) {
    if (auto s = m_lexer.peak(static_cast<int>(duration->tokenCount));
        s && s->type == Lexer::TokenType::String) {
      auto word = s->raw;
      if (equalsIgnoreCase(word, std::string_view{"ago"})) {
        m_lexer.advance(static_cast<int>(duration->tokenCount) + 1);
        return RelativeDateTimeLiteral{
            .delta = duration->data,
            .direction = RelativeDateTimeLiteral::Direction::Past,
        };
      }
    }
  }

  if (auto s = m_lexer.peakAs<Lexer::String>()) {
    if (s->data == "yesterday") {
      m_lexer.next();

      return RelativeDateTimeLiteral{
          .delta = Duration{.seconds = std::chrono::days{1}},
          .direction = RelativeDateTimeLiteral::Direction::Past,
          .precision = DateTimePrecision::Date,
      };
    }

    if (s->data == "tomorrow") {
      m_lexer.next();

      return RelativeDateTimeLiteral{
          .delta = Duration{.seconds = std::chrono::days{1}},
          .direction = RelativeDateTimeLiteral::Direction::Future,
          .precision = DateTimePrecision::Date,
      };
    }

    if (s->data == "today" || s->data == "date") {
      m_lexer.next();
      return RelativeDateTimeLiteral{.precision = DateTimePrecision::Date};
    }

    if (s->data == "now" || s->data == "time") {
      m_lexer.next();
      return RelativeDateTimeLiteral{.precision = DateTimePrecision::DateTime};
    }
  }

  return std::nullopt;
}

std::optional<DateTimeValue> Parser::parseDate() {
  auto time = parseTime();

  if (auto d = parseRelativeDateTimeLiteral()) {
    d->time = parseTime(true);
    if (!d->time) d->time = time;
    return d;
  }

  if (auto d = parseYYYYMMDD()) {
    d->time = parseTime(true);
    if (!d->time) d->time = time;
    return d;
  }

  if (auto d = parseNaturalDateLiteral()) {
    d->time = parseTime(true);
    if (!d->time) d->time = time;
    return d;
  }

  if (time) return DateTimeLiteral{.time = time};

  return std::nullopt;
}

std::optional<ParsedTime> Parser::parseTime(bool afterDate) {
  auto reset = m_lexer.checkpoint();

  if (auto tok = m_lexer.peakAs<Lexer::String>(); tok && tok->data == "at") { m_lexer.next(); }

  ParsedTime time;

  auto head = m_lexer.peakIf(Lexer::TokenType::Number);

  if (!head) return std::nullopt;

  if (head) {
    auto value = clockComponent(head->asNumber()->n.toDouble(), 23);
    if (!value) return std::nullopt;
    time.hours = std::chrono::hours{*value};
  }

  const auto commitTime = [&]() -> std::optional<ParsedTime> {
    if (auto tok = m_lexer.peakAs<Lexer::String>()) {
      const bool am = equalsIgnoreCase(tok->data, "am");
      const bool pm = equalsIgnoreCase(tok->data, "pm");
      const bool is12Hour = am || pm;

      if (is12Hour) {
        if (time.hours->count() > 12) {
          reset();
          return std::nullopt;
        }
        m_lexer.next();
        if (pm) time.hours = time.hours.value_or(std::chrono::hours{0}) + std::chrono::hours{12};
      }
    }
    return time;
  };

  using IV = std::initializer_list<std::string_view>;

  auto sep = m_lexer.peak(1);

  if (!sep) return std::nullopt;

  if (!std::ranges::contains(IV{":", "h"}, sep->raw)) {
    if (afterDate) {
      m_lexer.next();
      return commitTime();
    }
    if (equalsIgnoreCase(sep->raw, "pm") || equalsIgnoreCase(sep->raw, "am")) {
      m_lexer.next();
      return commitTime();
    }
    return std::nullopt;
  }

  // "h" also names the hour unit, so it only separates a clock when glued
  if (sep->raw == "h") {
    if (!head || !head->isAdjacent(*sep)) return std::nullopt;

    // on its own "12h" is twelve hours far more often than noon, so it needs
    // its minutes. after a date there is nothing else it could mean
    if (!afterDate) {
      auto mins = m_lexer.peak(2);
      if (!mins || mins->type != Lexer::TokenType::Number || !sep->isAdjacent(*mins)) return std::nullopt;
    }
  }

  m_lexer.next();
  m_lexer.next();

  if (auto tok = m_lexer.peakIf(Lexer::TokenType::Number)) {
    auto value = clockComponent(tok->asNumber()->n.toDouble(), 59);
    if (!value) return std::nullopt;

    time.minutes = std::chrono::minutes{*value};
    m_lexer.next();
  }

  if (auto tok = m_lexer.peak(); !tok || tok->raw != ":") {
    if (tok && tok->raw == "min") {
      reset();
      return std::nullopt;
    }
    return commitTime();
  }
  m_lexer.next();

  if (auto tok = m_lexer.peakIf(Lexer::TokenType::Number)) {
    auto value = clockComponent(tok->asNumber()->n.toDouble(), 59);
    if (!value) return std::nullopt;

    time.seconds = std::chrono::seconds{*value};
    m_lexer.next();
  }

  return commitTime();
}

std::optional<DateString> Parser::parseRFC3339() {
  // 2026-08-17T14:30:00Z
  using L = Lexer;
  auto parsed = m_lexer.peakForward<L::Number, L::Operator, L::Number, L::Operator, L::Number, L::String,
                                    L::Number, L::Operator, L::Number, L::Operator, L::Number>();

  if (!parsed) return std::nullopt;

  auto &[year, s1, month, s2, day, tsep, hour, s3, min, s4, s] = *parsed;

  if (!(s1.op == "-" && s2.op == "-" && s3.op == ":" && s4.op == ":")) return std::nullopt;

  // the shape alone does not make it a timestamp: every component has to be
  // one a clock could read, or the cast below has nothing to land on
  auto yr = asYear(year.n.toDouble());
  auto mon = clockComponent(month.n.toDouble(), 12);
  auto dayOfMonth = clockComponent(day.n.toDouble(), 31);
  auto hours = clockComponent(hour.n.toDouble(), 23);
  auto minutes = clockComponent(min.n.toDouble(), 59);
  // :60 is a leap second, which is a reading RFC 3339 allows
  auto seconds = clockComponent(s.n.toDouble(), 60);

  if (!yr || !mon || !dayOfMonth || !hours || !minutes || !seconds) return std::nullopt;
  if (*mon < 1 || *dayOfMonth < 1) return std::nullopt;

  DateString ds;

  ds.value = DateTimeLiteral{.day = std::chrono::day{*dayOfMonth},
                             .month = std::chrono::month{*mon},
                             .year = yr,
                             .time = ParsedTime{
                                 .hours = std::chrono::hours{*hours},
                                 .minutes = std::chrono::minutes{*minutes},
                                 .seconds = std::chrono::seconds{*seconds},
                             }};

  m_lexer.advance(std::tuple_size_v<decltype(parsed)::value_type>);

  if (auto zulu = m_lexer.peakAs<Lexer::String>(); zulu && zulu->data == "Z") {
    m_lexer.next();
    ds.timezone = TimezoneOffset{.name = "UTC"};
    return ds;
  } else if (auto offset = parseTimezoneOffset()) {
    ds.timezone = TimezoneOffset{.name = "UTC", .offset = *offset};
    return ds;
  }

  return std::nullopt;
}

std::optional<std::chrono::seconds> Parser::parseTimezoneOffset() {
  constexpr auto isValidOffset = [](auto offset) { return offset >= 0 && offset <= 23; };
  std::chrono::seconds offset{0};

  if (auto str = m_lexer.peakIf(Lexer::TokenType::Operator)) {
    if (str->raw == "+" || str->raw == "-") {
      const int sign = str->raw == "+" ? 1 : -1;
      m_lexer.next();

      if (auto n = m_lexer.peakAs<Lexer::Number>(); n && n->n.isInteger() && isValidOffset(n->n)) {
        m_lexer.next();
        offset += std::chrono::hours(static_cast<int>(n->n.toDouble()) * sign);

        if (auto tok = m_lexer.peak(); tok && tok->raw == ":") {
          m_lexer.next();
          if (auto mins = m_lexer.peakAs<Lexer::Number>();
              mins && mins->n.isInteger() && mins->n >= 0 && mins->n < 60) {
            m_lexer.next();
            offset += std::chrono::minutes(static_cast<int>(mins->n.toDouble()) * sign);
          }
        }
      }
    }
    return offset;
  };
  return std::nullopt;
}

std::optional<TimezoneOffset> Parser::parseTimezone() {
  if (auto str = m_lexer.peakIf(Lexer::TokenType::String)) {
    const bool isCalendarWord = m_dateStringVocab.asMonth(str->raw) || m_dateStringVocab.asWeekday(str->raw);

    if (isCalendarWord || std::ranges::contains(RESERVED_TIME_TOKENS, str->raw)) return std::nullopt;

    auto isOffsettableTz = std::ranges::any_of(std::initializer_list<std::string_view>({"gmt", "utc"}),
                                               [&](auto &&s) { return equalsIgnoreCase(s, str->raw); });

    if (isOffsettableTz) {
      m_lexer.next();
      return TimezoneOffset{.name = str->raw,
                            .offset = parseTimezoneOffset().value_or(std::chrono::seconds{0})};
    }
  }

  return greedyParse(4, [&](std::string_view word) { return isTimezoneToken(word); })
      .transform([](auto &&str) { return TimezoneOffset(str); });
}

std::optional<NamedNumberFormat> Parser::parseNumberFormat() {
  return greedyParse(3,
                     [&](std::string_view word) {
                       return std::ranges::contains(std::initializer_list{"hex", "octal", "binary",
                                                                          "hexadecimal", "bin", "dec",
                                                                          "decimal"},
                                                    word);
                     })
      .transform([](auto &&str) { return NamedNumberFormat(str); });
}

std::optional<numen::Value> Parser::parseNumber() {
  constexpr auto CONTEXT_AWARE_THOUSAND_SEP = ",";
  std::string ns;
  size_t count = 0;

  const auto joinableNumber = [&](const Lexer::Token &tok) {
    return tok.type == Lexer::TokenType::Number && std::ranges::all_of(tok.raw, [&](char c) {
             return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.' ||
                    c == m_numpunct.decimal_point();
           });
  };

  // space separated number should be glued together so that "150 000" parsed as "150,000"
  const auto spacedNumberFollows = [&](const Lexer::Token &prev) {
    const auto isDigit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    if (!std::ranges::all_of(prev.raw, isDigit)) return false;

    auto tok = m_lexer.peak();
    if (!tok) return false;
    if (joinableNumber(*tok)) return true;

    if (tok->type == Lexer::TokenType::String && std::ranges::contains(UNICODE_SPACE_SEPS, tok->raw)) {
      if (auto next = m_lexer.peak(1); next && joinableNumber(*next)) {
        m_lexer.next();
        return true;
      }
    }

    return false;
  };

  while (true) {
    auto n = m_lexer.peak();
    if (!n || n->type != Lexer::TokenType::Number) break;
    auto &nb = std::get<Lexer::Number>(n->data);

    ns += n->raw;
    m_lexer.next();

    if (!parseDouble(n->raw).whole && count == 0) { return nb.n; }

    if (spacedNumberFollows(*n)) {
      ++count;
      continue;
    }

    if (auto tok = m_lexer.peak(); m_inFunction || !tok || tok->raw != CONTEXT_AWARE_THOUSAND_SEP) {
      if (count == 0) return nb.n; // do not bother stringifying, there is only one part so pass the number
      break;
    };

    ++count;
    m_lexer.next();
  }

  if (ns.empty()) return std::nullopt;

  // the fraction may carry the locale's delimiter; parseDouble reads the classic one
  std::ranges::replace(ns, m_numpunct.decimal_point(), '.');
  return parseDouble(ns).value;
}

std::unique_ptr<Expression> Parser::parseTerm() {
  if (!m_lexer.peak()) {
    throw std::runtime_error("Expected EOF, looks like there is nothing we can parse!");
  }

  auto expr = std::unique_ptr<Expression>();
  auto frontUnit = parseUnit();

  if (auto tok = m_lexer.peak()) {

    if (auto constant = parseConstant(tok->raw)) {
      m_lexer.next();

      auto lhs = makeNumberExpr(numen::Value{*constant});

      // pi2 = pi * 2
      if (auto n = m_lexer.peak(); n && !isOperatorToken(n->raw) && n->raw != ")") {
        return makeBinExpr(std::move(lhs), parseMul(), "*");
      }

      return lhs;
    }

    if (auto date = parseRFC3339()) { return std::make_unique<Expression>(*date); }

    constexpr auto commitDate = [](DateString ds) {
      if (ds.timezone && std::holds_alternative<RelativeDateTimeLiteral>(ds.value)) {
        auto inner = std::make_unique<Expression>(ds);
        ConversionExpression conv{.lhs = std::move(inner)};
        conv.target.tz = *ds.timezone;
        return std::make_unique<Expression>(std::move(conv));
      }
      return std::make_unique<Expression>(ds);
    };

    if (auto tz = parseTimezone()) {
      if (auto date = parseDate()) {
        const DateString ds{.value = *date, .timezone = tz};
        return commitDate(ds);
      }
    }

    if (auto date = parseDate()) {
      DateString ds{.value = *date};

      if (auto result = parseTimezone()) { ds.timezone = result.value(); }

      return commitDate(ds);
    }

    // less than 2 tokens means likely unit
    if (auto duration = scanDuration(); duration && duration->tokenCount > 2) {
      for (std::size_t i = 0; i != duration->tokenCount; ++i) {
        m_lexer.next();
      }
      return std::make_unique<Expression>(duration->data);
    }
  }

  if (auto tok = m_lexer.peak()) {
    constexpr auto isSign = [](const auto &t) { return t.raw == "+" || t.raw == "-"; };
    if (!frontUnit && isSign(*tok)) {
      // optimization: collapse large run of signs instead of accumulation unary expressions
      bool negative = false;
      for (auto sign = m_lexer.peak(); sign && isSign(*sign); sign = m_lexer.peak()) {
        negative ^= sign->raw == "-";
        m_lexer.next();
      }
      auto operand = pratParse(EXPONENT_PRECEDENCE);
      if (!negative) return operand;
      return std::make_unique<Expression>(UnaryExpression{.op = "-", .lhs = std::move(operand)});
    }
  }

  if (auto open = m_lexer.peak(); open && open->raw == "(") {
    m_lexer.next();
    expr = pratParse();

    if (auto close = m_lexer.peak(); !close || close->raw != ")") {
      throw std::runtime_error("Expected ) to close the group");
    }
    m_lexer.next();

    // TODO: refactorize so that there is only one entrypoint for this
    if (auto tok = m_lexer.peak(); tok && equalsIgnoreCase(tok->raw, "k") && expr) {
      expr = std::make_unique<Expression>(PostfixExpression{.lhs = std::move(expr), .op = "k"});
      m_lexer.next();
    }

    if (auto tok = m_lexer.peak();
        tok && tok->type != Lexer::TokenType::Operator && tok->raw != "to" && tok->raw != "in") {
      if (auto unit = parseUnit()) {
        if (!expr)
          expr = makeUnit(makeNumberExpr(1), unit.value());
        else
          expr = makeUnit(std::move(expr), unit.value());
      } else {
        // implict multiplication, e.g "(150 * 2)4
        expr = makeBinExpr(std::move(expr), parseMul(), "*");
      }
    }
  }

  if (auto tok = m_lexer.peak()) {
    if (auto n = parseNumber()) {
      expr = makeNumberExpr(*n);
    } else if (auto name = m_lexer.peakIf(Lexer::TokenType::String)) {
      if (auto next = m_lexer.peak(1); next && next->raw == "(") {
        m_lexer.next();
        m_lexer.next();

        FunctionCall fn{.name = name->raw};
        constexpr auto unterminated = "Expected ) to close the argument list";

        m_inFunction = true;

        while (true) {
          if (m_lexer.peakOrThrow(unterminated).raw == ")") break;

          fn.args.emplace_back(pratParse());

          auto sep = m_lexer.peakOrThrow(unterminated);
          if (sep.raw == ")") { break; }
          if (!isFunctionParameterSeparator(sep.raw)) {
            throw std::runtime_error("Expected , to add another argument");
          }

          m_lexer.next();
        }

        m_inFunction = false;

        m_lexer.next();
        // falls through to the unit check so that "sqrt(4) km" carries a unit
        expr = std::make_unique<Expression>(std::move(fn));
      }
    }
  }

  if (auto tok = m_lexer.peak(); tok && equalsIgnoreCase(tok->raw, "k") && expr) {
    expr = std::make_unique<Expression>(PostfixExpression{.lhs = std::move(expr), .op = "k"});
    m_lexer.next();
  }

  // unit can be found after any term.
  if (auto unit = parseUnit()) {
    if (!expr)
      expr = makeUnit(makeNumberExpr(1), unit.value());
    else
      expr = makeUnit(std::move(expr), unit.value());
  }

  if (frontUnit) {
    if (!expr)
      expr = makeUnit(makeNumberExpr(1), frontUnit.value());
    else
      expr = makeUnit(std::move(expr), frontUnit.value());
  }

  if (!expr) {
    m_lexer.next();
    return parseTerm();
  }

  return expr;
}

std::optional<Scanned<Duration>> Parser::scanDuration() {
  // 1 year 2 weeks 2 minutes
  Scanned<Duration> d;

  while (true) {
    if (auto pk = m_lexer.peakForward<Lexer::Number, Lexer::String>(d.tokenCount)) {
      auto &[n, u] = *pk;
      if (auto unit = m_unitDb.findUnit(std::string{u.data});
          unit && unit->dimension == dimensions::DURATION) {

        auto part = numen::durationFrom(n.n.toDouble(), *unit);
        if (!part) break;

        d.tokenCount += 2;
        d.data = d.data + *part;
      } else {
        break;
      }
    } else {
      break;
    }
  }

  if (d.tokenCount) { return d; }

  return std::nullopt;
}

// "7 % -3" and "10% - 3" are the same token sequence, only spacing tells them apart
bool Parser::isPostfixPercent(const Lexer::Token &pct) {
  auto next = m_lexer.peak(1);

  if (!next) return true;
  if (next->raw == "+" || next->raw == "-") return m_lexer.isGluedLeft(pct);
  if (next->type == Lexer::TokenType::Number || next->raw == "(") return false;

  return next->type != Lexer::TokenType::String || !parseConstant(next->raw);
}

std::unique_ptr<Expression> Parser::pratParse(int minPrec) {
  auto left = parseTerm();

  while (auto tok = m_lexer.peak()) {
    if (tok->raw == "to" || tok->raw == "in" || tok->raw == "->") {
      if (minPrec > 0) break;

      m_lexer.next();

      // in is equivalent to addition for durations.
      // now in 2 days 5minutes
      if (auto duration = scanDuration(); duration && tok->raw == "in") {
        auto rhs = std::make_unique<Expression>(duration->data);
        left = std::make_unique<Expression>(BinaryExpression{
            .op = "+",
            .lhs = std::move(left),
            .rhs = std::move(rhs),
        });
        m_lexer.advance(static_cast<int>(duration->tokenCount));
        continue;
      }

      ConversionExpression expr{.lhs = std::move(left)};

      auto cursor = m_lexer.cursor();
      size_t maxCursor = 0;

      const auto stampChoice = [&](auto &&fn) {
        auto r = fn();
        maxCursor = std::max(maxCursor, m_lexer.cursor());
        m_lexer.setCursor(cursor);
        return r;
      };

      expr.target.tz = stampChoice([&]() { return parseTimezone(); });
      expr.target.unit = stampChoice([&]() { return parseConversionTarget(); });
      expr.target.fmt = stampChoice([&]() { return parseNumberFormat(); });
      m_lexer.setCursor(maxCursor);

      left = std::make_unique<Expression>(std::move(expr));

      continue;
    }

    if (tok->raw == "%" && isPostfixPercent(*tok)) {
      auto next = m_lexer.peak(1);
      const bool ofFollows = next && next->raw == "of";

      if (ofFollows && 3 < minPrec) { break; }

      m_lexer.next();
      left = std::make_unique<Expression>(PercentExpression{.expr = std::move(left)});

      // "of" is the same percentage, applied to what comes after it
      if (ofFollows) {
        m_lexer.next();
        left = makeBinExpr(std::move(left), parseMul(), "*");
      }

      continue;
    }

    auto it = std::ranges::find_if(operators(), [&](const OperatorDefinition &op) {
      return std::ranges::contains(op.aliases, tok->raw);
    });

    if (it != operators().end()) {
      if (it->precedence < minPrec) break;
      m_lexer.next();
      auto right = pratParse(it->rightAssociative ? it->precedence : it->precedence + 1);
      left = makeBinExpr(std::move(left), std::move(right), std::string{it->id});
    } else {
      // FIXME: ugly, hacky...
      if (!(3 < minPrec)) {
        if (auto constant = parseConstant(tok->raw)) {
          auto rhs = parseTerm();
          left = makeBinExpr(std::move(left), std::move(rhs), std::string{"*"});
          continue;
        } else if (tok->raw == "(") {
          left = makeBinExpr(std::move(left), parseTerm(), std::string{"*"});
          continue;
        }
      }

      if (tok->raw == "(" || tok->raw == ")" || isFunctionParameterSeparator(tok->raw) ||
          parseConstant(tok->raw))
        break;
      if (m_opts.strict) throw std::runtime_error(std::format("Unknown token: {}", tok->raw));

      m_lexer.next();
      continue;
    }
  }

  return left;
}

bool Parser::isFunctionParameterSeparator(std::string_view tok) const {
  return ((m_numpunct.decimal_point() != ',' && tok == ",") || tok == ";");
}
