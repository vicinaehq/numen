#include "numen/unit.hpp"
#include "numen/abstract-currency-provider.hpp"
#include "builtin-units.hpp"
#include "utils.hpp"
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <algorithm>
#include <iostream>
#include <ranges>

namespace {

// a currency only the provider knows about states no minor units of its own,
// and most of those are crypto, so it gets satoshi precision
constexpr int PROVIDER_CURRENCY_DECIMALS = 8;

struct Prefix {
  // a prefixed unit takes its name from the long spelling, its symbol from
  // the short one
  std::string_view name;
  std::string_view symbol;
  double multiplier;
  // the micro sign also has an ascii stand-in and a greek look-alike
  std::array<std::string_view, 2> alt = {};
  // 'm'/'M' (milli/mega) and 'p'/'P' (pico/peta) only differ by case
  bool caseSensitive = false;
  // binary prefixes only make sense for data units
  bool dataOnly = false;

  std::optional<std::string_view> leading(std::string_view q) const {
    auto head = [&](std::string_view spelling) { return q.substr(0, spelling.size()); };
    if (equalsIgnoreCase(head(name), name)) return name;
    if (caseSensitive ? head(symbol) == symbol : equalsIgnoreCase(head(symbol), symbol)) return symbol;
    for (auto spelling : alt) {
      if (!spelling.empty() && head(spelling) == spelling) return spelling;
    }
    return std::nullopt;
  }
};

// declaration order is irrelevant: expansion collects candidates from
// every matching prefix, first-match-wins semantics must not be assumed
constexpr auto PREFIXES = std::to_array<Prefix>({
    {.name = "nano", .symbol = "n", .multiplier = 1e-9},
    {.name = "micro", .symbol = "µ", .multiplier = 1e-6, .alt = {"u", "μ"}},
    {.name = "milli", .symbol = "m", .multiplier = 1e-3, .caseSensitive = true},
    {.name = "centi", .symbol = "c", .multiplier = 1e-2},
    {.name = "deci", .symbol = "d", .multiplier = 1e-1},
    {.name = "kilo", .symbol = "k", .multiplier = 1e3},
    {.name = "mega", .symbol = "M", .multiplier = 1e6, .caseSensitive = true},
    {.name = "giga", .symbol = "G", .multiplier = 1e9},
    {.name = "tera", .symbol = "T", .multiplier = 1e12},
    {.name = "pico", .symbol = "p", .multiplier = 1e-12, .caseSensitive = true},
    {.name = "peta", .symbol = "P", .multiplier = 1e15, .caseSensitive = true},
    {.name = "kibi", .symbol = "Ki", .multiplier = 1024.0, .dataOnly = true},
    {.name = "mebi", .symbol = "Mi", .multiplier = 1048576.0, .dataOnly = true},
    {.name = "gibi", .symbol = "Gi", .multiplier = 1073741824.0, .dataOnly = true},
    {.name = "tebi", .symbol = "Ti", .multiplier = 1099511627776.0, .dataOnly = true},
});

std::string symbolOf(const UnitDef &def) { return def.symbol.empty() ? def.id : def.symbol; }
std::string nameOf(const UnitDef &def) { return def.name.empty() ? def.id : def.name; }

// "meters" -> "meter", "inches" -> "inch". Only ever consulted for tokens
// with no direct reading: a token that is a unit name by itself ("ms",
// "min") must never be reinterpreted as a plural.
std::vector<std::string_view> pluralForms(std::string_view q) {
  std::vector<std::string_view> forms;
  if (q.size() > 1 && (q.back() == 's' || q.back() == 'S')) {
    forms.push_back(q.substr(0, q.size() - 1));
    if (q.size() > 2 && equalsIgnoreCase(q.substr(q.size() - 2), std::string_view{"es"})) {
      forms.push_back(q.substr(0, q.size() - 2));
    }
  }
  return forms;
}

} // namespace

UnitDatabase::UnitDatabase() noexcept {
  for (const auto &unit : units::builtins()) {
    registerUnit(unit);
  }
}

DimensionTraits traitsOf(std::string_view name) {
  if (name == dimensions::LENGTH) return {.signature = {.length = 1}};
  if (name == dimensions::MASS) return {.signature = {.mass = 1}};
  if (name == dimensions::DURATION) return {.signature = {.time = 1}};
  if (name == dimensions::DATA) return {.signature = {.data = 1}};
  if (name == dimensions::TEMPERATURE) return {.signature = {.temperature = 1}};
  if (name == dimensions::AREA) return {.signature = {.length = 2}};
  if (name == dimensions::VOLUME) return {.signature = {.length = 3}};
  if (name == dimensions::SPEED) return {.signature = {.length = 1, .time = -1}};
  if (name == dimensions::FORCE) return {.signature = {.length = 1, .mass = 1, .time = -2}};
  if (name == dimensions::PRESSURE) return {.signature = {.length = -1, .mass = 1, .time = -2}};
  if (name == dimensions::ENERGY) return {.signature = {.length = 2, .mass = 1, .time = -2}};
  if (name == dimensions::POWER) return {.signature = {.length = 2, .mass = 1, .time = -3}};
  if (name == dimensions::FREQUENCY) return {.signature = {.time = -1}};

  if (name == dimensions::CURRENCY) {
    return {.signature = {.currency = 1}, .composition = Composition::RateOnly, .dynamicFactor = true};
  }

  return {};
}

Dimension CompoundUnit::dimension() const {
  Dimension d;

  for (const auto &term : terms) {
    auto base = dimensionOf(term.def.dimension);
    d.length = static_cast<std::int8_t>(d.length + base.length * term.exponent);
    d.mass = static_cast<std::int8_t>(d.mass + base.mass * term.exponent);
    d.time = static_cast<std::int8_t>(d.time + base.time * term.exponent);
    d.currency = static_cast<std::int8_t>(d.currency + base.currency * term.exponent);
    d.data = static_cast<std::int8_t>(d.data + base.data * term.exponent);
    d.temperature = static_cast<std::int8_t>(d.temperature + base.temperature * term.exponent);
  }

  return d;
}

double CompoundUnit::factor() const {
  double f = 1;
  for (const auto &term : terms) {
    f *= std::pow(term.def.factor, term.exponent);
  }
  return f;
}

bool CompoundUnit::hasStableFactor() const {
  return std::ranges::none_of(
      terms, [](const UnitTerm &term) { return traitsOf(term.def.dimension).dynamicFactor; });
}

std::string CompoundUnit::render() const {
  auto name = [&](const UnitTerm &term) {
    auto base = symbolOf(term.def);
    auto exponent = std::abs(term.exponent);
    if (exponent == 1) return base;
    if (exponent == 2) return base + "²";
    if (exponent == 3) return base + "³";
    return std::format("{}^{}", base, exponent);
  };

  std::vector<std::string> over, under;

  for (const auto &term : terms) {
    if (term.exponent > 0) { over.push_back(name(term)); }
    if (term.exponent < 0) { under.push_back(name(term)); }
  }

  auto join = [](const std::vector<std::string> &parts) {
    std::string out;
    for (const auto &part : parts) {
      if (!out.empty()) out += "·";
      out += part;
    }
    return out;
  };

  if (under.empty()) return join(over);
  // "usd/person·day" would read as (usd/person)·day
  if (under.size() > 1) return std::format("{}/({})", join(over), join(under));

  return std::format("{}/{}", join(over), under.front());
}

std::string CompoundUnit::name() const {
  auto name = [](const UnitTerm &term) {
    auto base = nameOf(term.def);
    auto exponent = std::abs(term.exponent);
    if (exponent == 1) return base;
    if (exponent == 2) return "square " + base;
    if (exponent == 3) return "cubic " + base;
    return std::format("{}^{}", base, exponent);
  };

  std::string out;
  for (const auto &term : terms) {
    if (term.exponent < 0) continue;
    if (!out.empty()) out += " ";
    out += name(term);
  }
  for (const auto &term : terms) {
    if (term.exponent > 0) continue;
    if (!out.empty()) out += " ";
    out += "per " + name(term);
  }
  return out;
}

const UnitDef *CompoundUnit::leadingCurrency() const {
  const UnitDef *currency = nullptr;

  for (const auto &term : terms) {
    if (term.exponent < 0) continue;
    // a second numerator, or a power, is no longer an amount of money
    if (currency || term.exponent != 1) return nullptr;
    if (!term.def.symbolPrefix || term.def.symbol.empty()) return nullptr;
    currency = &term.def;
  }

  return currency;
}

const UnitDef *CompoundUnit::sole() const {
  if (terms.size() != 1 || terms.front().exponent != 1) return nullptr;
  return &terms.front().def;
}

CompoundUnit soleUnit(UnitDef def) { return CompoundUnit{{UnitTerm{.def = std::move(def)}}}; }

std::vector<UnitDef> UnitDatabase::matchExact(std::string_view q) const {
  // a unit with no symbol would otherwise match the empty name
  if (q.empty()) return {};

  return m_units | std::views::filter([&](const UnitDef &unit) {
           return equalsIgnoreCase(unit.id, q) || unit.symbol == q ||
                  std::ranges::any_of(unit.aliases, [&](auto &&str) { return equalsIgnoreCase(str, q); });
         }) |
         std::ranges::to<std::vector>();
}

std::vector<UnitDef> UnitDatabase::expandPrefixed(std::string_view q) const {
  std::vector<UnitDef> out;

  for (const auto &prefix : PREFIXES) {
    auto spelling = prefix.leading(q);
    if (!spelling || q.size() <= spelling->size()) { continue; }

    // exact-only: a plural remainder ("mins" as milli + "ins" -> inch) must
    // lose to the plural pass on the whole token
    auto rest = q.substr(spelling->size());
    auto bases = matchExact(rest);

    for (const auto &base : bases) {
      if (!base.prefixable || base.offset != 0) { continue; }
      if (prefix.dataOnly && base.dimension != dimensions::DATA) { continue; }

      out.push_back(UnitDef{
          .id = std::string{q},
          .symbol = std::string{prefix.symbol} + symbolOf(base),
          .name = std::string{prefix.name} + nameOf(base),
          .factor = prefix.multiplier * base.factor,
          .dimension = base.dimension,
          .family = base.family,
      });
    }
  }

  return out;
}

std::vector<UnitDef> UnitDatabase::findUnitCandidates(std::string_view q) const {
  if (auto units = matchExact(q); !units.empty()) { return units; }
  if (auto units = expandPrefixed(q); !units.empty()) { return units; }

  for (auto form : pluralForms(q)) {
    if (auto units = matchExact(form); !units.empty()) { return units; }
    if (auto units = expandPrefixed(form); !units.empty()) { return units; }
  }

  if (auto unit = providerCurrency(q)) { return {*unit}; }

  return {};
}

// a ticker the provider quotes a rate for is a currency too, even with no
// builtin behind it. this comes last so no ticker can ever shadow a real unit
std::optional<UnitDef> UnitDatabase::providerCurrency(std::string_view q) const {
  if (!m_currencyProvider || q.empty()) return std::nullopt;

  std::string code{q};
  std::ranges::transform(code, code.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (!m_currencyProvider->getRate(code)) return std::nullopt;

  std::string ticker = code;
  std::ranges::transform(ticker, ticker.begin(),
                         [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

  return UnitDef{
      .id = code,
      .symbol = ticker,
      .name = ticker,
      .dimension = dimensions::CURRENCY,
      .family = families::CURRENCY,
      .decimals = PROVIDER_CURRENCY_DECIMALS,
  };
}

void UnitDatabase::registerUnit(UnitDef unit) { m_units.emplace_back(std::move(unit)); }

// "km/h" or "m^2": terms separated by / and *, each optionally raised to a
// power. only ever fed the built-in alias table, so a failure is a table bug
std::optional<CompoundUnit> UnitDatabase::expandComposition(std::string_view spec) const {
  CompoundUnit out;
  std::int8_t sign = 1;

  for (std::size_t i = 0; i <= spec.size();) {
    auto end = spec.find_first_of("/*", i);
    auto token = spec.substr(i, end == std::string_view::npos ? end : end - i);

    std::int8_t exponent = 1;
    if (auto caret = token.find('^'); caret != std::string_view::npos) {
      exponent = static_cast<std::int8_t>(token[caret + 1] - '0');
      token = token.substr(0, caret);
    }

    // the atomic view, so a composition can never refer to another alias
    auto candidates = findUnitCandidates(token);
    if (candidates.empty()) return std::nullopt;

    out.terms.push_back(
        UnitTerm{.def = candidates.front(), .exponent = static_cast<std::int8_t>(exponent * sign)});

    if (end == std::string_view::npos) break;
    sign = spec[end] == '/' ? -1 : 1;
    i = end + 1;
  }

  return out;
}

std::vector<CompoundUnit> UnitDatabase::findCompounds(std::string_view q) const {
  // the spelling render() produces: "m²" is m^2
  for (auto [suffix, exponent] : {std::pair{std::string_view{"²"}, 2}, {std::string_view{"³"}, 3}}) {
    if (!q.ends_with(suffix)) continue;

    auto compounds = findCompounds(q.substr(0, q.size() - suffix.size()));
    for (auto &compound : compounds) {
      for (auto &term : compound.terms) {
        term.exponent = static_cast<std::int8_t>(term.exponent * exponent);
      }
    }
    return compounds;
  }

  for (const auto &alias : units::compoundAliases()) {
    if (!equalsIgnoreCase(alias.name, q)) continue;
    if (auto expanded = expandComposition(alias.composition)) return {*expanded};
  }

  return findUnitCandidates(q) | std::views::transform([&](const UnitDef &def) { return soleUnit(def); }) |
         std::ranges::to<std::vector>();
}

std::optional<UnitDef> UnitDatabase::findUnit(const std::string &id) const {
  if (auto units = findUnitCandidates(id); !units.empty()) { return units.front(); }
  return std::nullopt;
}

std::expected<double, std::string> UnitDatabase::factorOf(const UnitDef &unit) const {
  if (!traitsOf(unit.dimension).dynamicFactor) return unit.factor;

  if (!m_currencyProvider) return std::unexpected("No currency provider is configured");

  // a rate is how many of this unit one base unit buys: the reciprocal of a factor
  auto rate = m_currencyProvider->getRate(unit.id);
  if (!rate || rate->rate == 0) {
    return std::unexpected(std::format("No conversion rate available for {}.", unit.id));
  }

  return 1 / rate->rate;
}

std::expected<double, std::string> UnitDatabase::conversionRatio(const CompoundUnit &from,
                                                                 const CompoundUnit &to) const {
  std::map<std::string, std::pair<UnitDef, int>> net;

  auto accumulate = [&](const CompoundUnit &unit, int sign) {
    for (const auto &term : unit.terms) {
      auto &[def, exponent] = net[term.def.id];
      def = term.def;
      exponent += term.exponent * sign;
    }
  };

  accumulate(from, 1);
  accumulate(to, -1);

  double ratio = 1;

  for (const auto &[id, entry] : net) {
    const auto &[def, exponent] = entry;
    // the same unit on both sides cancels, so it never needs a rate looked up
    if (exponent == 0) continue;

    auto base = factorOf(def);
    if (!base) return std::unexpected(base.error());

    ratio *= std::pow(*base, exponent);
  }

  return ratio;
}

std::expected<double, std::string> UnitDatabase::convert(double n, const UnitDef &from,
                                                         const UnitDef &to) const {
  if (from.id == to.id) { return n; }

  if (from.dimension != to.dimension) {
    return std::unexpected(
        std::format("No idea how to convert {} to {}, as they are not of the same type.", from.id, to.id));
  }

  auto fromFactor = factorOf(from);
  if (!fromFactor) return std::unexpected(fromFactor.error());

  auto toFactor = factorOf(to);
  if (!toFactor) return std::unexpected(toFactor.error());

  auto base = n * *fromFactor + from.offset;
  return (base - to.offset) / *toFactor;
}
