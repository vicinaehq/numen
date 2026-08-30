#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace numen {

std::optional<std::string_view> currencyForRegion(std::string_view region);

/**
 * Resolve the tender currency (ISO 4217) for a POSIX or BCP 47 locale
 * string, e.g "fr_FR", "en-US", "fr_FR.UTF-8@euro". Locales without a
 * region subtag ("fr", "C", "POSIX") resolve to nothing.
 */
std::optional<std::string_view> currencyForLocale(std::string_view locale);

// LC_ALL, LC_MONETARY, LANG on POSIX, falling back to systemLocaleName()
std::string monetaryLocale();

} // namespace numen
