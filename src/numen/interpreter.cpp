#include "interpreter.hpp"
#include "datetime.hpp"
#include "duration.hpp"
#include "fn.hpp"
#include "numen/numen.hpp"
#include "region-currency.hpp"
#include "timezone.hpp"
#include "utils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <iostream>
#include <numbers>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace numen::detail {

namespace {

class Interpreter {
private:
  // chains keep the first `from`
  template <class T>
  static Computed stamp(const Computed &src, Computed out, std::type_identity_t<std::optional<T>> from, T to,
                        bool implicit = false) {
    if (src.conversion) {
      if (auto prior = src.conversion->as<T>()) from = prior->from;
    }
    out.conversion =
        Conversion{.sides = ConversionOf<T>{std::move(from), std::move(to)}, .implicit = implicit};
    if (!implicit) out.explicitlyConverted = true;
    return out;
  }

  static Computed stampUnit(const Computed &src, Computed out, bool implicit = false) {
    auto n = out.asNumber();
    if (!n || !n->unit) return out;
    auto s = src.asNumber();
    return stamp(src, out, s ? s->unit : std::nullopt, *n->unit, implicit);
  }

  Computed applyBinary(Computed lhs, Computed rhs, const std::string &op) const {

    if (lhs.asNumber() && rhs.asNumber()) {
      // converting years into months would floor the fraction, and adding
      // durations does not need a common unit to begin with
      const bool durationSum = (op == "+" || op == "-") && promoteDuration(lhs) && promoteDuration(rhs);

      if (!durationSum) { reconcileUnits(lhs, rhs, op); }
    }

    auto r = [&]() -> std::optional<Computed> {
      if (op == "+") return add(lhs, rhs);
      if (op == "-") return subtract(lhs, rhs);
      if (op == "*") return multiply(lhs, rhs);
      if (op == "/") return div(lhs, rhs);
      if (op == "%") return modulo(lhs, rhs);
      if (op == "^") return pow(lhs, rhs);
      if (op == "<<") return leftshift(lhs, rhs);
      if (op == ">>") return rightshift(lhs, rhs);
      if (op == "&") return bitwiseAnd(lhs, rhs);
      if (op == "|") return bitwiseor(lhs, rhs);
      if (op == "==") return Computed{.value = Boolean{lhs.value == rhs.value}};
      if (op == "!=") return Computed{.value = Boolean{lhs.value != rhs.value}};
      if (op == ">") return Computed{.value = Boolean{lhs.value > rhs.value}};
      if (op == ">=") return Computed{.value = Boolean{lhs.value >= rhs.value}};
      if (op == "<") return Computed{.value = Boolean{lhs.value < rhs.value}};
      if (op == "<=") return Computed{.value = Boolean{lhs.value <= rhs.value}};
      return std::nullopt;
    }();

    if (r) {
      r->explicitlyConverted = lhs.explicitlyConverted || rhs.explicitlyConverted;
      return *r;
    }

    throw std::runtime_error(std::format("Unhandled operator {}", op));
  }

public:
  Interpreter(const UnitDatabase &db, const EvalOptions &opts)
      : m_db(db), m_opts(opts), m_now(opts.now.value_or(std::chrono::system_clock::now())) {}

  Computed computeExpr(const Expression &expr) const {
    auto visitor = [&](const auto &value) -> Computed {
      using T = std::remove_cvref_t<decltype(value)>;
      if constexpr (std::is_same_v<T, UnaryExpression>) {
        const auto &ue = value;
        auto c = computeExpr(*ue.lhs);

        if (auto n = c.asNumber(); n && ue.op == "-") n->n = -n->n;

        return c;
      } else if constexpr (std::is_same_v<T, BinaryExpression>) {
        // left-associative chains nest on the lhs: fold the spine in a loop so a
        // long "1+1+...+1" does not recurse once per operator
        std::vector<const BinaryExpression *> spine{&value};
        while (auto next = std::get_if<BinaryExpression>(&spine.back()->lhs->data)) {
          spine.push_back(next);
        }
        auto acc = computeExpr(*spine.back()->lhs);
        for (auto it = spine.rbegin(); it != spine.rend(); ++it) {
          acc = applyBinary(std::move(acc), computeExpr(*(*it)->rhs), (*it)->op);
        }
        return acc;
      } else if constexpr (std::is_same_v<T, ConversionExpression>) {
        const auto &conv = value;
        auto v = computeExpr(*conv.lhs);

        if (v.isDateTime()) {
          if (auto tz = conv.target.tz) {
            auto d = *v.asDateTime();

            d.tz = TimezoneDB{}.query(tz->name);
            d.offset = tz->offset;

            return stamp(v, Computed{d}, Timezone{v.asDateTime()->tz, v.asDateTime()->offset},
                         Timezone{d.tz, d.offset});
          }

          if (auto unit = conv.target.unit; unit && unit->isSimple()) {
            if (std::ranges::contains(std::initializer_list<std::string_view>{"unix", "epoch"},
                                      unit->simpleName())) {
              auto dt = v.asDateTime();
              auto seconds_epoch =
                  std::chrono::duration_cast<std::chrono::seconds>(dt->time.time_since_epoch()).count();

              return stampUnit(v, Computed{Num{.n = Value{static_cast<double>(seconds_epoch)},
                                               .unit = Number::Unit{.raw = "second"}}});
            }
          }
        }

        if (auto d = v.asDuration()) {
          if (auto unit = conv.target.unit; unit && unit->isSimple()) {
            return stampUnit(
                v, convertToUnit(static_cast<double>(d->total().count()), "second", unit->simpleName()));
          }
        }

        if (auto n = v.asNumber()) {
          if (auto fmt = conv.target.fmt) {
            const auto toFormatted = [&](NumberOutputFormat format) {
              auto out = v;
              out.asNumber()->format = format;
              return out;
            };

            if (fmt->name == "hex" || fmt->name == "hexadecimal") {
              return toFormatted(NumberOutputFormat::Hexadecimal);
            }
            if (fmt->name == "bin" || fmt->name == "binary") {
              return toFormatted(NumberOutputFormat::Binary);
            }
            if (fmt->name == "octal") { return toFormatted(NumberOutputFormat::Octal); }
            if (fmt->name == "dec" || fmt->name == "decimal") {
              return toFormatted(NumberOutputFormat::Decimal);
            }
          }

          if (auto unit = conv.target.unit) {
            auto target = buildTarget(*unit);

            if (!n->unit) {
              n->unit = Number::Unit{.raw = std::string{unit->simpleName()}, .resolved = target};
              return Computed{.value = *n};
            }

            // a single token can still name a composition, so ask what it
            // resolved to rather than how it was spelled
            const bool plainTarget = unit->isSimple() && target.sole();
            const bool plainSource = !n->unit->resolved || n->unit->resolved->sole();

            // the plain path is the only one that knows about offsets, and the
            // only one that can settle an ambiguous token against its target
            if (plainTarget && plainSource) {
              return stampUnit(v, convertToUnit(n->n.toDouble(), n->unit->raw, unit->simpleName()));
            }

            return stampUnit(v, convertCompound(*n, std::move(target)));
          }
        }
        if (auto unit = conv.target.unit) {
          throw std::runtime_error(
              std::format("Cannot convert a {} to {}", v.valueTypeName(),
                          unit->isSimple() ? std::string{unit->simpleName()} : buildTarget(*unit).render()));
        }

        throw std::runtime_error(std::format("Cannot convert a {} to that", v.valueTypeName()));
      } else if constexpr (std::is_same_v<T, NumberString>) {
        return Computed{.value = Num{value}};
      } else if constexpr (std::is_same_v<T, PostfixExpression>) {
        auto lhs = computeExpr(*value.lhs);
        if (auto n = lhs.asNumber(); n && value.op == "k") { n->n = n->n.toDouble() * 1e3; }
        return lhs;
      } else if constexpr (std::is_same_v<T, UnitExpression>) {
        const auto &ue = value;
        Computed c{.value = computeExpr(*ue.expr).value};

        // unit only makes sense for a number, ignore it otherwise
        if (auto n = c.asNumber()) {
          n->unit = Number::Unit{.raw = std::string{ue.unit.simpleName()}, .resolved = buildTarget(ue.unit)};
        }

        return c;
      } else if constexpr (std::is_same_v<T, PercentExpression>) {
        auto c = computeExpr(*value.expr);

        if (auto n = c.asNumber()) {
          n->n = n->n / Value{100};
          n->isPercentage = true;
        }

        return c;
      } else if constexpr (std::is_same_v<T, FunctionCall>) {
        return executeFunction(value);
      } else if constexpr (std::is_same_v<T, Duration>) {
        return {value};
      } else {
        static_assert(std::is_same_v<T, DateString>);
        const auto &ds = value;
        auto &tz = m_opts.timezone ? *m_opts.timezone : *tz::current_zone();
        auto dt = parseDateTime(ds, tz, m_now);
        return Computed{.value = dt};
      }
    };

    return std::visit(visitor, expr.data);
  }

  Computed computeExprBase(const Expression &expr) const {
    auto result = computeExpr(expr);

    if (auto dt = result.asDateTime();
        dt && m_opts.implicitTimezoneConversion && !result.explicitlyConverted) {
      DateTime out = *dt;
      out.tz = m_opts.timezone ? m_opts.timezone : tz::current_zone();
      return Computed{out};
    }

    if (auto n = result.asNumber(); m_opts.implicitCurrencyConversion && n && n->unit && n->unit->def() &&
                                    n->unit->def()->dimension == dimensions::CURRENCY &&
                                    !result.explicitlyConverted) {
      auto target = numen::currencyForLocale(m_opts.parseOptions.locale.value_or(numen::monetaryLocale()));
      if (target && !equalsIgnoreCase(*target, n->unit->def()->id)) {
        result = stampUnit(result, convertToUnit(n->n.toDouble(), n->unit->def()->id, *target), true);
      }
    }

    if (auto n = result.asNumber(); m_opts.implicitUnitConversion && n && n->unit && n->unit->def() &&
                                    !n->unit->def()->implicitTarget.empty() && !result.explicitlyConverted) {
      result = stampUnit(
          result, convertToUnit(n->n.toDouble(), n->unit->def()->id, n->unit->def()->implicitTarget), true);
    }

    if (auto n = result.asNumber(); n && !result.explicitlyConverted) {
      if (auto d = foldToDuration(*n)) return Computed{.value = *d};
    }

    return result;
  }

private:
  std::optional<Duration> foldToDuration(const Num &n) const {
    if (!n.unit) return std::nullopt;

    // the settled reading beats re-deriving one, which may still be ambiguous
    if (auto def = n.unit->def()) return durationFrom(n.n.toDouble(), *def);

    auto candidates = m_db.findUnitCandidates(n.unit->raw);
    if (candidates.size() != 1) return std::nullopt;

    return durationFrom(n.n.toDouble(), candidates.front());
  }

  std::optional<Duration> promoteDuration(const Computed &v) const {
    if (auto dur = v.asDuration()) return *dur;
    if (auto n = v.asNumber()) return foldToDuration(*n);

    return std::nullopt;
  }

  // the unambiguous side decides the other: in "1m to s" the second operand is
  // what makes "m" a minute. nullopt when both are ambiguous
  std::optional<std::pair<UnitDef, UnitDef>> resolvePair(std::string_view fromUnit,
                                                         std::string_view toUnit) const {
    auto valueCandidates = m_db.findUnitCandidates(fromUnit);
    auto targetCandidates = m_db.findUnitCandidates(toUnit);

    if (valueCandidates.empty()) { throw std::runtime_error(std::format("Unknown unit \"{}\"", fromUnit)); }
    if (targetCandidates.empty()) { throw std::runtime_error(std::format("Unknown unit \"{}\"", toUnit)); }

    if (valueCandidates.size() == 1 && targetCandidates.size() == 1) {
      return std::pair{valueCandidates.front(), targetCandidates.front()};
    }

    if (valueCandidates.size() > 1 && targetCandidates.size() > 1) { return std::nullopt; }

    if (valueCandidates.size() > targetCandidates.size()) {
      auto rhs = targetCandidates.front();
      auto lhs = std::ranges::find_if(valueCandidates,
                                      [&](const UnitDef &unit) { return unit.dimension == rhs.dimension; });
      if (lhs == valueCandidates.end()) {
        throw std::runtime_error(std::format("Incompatible units: no common family"));
      }
      return std::pair{*lhs, rhs};
    }

    auto lhs = valueCandidates.front();
    auto rhs = std::ranges::find_if(targetCandidates,
                                    [&](const UnitDef &unit) { return unit.dimension == lhs.dimension; });
    if (rhs == targetCandidates.end()) {
      throw std::runtime_error(std::format("Incompatible units: no common type"));
    }
    return std::pair{lhs, *rhs};
  }

  // callers holding both readings must come here: convertToUnit rediscovers them
  // from the tokens and cannot when both are ambiguous
  Computed convertResolved(double v, const UnitDef &from, const UnitDef &to, std::string display,
                           bool pinned = true) const {
    if (from.dimension != to.dimension) {
      throw std::runtime_error(std::format("Incompatible units: {} ({}) to {} ({})", from.id, from.dimension,
                                           to.id, to.dimension));
    }

    auto res = m_db.convert(v, from, to);

    if (!res) throw std::runtime_error(res.error());

    return {
        .value = Num{.n = Value{res.value()},
                     .unit = Number::Unit{.raw = std::move(display), .resolved = soleUnit(to)}},
        .explicitlyConverted = pinned,
    };
  }

  CompoundUnit buildTarget(const NamedUnit &named) const {
    std::vector<UnitTerm> terms;

    for (const auto &named_term : named.terms) {
      auto candidates = m_db.findCompounds(named_term.name);
      if (candidates.empty()) {
        throw std::runtime_error(std::format("Unknown unit \"{}\"", named_term.name));
      }

      // the named term may itself be a composition, as in "to kmh"
      for (const auto &part : candidates.front().terms) {
        auto exponent = static_cast<std::int8_t>(part.exponent * named_term.exponent);
        auto known = std::ranges::find_if(terms, [&](const UnitTerm &x) { return x.def.id == part.def.id; });

        if (known == terms.end()) {
          terms.push_back(UnitTerm{.def = part.def, .exponent = exponent});
        } else {
          known->exponent = static_cast<std::int8_t>(known->exponent + exponent);
        }
      }
    }

    std::erase_if(terms, [](const UnitTerm &term) { return term.exponent == 0; });
    return CompoundUnit{std::move(terms)};
  }

  Computed convertCompound(const Num &n, CompoundUnit target) const {
    Num source = n;
    resolveToDefault(source);
    const auto &from = *source.unit->resolved;

    // Allow implict conversion such as "150 km/h to in", by promoting rhs to in/h
    if (!from.sole() && target.sole()) {
      if (from.terms[0].def.dimension == target.terms[0].def.dimension) {
        target.terms.insert(target.terms.end(), from.terms.begin() + 1, from.terms.end());
      }
    }

    if (from.dimension() != target.dimension()) {
      throw std::runtime_error(std::format("Incompatible units: {} to {}", from.render(), target.render()));
    }

    validateTerms(from.terms);
    validateTerms(target.terms);

    auto ratio = m_db.conversionRatio(from, target);
    if (!ratio) throw std::runtime_error(ratio.error());

    auto display = target.render();

    return Computed{.value = Num{.n = Value{n.n.toDouble() * *ratio},
                                 .unit = Number::Unit{.raw = display, .resolved = std::move(target)}},
                    .explicitlyConverted = true};
  }

  Computed convertToCompound(const Num &n, const NamedUnit &named) const {
    return convertCompound(n, buildTarget(named));
  }

  Computed convertToUnit(double v, std::string_view fromUnit, std::string_view toUnit) const {
    auto pair = resolvePair(fromUnit, toUnit);

    // we are unable to infer what unit should be used, we need to
    // wait for more info...
    if (!pair) {
      return Computed{.value = Num{.n = Value{v}, .unit = Number::Unit{.raw = std::string{toUnit}}},
                      .explicitlyConverted = true};
    }

    return convertResolved(v, pair->first, pair->second, std::string{toUnit});
  }

  static bool composes(std::string_view op) { return op == "*" || op == "/"; }

  void resolveToDefault(Num &n) const {
    if (n.unit->resolved) return;

    auto candidates = m_db.findCompounds(n.unit->raw);
    if (candidates.empty()) { throw std::runtime_error(std::format("Unknown unit \"{}\"", n.unit->raw)); }
    n.unit->resolved = std::move(candidates.front());
  }

  static bool isComposed(const Number::Unit &unit) { return unit.resolved && !unit.resolved->sole(); }

  void reconcileCompounds(Computed &lhs, Computed &rhs) const {
    auto n1 = lhs.asNumber();
    auto n2 = rhs.asNumber();

    resolveToDefault(*n1);
    resolveToDefault(*n2);

    const auto &ca = *n1->unit->resolved;
    const auto &cb = *n2->unit->resolved;

    const bool keepLhs = ca.hasStableFactor() && cb.hasStableFactor() && ca.factor() > cb.factor();

    if (keepLhs) {
      rhs = convertCompound(*n2, ca);
    } else {
      lhs = convertCompound(*n1, cb);
    }
  }

  void reconcileUnits(Computed &lhs, Computed &rhs, std::string_view op) const {
    auto n1 = lhs.asNumber();
    auto n2 = rhs.asNumber();
    if (!n1 || !n2) return;

    // each side reads on its own terms: "2 m * 3 s" must not let the second
    // operand turn the metre into a minute
    if (composes(op) || op == "^") {
      if (n1->unit) resolveToDefault(*n1);
      if (n2->unit) resolveToDefault(*n2);
    }

    if (!n1->unit || !n2->unit) return;

    UnitDef a, b;

    if (composes(op)) {
      auto da = n1->unit->def();
      auto db = n2->unit->def();
      if (!da || !db || da->dimension != db->dimension) return;
      a = *da;
      b = *db;
    } else {
      // a composed unit renders its name, so "km/h" is not in the table
      if (isComposed(*n1->unit) || isComposed(*n2->unit)) {
        reconcileCompounds(lhs, rhs);
        return;
      }

      auto pair = resolvePair(n1->unit->raw, n2->unit->raw);
      if (!pair) return;
      a = pair->first;
      b = pair->second;
    }

    // larger wins, so "1 km + 100 m" reads as 1.1km. a factor that moves has no
    // size to rank by, so there the right-hand side decides
    const bool keepLhs = !traitsOf(a.dimension).dynamicFactor && a.factor > b.factor;

    // the kept side holds the settled reading, so "1m + 30s" still knows its
    // "m" is a minute once the other side is gone
    // reconciling is not the user pinning a unit, so the operand's own flag stays
    if (keepLhs) {
      n1->unit->resolved = soleUnit(a);
      rhs = convertResolved(n2->n.toDouble(), b, a, n1->unit->raw, rhs.explicitlyConverted);
    } else {
      n2->unit->resolved = soleUnit(b);
      lhs = convertResolved(n1->n.toDouble(), a, b, n2->unit->raw, lhs.explicitlyConverted);
    }
  }

  static void validateTerms(const std::vector<UnitTerm> &terms) {
    for (const auto &term : terms) {
      // 0°C is a point on a scale, not a quantity that can be multiplied out
      if (term.def.offset != 0 && (terms.size() > 1 || term.exponent != 1)) {
        throw std::runtime_error(std::format("Cannot build a compound unit out of {}", term.def.id));
      }

      if (compositionOf(term.def.dimension) != Composition::RateOnly) continue;

      // "usd/kg" and "km/usd" mean something, "usd·kg" and "usd²" do not
      const bool alone = std::ranges::none_of(terms, [&](const UnitTerm &other) {
        return other.def.id != term.def.id && (other.exponent > 0) == (term.exponent > 0);
      });

      if (std::abs(term.exponent) != 1 || !alone) {
        throw std::runtime_error(
            std::format("{} can only be combined with other units as a rate", term.def.id));
      }
    }
  }

  // nullopt once everything cancels, which is what makes "1 km / 100 m" plain
  static std::optional<Number::Unit> composeUnits(const Num &n1, const Num &n2, int sign) {
    std::vector<UnitTerm> terms;

    auto merge = [&](const Number::Unit &unit, int s) {
      if (!unit.resolved) { throw std::runtime_error(std::format("Unknown unit \"{}\"", unit.raw)); }

      for (const auto &term : unit.resolved->terms) {
        auto exponent = static_cast<std::int8_t>(term.exponent * s);
        auto known = std::ranges::find_if(terms, [&](const UnitTerm &x) { return x.def.id == term.def.id; });

        if (known == terms.end()) {
          terms.push_back(UnitTerm{.def = term.def, .exponent = exponent});
        } else {
          known->exponent = static_cast<std::int8_t>(known->exponent + exponent);
        }
      }
    };

    if (n1.unit) merge(*n1.unit, 1);
    if (n2.unit) merge(*n2.unit, sign);

    std::erase_if(terms, [](const UnitTerm &term) { return term.exponent == 0; });
    if (terms.empty()) return std::nullopt;

    validateTerms(terms);

    CompoundUnit compound{std::move(terms)};
    return Number::Unit{.raw = compound.render(), .resolved = compound};
  }

  template <typename T, typename U = T> static void assertBinary(const Computed &lhs, const Computed &rhs) {
    const bool ok = std::holds_alternative<T>(lhs.value) && std::holds_alternative<U>(rhs.value);

    if (!ok) {
      throw std::runtime_error(
          std::format("Invalid operands: {} and {}", lhs.valueTypeName(), rhs.valueTypeName()));
    }
  }

  // swappable should be set to true for commutative operators
  template <typename T, typename U>
  static std::optional<std::tuple<const T *, const U *>>
  getTypedOperands(const Computed &lhs, const Computed &rhs, bool swappable = false) {
    if (std::holds_alternative<T>(lhs.value) && std::holds_alternative<U>(rhs.value)) {
      return std::tuple<const T *, const U *>{std::get_if<T>(&lhs.value), std::get_if<U>(&rhs.value)};
    }

    if constexpr (std::is_same_v<T, U>) { return std::nullopt; }

    if (swappable) return getTypedOperands<T, U>(rhs, lhs, false);

    return std::nullopt;
  }

  Computed add(const Computed &lhs, const Computed &rhs) const {
    {
      auto dur1 = promoteDuration(lhs);
      auto dur2 = promoteDuration(rhs);
      if (dur1 && dur2) { return {*dur1 + *dur2}; }
    }

    if (auto ops = getTypedOperands<DateTime, Duration>(lhs, rhs, true)) {
      auto [dt, dur] = *ops;
      auto result = *dt;

      if (auto y = dur->years) { result.time = shift(result.time, *y); }
      if (auto m = dur->months) { result.time = shift(result.time, *m); }
      if (auto s = dur->seconds) { result.time += *s; }
      if (auto ns = dur->subsecond) { result.time += std::chrono::duration_cast<TimePoint::duration>(*ns); }

      return Computed{result};
    }

    if (auto ops = getTypedOperands<DateTime, Num>(lhs, rhs, true)) {
      auto [d, n] = *ops;
      if (auto dur = foldToDuration(*n)) return add(Computed{.value = *d}, Computed{.value = *dur});
    }

    if (auto ops = getTypedOperands<Num, Num>(lhs, rhs, true)) {
      auto [n1, n2] = *ops;
      if (n2->isPercentage && !n1->isPercentage) { return output(n1->n + n1->n * n2->n, *n1, *n2); }
      return output(n1->n + n2->n, *n1, *n2);
    }

    throw std::runtime_error(std::format("Cannot add {} to {}", rhs.valueTypeName(), lhs.valueTypeName()));
  }

  Computed subtract(const Computed &lhs, const Computed &rhs) const {
    {
      auto dur1 = promoteDuration(lhs);
      auto dur2 = promoteDuration(rhs);
      if (dur1 && dur2) return Computed{*dur1 - *dur2};
    }

    if (lhs.isDateTime() && rhs.isDateTime()) {
      if (lhs.asDateTime()->time > rhs.asDateTime()->time) return subtract(rhs, lhs);
      return Computed{subtractDates(*lhs.asDateTime(), *rhs.asDateTime())};
    }

    if (lhs.isDateTime() && rhs.asDuration()) {
      auto dt = lhs.asDateTime();
      auto dur = rhs.asDuration();
      auto result = *dt;

      if (auto y = dur->years) result.time = shift(result.time, -*y);
      if (auto m = dur->months) result.time = shift(result.time, -*m);
      if (auto s = dur->seconds) result.time += -*s;
      if (auto ns = dur->subsecond) result.time -= std::chrono::duration_cast<TimePoint::duration>(*ns);

      return Computed{result};
    }

    if (lhs.asDuration() && rhs.asDuration()) { return Computed{{*lhs.asDuration() - *rhs.asDuration()}}; }

    if (auto ops = getTypedOperands<DateTime, Num>(lhs, rhs)) {
      auto [d, n] = *ops;
      if (auto dur = foldToDuration(*n)) return subtract(Computed{.value = *d}, Computed{.value = *dur});
    }

    assertBinary<Num, Num>(lhs, rhs);

    auto n1 = lhs.asNumber();
    auto n2 = rhs.asNumber();

    if (n2->isPercentage && !n1->isPercentage) { return output(n1->n - n1->n * n2->n, *n1, *n2); }

    return output(n1->n - n2->n, *n1, *n2);
  }

  static Computed multiply(const Computed &lhs, const Computed &rhs) {
    if (auto ops = getTypedOperands<Duration, Num>(lhs, rhs, true)) {
      auto [dur, n] = *ops;
      if (!n->unit) return Computed{.value = scaleDuration(*dur, n->n.toDouble())};
    }

    assertBinary<Num, Num>(lhs, rhs);

    auto n1 = lhs.asNumber();
    auto n2 = rhs.asNumber();

    return output(n1->n * n2->n, *n1, *n2, composeUnits(*n1, *n2, 1));
  }

  static Computed div(const Computed &lhs, const Computed &rhs) {
    if (auto ops = getTypedOperands<Duration, Num>(lhs, rhs)) {
      auto [dur, n] = *ops;
      if (!n->unit) {
        if (n->n.isZero()) throw std::runtime_error("Division by zero");
        return Computed{.value = scaleDuration(*dur, 1.0 / n->n.toDouble())};
      }
    }

    assertBinary<Num, Num>(lhs, rhs);
    if (rhs.asNumber()->n.isZero()) throw std::runtime_error("Division by zero");

    auto n1 = lhs.asNumber();
    auto n2 = rhs.asNumber();

    return output(n1->n / n2->n, *n1, *n2, composeUnits(*n1, *n2, -1));
  }

  static Computed modulo(const Computed &lhs, const Computed &rhs) {
    assertBinary<Num, Num>(lhs, rhs);
    return output(lhs.asNumber()->n.mod(rhs.asNumber()->n), *lhs.asNumber(), *rhs.asNumber());
  }

  static Computed pow(const Computed &lhs, const Computed &rhs) {
    assertBinary<Num, Num>(lhs, rhs);
    auto n1 = lhs.asNumber();
    auto n2 = rhs.asNumber();
    auto raised = n1->n.pow(n2->n);

    if (!n1->unit || n2->n == Value{1}) return output(raised, *n1, *n2);
    if (n2->n.isZero()) return output(raised, *n1, *n2, std::nullopt);

    auto exponent = n2->n.toDouble();
    // a fractional power would need a root of the dimension, and exponents are
    // stored in a byte
    if (exponent != std::trunc(exponent) || std::abs(exponent) > 9) {
      throw std::runtime_error("Cannot raise a unit to that power");
    }

    if (!n1->unit->resolved) { throw std::runtime_error(std::format("Unknown unit \"{}\"", n1->unit->raw)); }

    auto terms = n1->unit->resolved->terms;

    for (auto &term : terms) {
      term.exponent = static_cast<std::int8_t>(term.exponent * static_cast<int>(exponent));
    }

    validateTerms(terms);

    CompoundUnit compound{std::move(terms)};
    return output(raised, *n1, *n2, Number::Unit{.raw = compound.render(), .resolved = compound});
  }

  static Computed leftshift(const Computed &lhs, const Computed &rhs) {
    assertBinary<Num, Num>(lhs, rhs);
    return output(lhs.asNumber()->n << rhs.asNumber()->n, *lhs.asNumber(), *rhs.asNumber());
  }

  static Computed rightshift(const Computed &lhs, const Computed &rhs) {
    assertBinary<Num, Num>(lhs, rhs);
    return output(lhs.asNumber()->n >> rhs.asNumber()->n, *lhs.asNumber(), *rhs.asNumber());
  }

  static Computed bitwiseor(const Computed &lhs, const Computed &rhs) {
    assertBinary<Num, Num>(lhs, rhs);
    return output(lhs.asNumber()->n | rhs.asNumber()->n, *lhs.asNumber(), *rhs.asNumber());
  }

  static Computed bitwiseAnd(const Computed &lhs, const Computed &rhs) {
    assertBinary<Num, Num>(lhs, rhs);
    return output(lhs.asNumber()->n & rhs.asNumber()->n, *lhs.asNumber(), *rhs.asNumber());
  }

  static Computed output(Value n, const Num &lhs, const Num &rhs) {
    return output(n, lhs, rhs, rhs.unit.or_else([&]() { return lhs.unit; }));
  }

  static Computed output(Value n, const Num &lhs, const Num &, std::optional<Number::Unit> unit) {
    if (n.isNaN()) throw std::runtime_error("Result is undefined");

    return Computed{.value = Num{.n = n, .format = lhs.format, .unit = std::move(unit)}};
  }

  // arguments are computed independently, so without this "min(1 km, 999 m)"
  // compares the bare numbers and answers 1 km
  void reconcileArguments(std::vector<Computed> &args) const {
    if (args.size() < 2) return;

    for (auto &arg : args) {
      auto n = arg.asNumber();
      // a plain number among them, as in "max(1 km to m, 100)"
      if (!n || !n->unit) return;
      resolveToDefault(*n);
    }

    const CompoundUnit *target = &*args.front().asNumber()->unit->resolved;

    bool rankable = true;

    for (auto &arg : args) {
      const auto &unit = *arg.asNumber()->unit->resolved;

      if (unit.dimension() != target->dimension()) {
        throw std::runtime_error(
            std::format("Incompatible units: {} to {}", unit.render(), target->render()));
      }

      rankable = rankable && unit.hasStableFactor();
    }

    if (rankable) {
      for (auto &arg : args) {
        const auto &unit = *arg.asNumber()->unit->resolved;
        if (unit.factor() > target->factor()) target = &unit;
      }
    }

    auto chosen = *target;

    for (auto &arg : args) {
      auto n = arg.asNumber();
      auto from = n->unit->resolved->sole();
      auto to = chosen.sole();

      // only the plain path knows about offsets, which affine units need
      if (from && to) {
        arg = convertResolved(n->n.toDouble(), *from, *to, chosen.render());
      } else {
        arg = convertCompound(*n, chosen);
      }
    }
  }

  Computed executeFunction(const FunctionCall &fn) const {
    auto handler = FunctionDatabase::builtin().find(fn.name);
    if (!handler) throw std::runtime_error(std::format("Unknown function \"{}\"", fn.name));

    auto computedArgs = fn.args | std::views::transform([&](auto &&expr) { return computeExpr(*expr); }) |
                        std::ranges::to<std::vector>();

    reconcileArguments(computedArgs);

    auto r = (*handler)(FunctionCtx{.name = fn.name, .args = computedArgs});
    r.explicitlyConverted = std::ranges::any_of(computedArgs, &Computed::explicitlyConverted);
    return r;
  }

  const UnitDatabase &m_db;
  const EvalOptions &m_opts;
  TimePoint m_now;
};

} // namespace

Computed interpret(const Expression &expr, const UnitDatabase &db, const EvalOptions &opts) {
  return Interpreter{db, opts}.computeExprBase(expr);
}

} // namespace numen::detail
