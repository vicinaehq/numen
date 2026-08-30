#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include "nlohmann/json.hpp"
#include "http-get.hpp"
#include "numen/abstract-currency-provider.hpp"
#include "numen/env.hpp"
#include "vicinae-currency-provider.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
constexpr auto CACHE_TTL = std::chrono::minutes{30};

void persistOnDisk(const std::filesystem::path &path, std::string_view body) {
  fs::create_directories(path.parent_path());
  std::ofstream{path} << body;
}

fs::path persistPath() {
#ifdef _WIN32
  auto cacheHome = numen::getEnv("LOCALAPPDATA")
                       .transform([](auto &&s) { return fs::path{s}; })
                       .value_or(fs::path{"."});
#else
  auto cacheHome = numen::getEnv("XDG_CACHE_HOME")
                       .transform([](auto &&s) { return fs::path{s}; })
                       .value_or(fs::path{numen::getEnv("HOME").value_or(".")} / ".cache");
#endif
  return cacheHome / "libnumen" / "vicinae-rates.json";
}

std::string normalizeCurrencyId(std::string_view id) {
  std::string s{id};
  std::ranges::transform(id, s.begin(), [](char c) { return std::tolower(c); });
  return s;
}

} // namespace

std::optional<numen::ExchangeRate> VicinaeCurrencyProvider::getRate(std::string_view code) const {
  {
    const std::scoped_lock lock{m_mut};
    if (auto it = m_rates.find(std::string{code}); it != m_rates.end()) {
      return numen::ExchangeRate{.rate = it->second};
    }
    if (code.starts_with("$")) return getRate(code.substr(1));
  }
  return std::nullopt;
}

bool VicinaeCurrencyProvider::loadRates(std::string_view payload) {
  try {
    auto obj = json::parse(payload);

    {
      const std::scoped_lock lock{m_mut};
      for (const auto &[k, v] : obj["crypto"]["prices"].get<json::object_t>()) {
        m_rates[normalizeCurrencyId(k)] = 1 / v.get<double>();
      }
      // fiat always take priority over crypto tickers for nowk
      for (const auto &[k, v] : obj["fiat"]["rates"].get<json::object_t>()) {
        m_rates[normalizeCurrencyId(k)] = v.get<double>();
      }

      return true;
    }
  } catch (const std::exception &e) {
    std::cerr << "VicinaeCurrencyProvider: failed to fetch rates: " << e.what() << "\n" << payload << "\n";
    return false;
  }
}

void VicinaeCurrencyProvider::fetchRates() {
  std::error_code ec;
  auto path = persistPath();

  if (!m_lastFetchedAt && fs::is_regular_file(path, ec)) {
    if (std::chrono::file_clock::now() - fs::last_write_time(path) < CACHE_TTL) {
      const std::ifstream ifs{path};
      std::stringstream buffer;
      buffer << ifs.rdbuf();
      loadRates(buffer.str());
      return;
    }
  }

  {
    const std::scoped_lock lock{m_mut};
    if (m_lastFetchedAt && std::chrono::system_clock::now() - *m_lastFetchedAt < CACHE_TTL) return;
  }

  if (m_worker.joinable()) m_worker.join(); // a previous fetch still in flight: let it land first

  m_worker = std::jthread{[this]() {
    auto res = httpGet("https://api.vicinae.com", "/v1/currencies");

    if (!res) {
      std::cerr << "VicinaeCurrencyProvider: failed to fetch rates: " << res.error() << "\n";
      return;
    }

    if (loadRates(*res)) {
      {
        const std::scoped_lock lock{m_mut};
        m_lastFetchedAt = std::chrono::system_clock::now();
      }
      persistOnDisk(persistPath(), *res);
    }
  }};
}
