#pragma once
#include <cstdint>
#include "numen/abstract-currency-provider.hpp"
#include <cassert>
#include <expected>
#include <string>
#include <vector>

struct Dimension {
  std::int8_t length = 0;
  std::int8_t mass = 0;
  std::int8_t time = 0;
  std::int8_t currency = 0;
  std::int8_t data = 0;
  std::int8_t temperature = 0;

  auto operator<=>(const Dimension &) const = default;
};

namespace dimensions {
inline constexpr const char *LENGTH = "length";
inline constexpr const char *MASS = "mass";
inline constexpr const char *DURATION = "duration";
inline constexpr const char *TEMPERATURE = "temperature";
inline constexpr const char *DATA = "data";
inline constexpr const char *VOLUME = "volume";
inline constexpr const char *AREA = "area";
inline constexpr const char *SPEED = "speed";
inline constexpr const char *FORCE = "force";
inline constexpr const char *PRESSURE = "pressure";
inline constexpr const char *ENERGY = "energy";
inline constexpr const char *POWER = "power";
inline constexpr const char *FREQUENCY = "frequency";
inline constexpr const char *CURRENCY = "currency";
} // namespace dimensions

// not about affine units, which are a property of the unit rather than its
// dimension: kelvin composes freely where celsius cannot compose at all
enum class Composition : std::uint8_t { Free, RateOnly };

struct DimensionTraits {
  // every unit factor must compose from these, which is why volume is based on
  // the cubic meter and not the litre
  Dimension signature;

  Composition composition = Composition::Free;

  // the table's factor is a placeholder, the real one is resolved per
  // evaluation. nothing may rank such a unit by size
  bool dynamicFactor = false;
};

DimensionTraits traitsOf(std::string_view name);

inline Dimension dimensionOf(std::string_view name) { return traitsOf(name).signature; }
inline Composition compositionOf(std::string_view name) { return traitsOf(name).composition; }

namespace families {
inline constexpr const char *METRIC = "metric";
inline constexpr const char *IMPERIAL = "imperial";
inline constexpr const char *TYPOGRAPHIC = "typographic";
inline constexpr const char *DURATION = "duration";
inline constexpr const char *DEGREE = "degree";
inline constexpr const char *SI = "si";
inline constexpr const char *DATA = "data";
inline constexpr const char *CURRENCY = "currency";
} // namespace families

struct UnitDef {
  std::string id;
  std::vector<std::string> aliases;
  // "km", "$". resolvable like any alias; empty leaves the id to stand for it
  std::string symbol;
  // CLDR spelling for currencies ("US Dollar"), else only where the id is no
  // readable word ("short ton")
  std::string name;
  // "$12.50", not "12.50 $"
  bool symbolPrefix = false;
  // meaningless for currency: exchange rates are resolved at runtime, keyed
  // by the currency dimension
  double factor = 1;
  std::string dimension;
  std::string family;
  double offset = 0;
  bool prefixable = false;
  // fixes the fraction a rendered amount may show, as money does: two for
  // usd, none for jpy. unset means the generic number formatting applies
  std::optional<int> decimals;

  // target to convert this unit to
  std::string implicitTarget;

  // a fixed price instead of a market rate: one unit is `factor` of this currency
  std::string peggedTo;
};

struct UnitTerm {
  UnitDef def;
  std::int8_t exponent = 1;
};

struct CompoundUnit {
  std::vector<UnitTerm> terms;

  Dimension dimension() const;

  // meaningless unless hasStableFactor()
  double factor() const;
  bool hasStableFactor() const;

  // conventional notation, however the unit was typed: "km²", "$/h"
  std::string render() const;

  // "square kilometer", "US Dollar per hour"
  std::string name() const;

  // the currency whose symbol leads the amount instead of trailing it: "$25/h"
  const UnitDef *leadingCurrency() const;

  // null once composition made this something no table entry names
  const UnitDef *sole() const;
};

CompoundUnit soleUnit(UnitDef def);

// a spelling that stands for a composition rather than a unit of its own, so it
// states no factor: that follows from the parts, as does what it renders as
struct CompoundAlias {
  std::string_view name;
  std::string_view composition;
};

class UnitDatabase {
public:
  UnitDatabase() noexcept;

  void registerUnit(UnitDef unit);

  // units can share a same identifier, e.g 'm' can stand for 'meter' or
  // 'minute'. a token no builtin answers to is last offered to the currency
  // provider, so any ticker it quotes a rate for resolves as a currency
  std::vector<UnitDef> findUnitCandidates(std::string_view q) const;

  std::optional<UnitDef> findUnit(const std::string &id) const;

  // the compound behind a token: a plain unit gives one term, an alias like
  // "kmh" gives its composition. this is what callers doing arithmetic want,
  // findUnitCandidates being the atomic-only view underneath it
  std::vector<CompoundUnit> findCompounds(std::string_view q) const;

  std::expected<double, std::string> factorOf(const UnitDef &unit) const;

  std::expected<double, std::string> convert(double n, const UnitDef &from, const UnitDef &to) const;

  std::expected<double, std::string> conversionRatio(const CompoundUnit &from, const CompoundUnit &to) const;

  void setCurrencyProvider(const numen::AbstractCurrencyProvider &provider) {
    m_currencyProvider = &provider;
  }

private:
  std::optional<CompoundUnit> expandComposition(std::string_view spec) const;
  std::vector<UnitDef> matchExact(std::string_view q) const;
  std::vector<UnitDef> expandPrefixed(std::string_view q) const;
  std::optional<UnitDef> providerCurrency(std::string_view q) const;
  std::expected<std::pair<UnitDef, double>, std::string> resolvePeg(const UnitDef &unit) const;

  std::vector<UnitDef> m_units;
  const numen::AbstractCurrencyProvider *m_currencyProvider = nullptr;
};
