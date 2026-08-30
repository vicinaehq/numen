#include "region-currency.hpp"
#include "env.hpp"
#include "locale.hpp"
#include <algorithm>
#include <cctype>

namespace numen {

namespace {

struct RegionCurrency {
  std::string_view region;
  std::string_view currency;
};

#include "gen/region-currency-table.inc"

bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)); }
char toUpper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }

} // namespace

std::optional<std::string_view> currencyForRegion(std::string_view region) {
  if (region.size() != 2) return std::nullopt;

  const char key[2] = {toUpper(region[0]), toUpper(region[1])};
  auto it =
      std::ranges::lower_bound(kRegionCurrencyTable, std::string_view(key, 2), {}, &RegionCurrency::region);

  if (it != std::end(kRegionCurrencyTable) && it->region == std::string_view(key, 2)) return it->currency;

  return std::nullopt;
}

std::optional<std::string_view> currencyForLocale(std::string_view locale) {
  if (auto pos = locale.find_first_of(".@"); pos != std::string_view::npos) {
    locale = locale.substr(0, pos);
  }

  bool language = true;

  for (size_t start = 0; start <= locale.size();) {
    size_t end = locale.find_first_of("_-", start);
    if (end == std::string_view::npos) end = locale.size();
    auto tag = locale.substr(start, end - start);

    // the region subtag is the first 2-letter tag after the language;
    // 4-letter tags are scripts, 3-digit tags are UN M49 areas not present
    // in the table
    if (!language && tag.size() == 2 && isAlpha(tag[0]) && isAlpha(tag[1])) { return currencyForRegion(tag); }

    language = false;
    start = end + 1;
  }

  return std::nullopt;
}

std::string monetaryLocale() {
#ifndef _WIN32
  for (const auto *var : {"LC_ALL", "LC_MONETARY", "LANG"}) {
    if (auto v = getEnv(var); v && !v->empty()) return *std::move(v);
  }
#endif
  return systemLocaleName();
}

} // namespace numen
