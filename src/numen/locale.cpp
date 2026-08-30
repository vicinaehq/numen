#include "locale.hpp"
#include "env.hpp"
#include "numen/numen.hpp"
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace numen {

namespace {

std::optional<std::locale> tryLocale(const std::string &name) {
  for (const auto &candidate : {name, name + ".UTF-8"}) {
    try {
      return std::locale{candidate};
    } catch (const std::runtime_error &) {} // NOLINT(bugprone-empty-catch)
  }
  return std::nullopt;
}

#ifndef _WIN32
std::string envLocaleName() {
  for (const auto *var : {"LC_ALL", "LANG"}) {
    if (auto v = getEnv(var); v && !v->empty()) return *std::move(v);
  }
  return {};
}
#endif

#ifdef __APPLE__
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// CFLocale identifiers are BCP 47-shaped ("zh-Hant_TW"), BSD locale names
// are not ("zh_TW")
std::string toPosixName(std::string_view ident) {
  if (auto pos = ident.find_first_of(".@"); pos != std::string_view::npos) ident = ident.substr(0, pos);

  std::string_view language;

  for (size_t start = 0; start <= ident.size();) {
    size_t end = ident.find_first_of("_-", start);
    if (end == std::string_view::npos) end = ident.size();
    auto tag = ident.substr(start, end - start);

    if (language.empty()) {
      language = tag;
    } else if (tag.size() == 2 && isAlpha(tag[0]) && isAlpha(tag[1])) {
      return std::string{language} + "_" + std::string{tag};
    }

    start = end + 1;
  }

  return std::string{ident};
}

std::string appleLocaleName() {
  std::string name;

  if (CFLocaleRef locale = CFLocaleCopyCurrent()) {
    if (CFStringRef ident = CFLocaleGetIdentifier(locale)) {
      char buf[128];
      if (CFStringGetCString(ident, buf, sizeof(buf), kCFStringEncodingUTF8)) name = buf;
    }
    CFRelease(locale);
  }

  return toPosixName(name);
}
#endif

} // namespace

std::string systemLocaleName() {
#if defined(_WIN32)
  // BCP 47 names ("en-US") are ASCII; len includes the null terminator, 0 on failure
  wchar_t name[LOCALE_NAME_MAX_LENGTH];
  const int len = GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH);
  std::string locale;
  for (int i = 0; i + 1 < len; ++i)
    locale += static_cast<char>(name[i]);
  return locale;
#elif defined(__APPLE__)
  if (auto env = envLocaleName(); !env.empty()) return env;
  return appleLocaleName();
#else
  return envLocaleName();
#endif
}

std::locale resolveLocale(const std::optional<std::string> &name) {
  if (name && !name->empty()) {
    if (auto loc = tryLocale(*name)) return *loc;
  }

  try {
    const std::locale env{""};
    // gui processes on macOS have no locale env vars, degrading "" to "C"
    if (env.name() != "C" && env.name() != "POSIX") return env;
  } catch (const std::runtime_error &) {} // NOLINT(bugprone-empty-catch)

  if (auto sys = systemLocaleName(); !sys.empty()) {
    if (auto loc = tryLocale(sys)) return *loc;
  }

  return std::locale::classic();
}

} // namespace numen
