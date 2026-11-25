#pragma once

#include "computed.hpp"
#include <cstddef>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace numen::detail {

struct FunctionCtx {
  std::string_view name;
  std::span<const Computed> args;

  const Num &number(std::size_t i) const;
  const DateTime &dateTime(std::size_t i) const;
  std::vector<const Num *> numbers() const;

  void expectArgs(std::size_t n) const;
  void expectArgs(std::size_t min, std::size_t max) const;
  void expectAtLeast(std::size_t n) const;
};

using FunctionHandler = std::function<Computed(const FunctionCtx &)>;

class FunctionDatabase {
public:
  struct FunctionOptions {
    bool caseSensitive = true;
  };

  static const FunctionDatabase &builtin();

  void add(std::string_view name, FunctionHandler handler, FunctionOptions options);
  void add(std::string_view name, FunctionHandler handler, bool isConverter = false);
  void addConverter(std::string_view name, FunctionHandler handler);
  const FunctionHandler *find(std::string_view name) const;
  const std::vector<std::string> &converterNames() const;

private:
  struct Entry {
    std::string_view name;
    FunctionHandler fn;
    FunctionOptions options;
  };

  std::vector<Entry> m_fns;
  std::vector<std::string> m_convNames;
};

} // namespace numen::detail
