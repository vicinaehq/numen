#include "fn.hpp"
#include "computed.hpp"
#include "numen/numen.hpp"
#include "utils.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace numen::detail {

const Num &FunctionCtx::number(std::size_t i) const {
  if (auto n = args[i].asNumber()) return *n;
  throw std::runtime_error(
      std::format("{}: argument {} must be a number, got {}", name, i + 1, args[i].valueTypeName()));
}

const DateTime &FunctionCtx::dateTime(std::size_t i) const {
  if (auto n = args[i].asDateTime()) return *n;
  throw std::runtime_error(
      std::format("{}: argument {} must be a date, got {}", name, i + 1, args[i].valueTypeName()));
}

// const Num &FunctionCtx::number(std::size_t i) const {
//   if (auto n = args[i].asNumber()) return *n;
//   throw std::runtime_error(
//       std::format("{}: argument {} must be a number, got {}", name, i + 1, args[i].valueTypeName()));
// }

std::vector<const Num *> FunctionCtx::numbers() const {
  std::vector<const Num *> out;
  out.reserve(args.size());
  for (std::size_t i = 0; i < args.size(); ++i)
    out.push_back(&number(i));
  return out;
}

void FunctionCtx::expectArgs(std::size_t n) const {
  if (args.size() != n) {
    throw std::runtime_error(
        std::format("{}: expected {} argument{}, got {}", name, n, n == 1 ? "" : "s", args.size()));
  }
}

void FunctionCtx::expectArgs(std::size_t min, std::size_t max) const {
  if (args.size() < min || args.size() > max) {
    throw std::runtime_error(
        std::format("{}: expected {} to {} arguments, got {}", name, min, max, args.size()));
  }
}

void FunctionCtx::expectAtLeast(std::size_t n) const {
  if (args.size() < n) {
    throw std::runtime_error(
        std::format("{}: expected at least {} argument{}, got {}", name, n, n == 1 ? "" : "s", args.size()));
  }
}

void FunctionDatabase::add(std::string_view name, FunctionHandler handler, bool isConverter) {
  m_fns.emplace_back(Entry{.name = name, .fn = std::move(handler)});
  if (isConverter) { m_convNames.emplace_back(name); }
}

void FunctionDatabase::addConverter(std::string_view name, FunctionHandler handler) {
  add(name, std::move(handler), true);
}

const FunctionHandler *FunctionDatabase::find(std::string_view name) const {
  auto it = std::ranges::find(m_fns, name, &Entry::name);
  return it == m_fns.end() ? nullptr : &it->fn;
}

const std::vector<std::string> &FunctionDatabase::converterNames() const { return m_convNames; }

namespace {

enum class UnitPolicy {
  Keep,    // abs(-3 m) is 3 m
  Discard, // sign(-3 m) is -1
  Forbid,  // sqrt(4 m) is an error
};

std::string describe(const Num &n) {
  std::string out = n.n.render(n.format);
  if (n.unit) out += " " + (n.unit->resolved ? n.unit->resolved->render() : n.unit->raw);
  return out;
}

const Num &plain(const FunctionCtx &ctx, std::size_t i) {
  const auto &n = ctx.number(i);
  if (n.unit) {
    throw std::runtime_error(
        std::format("{}: argument {} must be a plain number, got {}", ctx.name, i + 1, describe(n)));
  }
  return n;
}

const Num &integer(const FunctionCtx &ctx, std::size_t i) {
  const auto &n = plain(ctx, i);
  if (!n.n.isInteger()) {
    throw std::runtime_error(
        std::format("{}: argument {} must be an integer, got {}", ctx.name, i + 1, describe(n)));
  }
  return n;
}

Computed output(const FunctionCtx &ctx, double v, const Num &like, UnitPolicy policy) {
  if (std::isnan(v)) throw std::runtime_error(std::format("{}: result is undefined", ctx.name));

  Num out{.n = Value{v}, .format = like.format};

  if (policy == UnitPolicy::Keep) {
    out.unit = like.unit;
    out.isPercentage = like.isPercentage;
  }

  return Computed{.value = out};
}

// n holds the fraction behind a percentage, but rounding acts on the number as
// written: round(12.34%) is 12%, not 0%
double visible(const Num &n) { return n.isPercentage ? n.n.toDouble() * 100 : n.n.toDouble(); }
double fromVisible(const Num &like, double v) { return like.isPercentage ? v / 100 : v; }

Computed unary(const FunctionCtx &ctx, double (*fn)(double), UnitPolicy policy) {
  ctx.expectArgs(1);
  const auto &n = policy == UnitPolicy::Forbid ? plain(ctx, 0) : ctx.number(0);

  if (policy == UnitPolicy::Keep) return output(ctx, fromVisible(n, fn(visible(n))), n, policy);
  return output(ctx, fn(n.n.toDouble()), n, policy);
}

double sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }

double factorial(double x) {
  if (x < 0) return std::numeric_limits<double>::quiet_NaN();
  if (x != std::trunc(x)) return std::tgamma(x + 1);
  // 171! overflows a double, so the loop stays short
  double out = 1;
  // NOLINTNEXTLINE(bugprone-float-loop-counter,clang-analyzer-security.FloatLoopCounter)
  for (double i = 2; i <= x; ++i)
    out *= i;
  return out;
}

double roundTo(double x, double digits) {
  auto scale = std::pow(10.0, digits);
  return std::round(x * scale) / scale;
}

struct UnaryDef {
  std::string_view name;
  double (*fn)(double);
  UnitPolicy policy = UnitPolicy::Forbid;
};

// clang-format off
constexpr UnaryDef UNARY_FUNCTIONS[] = {
    // shape-preserving
    {"abs",       [](double x) { return std::fabs(x); },  UnitPolicy::Keep},
    {"floor",     [](double x) { return std::floor(x); }, UnitPolicy::Keep},
    {"ceil",      [](double x) { return std::ceil(x); },  UnitPolicy::Keep},
    {"trunc",     [](double x) { return std::trunc(x); }, UnitPolicy::Keep},
    {"sign",      sign,                                   UnitPolicy::Discard},
    {"sgn",       sign,                                   UnitPolicy::Discard},

    // roots and exponentials
    {"sqrt",      [](double x) { return std::sqrt(x); }},
    {"cbrt",      [](double x) { return std::cbrt(x); }},
    {"exp",       [](double x) { return std::exp(x); }},
    {"exp2",      [](double x) { return std::exp2(x); }},
    {"expm1",     [](double x) { return std::expm1(x); }},
    {"ln",        [](double x) { return std::log(x); }},
    {"log2",      [](double x) { return std::log2(x); }},
    {"log10",     [](double x) { return std::log10(x); }},
    {"log1p",     [](double x) { return std::log1p(x); }},

    // trigonometry, in radians
    {"sin",       [](double x) { return std::sin(x); }},
    {"cos",       [](double x) { return std::cos(x); }},
    {"tan",       [](double x) { return std::tan(x); }},
    {"asin",      [](double x) { return std::asin(x); }},
    {"acos",      [](double x) { return std::acos(x); }},
    {"atan",      [](double x) { return std::atan(x); }},
    {"sinh",      [](double x) { return std::sinh(x); }},
    {"cosh",      [](double x) { return std::cosh(x); }},
    {"tanh",      [](double x) { return std::tanh(x); }},
    {"asinh",     [](double x) { return std::asinh(x); }},
    {"acosh",     [](double x) { return std::acosh(x); }},
    {"atanh",     [](double x) { return std::atanh(x); }},

    // special functions
    {"gamma",     [](double x) { return std::tgamma(x); }},
    {"lgamma",    [](double x) { return std::lgamma(x); }},
    {"erf",       [](double x) { return std::erf(x); }},
    {"erfc",      [](double x) { return std::erfc(x); }},
    {"fact",      factorial},
    {"factorial", factorial},
};
// clang-format on

// the winning argument is returned as is so that its unit survives
template <typename Cmp> Computed extremum(const FunctionCtx &ctx, Cmp cmp) {
  ctx.expectAtLeast(1);
  auto nn = ctx.numbers();
  auto best = std::ranges::min_element(nn, cmp, [](const Num *n) { return n->n; });
  return Computed{.value = **best};
}

template <typename F> Computed fold(const FunctionCtx &ctx, F fn) {
  ctx.expectAtLeast(1);
  auto nn = ctx.numbers();
  auto like = std::ranges::find_if(nn, [](const Num *n) { return n->unit.has_value(); });
  auto values = nn | std::views::transform([](const Num *n) { return n->n.toDouble(); });
  return output(ctx, fn(values), like == nn.end() ? *nn.front() : **like, UnitPolicy::Keep);
}

std::int64_t toInt64(const FunctionCtx &ctx, std::size_t i) {
  auto v = integer(ctx, i).n.toDouble();
  if (std::fabs(v) >= 9223372036854775808.0) {
    throw std::runtime_error(std::format("{}: argument {} is too large", ctx.name, i + 1));
  }
  return static_cast<std::int64_t>(v);
}

FunctionDatabase makeBuiltin() {
  FunctionDatabase db;

  for (const auto &def : UNARY_FUNCTIONS) {
    db.add(def.name, [def](const FunctionCtx &ctx) { return unary(ctx, def.fn, def.policy); });
  }

  db.add("round", [](const FunctionCtx &ctx) {
    ctx.expectArgs(1, 2);
    const auto &n = ctx.number(0);
    const double digits = ctx.args.size() == 2 ? integer(ctx, 1).n.toDouble() : 0;
    return output(ctx, fromVisible(n, roundTo(visible(n), digits)), n, UnitPolicy::Keep);
  });

  db.add("log", [](const FunctionCtx &ctx) {
    ctx.expectArgs(1, 2);
    const auto &x = plain(ctx, 0);
    if (ctx.args.size() == 1) return output(ctx, std::log(x.n.toDouble()), x, UnitPolicy::Forbid);
    const auto &base = plain(ctx, 1);
    return output(ctx, std::log(x.n.toDouble()) / std::log(base.n.toDouble()), x, UnitPolicy::Forbid);
  });

  db.add("pow", [](const FunctionCtx &ctx) {
    ctx.expectArgs(2);
    const auto &x = plain(ctx, 0);
    const auto &y = plain(ctx, 1);
    return output(ctx, std::pow(x.n.toDouble(), y.n.toDouble()), x, UnitPolicy::Forbid);
  });

  db.add("root", [](const FunctionCtx &ctx) {
    ctx.expectArgs(2);
    const auto &x = plain(ctx, 0);
    const auto &n = plain(ctx, 1);
    auto xv = x.n.toDouble();
    auto nv = n.n.toDouble();
    // pow() has no answer for negative bases, but odd roots of them are well-defined
    auto isOdd = nv == std::trunc(nv) && std::fmod(nv, 2) != 0;
    auto r = xv < 0 && isOdd ? -std::pow(-xv, 1 / nv) : std::pow(xv, 1 / nv);
    return output(ctx, r, x, UnitPolicy::Forbid);
  });

  db.add("atan2", [](const FunctionCtx &ctx) {
    ctx.expectArgs(2);
    const auto &y = plain(ctx, 0);
    const auto &x = plain(ctx, 1);
    return output(ctx, std::atan2(y.n.toDouble(), x.n.toDouble()), y, UnitPolicy::Forbid);
  });

  db.add("fmod", [](const FunctionCtx &ctx) {
    ctx.expectArgs(2);
    const auto &x = plain(ctx, 0);
    const auto &y = plain(ctx, 1);
    return output(ctx, std::fmod(x.n.toDouble(), y.n.toDouble()), x, UnitPolicy::Forbid);
  });

  db.add("hypot", [](const FunctionCtx &ctx) {
    ctx.expectAtLeast(1);
    auto nn = ctx.numbers();
    double sum = 0;
    for (auto n : nn)
      sum += n->n.toDouble() * n->n.toDouble();
    return output(ctx, std::sqrt(sum), *nn.front(), UnitPolicy::Keep);
  });

  db.add("gcd", [](const FunctionCtx &ctx) {
    ctx.expectAtLeast(1);
    std::int64_t acc = 0;
    for (std::size_t i = 0; i < ctx.args.size(); ++i)
      acc = std::gcd(acc, toInt64(ctx, i));
    return output(ctx, static_cast<double>(acc), ctx.number(0), UnitPolicy::Forbid);
  });

  db.add("lcm", [](const FunctionCtx &ctx) {
    ctx.expectAtLeast(1);
    double acc = 1;
    for (std::size_t i = 0; i < ctx.args.size(); ++i) {
      auto v = toInt64(ctx, i);
      // past 2^63 the product stays in doubles so it shows as inf rather than wrapping
      auto a = static_cast<std::int64_t>(acc);
      if (v == 0 || acc == 0) {
        acc = 0;
      } else if (std::fabs(acc) < 9223372036854775808.0) {
        acc = static_cast<double>(std::lcm(a, v));
      } else {
        acc = acc / static_cast<double>(std::gcd(a, v)) * static_cast<double>(v);
      }
    }
    return output(ctx, std::fabs(acc), ctx.number(0), UnitPolicy::Forbid);
  });

  db.add("min", [](const FunctionCtx &ctx) { return extremum(ctx, std::less{}); });
  db.add("max", [](const FunctionCtx &ctx) { return extremum(ctx, std::greater{}); });

  db.add("sum", [](const FunctionCtx &ctx) {
    return fold(ctx, [](auto &&values) { return std::ranges::fold_left(values, 0.0, std::plus{}); });
  });

  for (auto name : {"avg", "mean", "average"}) {
    db.add(name, [](const FunctionCtx &ctx) {
      return fold(ctx, [&](auto &&values) {
        return std::ranges::fold_left(values, 0.0, std::plus{}) / static_cast<double>(ctx.args.size());
      });
    });
  }

  db.addConverter("upper", [](const FunctionCtx &ctx) {
    ctx.expectArgs(1);
    if (auto str = ctx.args.front().asStr()) {
      std::string out = *str;
      upperCase(out);
      return Computed{out};
    }
    throw std::runtime_error("Invalid type");
  });

  db.addConverter("lower", [](const FunctionCtx &ctx) {
    ctx.expectArgs(1);
    if (auto str = ctx.args.front().asStr()) {
      std::string out = *str;
      lowerCase(out);
      return Computed{out};
    }
    throw std::runtime_error("Invalid type");
  });

  db.addConverter("json", [](const FunctionCtx &ctx) {
    ctx.expectArgs(1);
    auto &arg = ctx.args[0];

    return std::visit(
        [&](const auto &value) -> Computed {
          using T = std::remove_cvref_t<decltype(value)>;

          if constexpr (std::is_same_v<T, DateTime>) {
            return Computed{value.toRFC3339()};
          } else if constexpr (std::is_same_v<T, Num>) {
            return Computed{value};
          } else {
            throw std::runtime_error(
                std::format("json() does not support the {} value type", arg.valueTypeName()));
          }
        },
        arg.value);
  });

  db.addConverter("weekday", [](const FunctionCtx &ctx) {
    ctx.expectAtLeast(1);
    const auto dt = ctx.dateTime(0);
    const auto displayTz = dt.tz ? dt.tz : tz::current_zone();
    // vendored date::zoned_time has no std::formatter (will hit the macOS path)
    const std::chrono::local_time localTime{displayTz->to_local(dt.time + dt.offset).time_since_epoch()};
    return Computed{.value = std::format("{:%A}", localTime)};
  });

  return db;
}

} // namespace

const FunctionDatabase &FunctionDatabase::builtin() {
  const static FunctionDatabase db = makeBuiltin();
  return db;
}

} // namespace numen::detail
