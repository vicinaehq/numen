#pragma once
#include <string>

namespace numen {

// user default locale on Windows, LC_ALL/LANG on POSIX with a CFLocale
// fallback on Apple; empty when nothing is known
std::string systemLocaleName();

} // namespace numen
