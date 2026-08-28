#include "numen/numen.hpp"
#include "ast-printer.hpp"
#include "computed.hpp"
#include "dummy-currency-provider.hpp"
#include "interpreter.hpp"
#include "parser.hpp"
#include <exception>
#include <expected>
#include <iostream>
#include <locale>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace numen {
using namespace numen::detail;

std::string Number::toString() const {
  if (!unit) return text;
  if (!unit->resolved) return text + unit->raw;

  const auto &resolved = *unit->resolved;

  // "-$5", "$25/h": the symbol leads the amount, the minus leads the symbol
  if (auto currency = resolved.leadingCurrency()) {
    auto rate = resolved;
    std::erase_if(rate.terms, [](const UnitTerm &term) { return term.exponent > 0; });

    std::string_view amount{text};
    const bool negative = amount.starts_with('-');
    if (negative) amount.remove_prefix(1);

    return std::format("{}{}{}{}", negative ? "-" : "", currency->symbol, amount,
                       rate.terms.empty() ? std::string{} : rate.render());
  }

  return text + resolved.render();
}

std::string Boolean::toString() const { return value ? "true" : "false"; }

std::string ComputedValue::toString(const DateTimeFormatOptions &dateTimeFormat) const {
  return std::visit(
      [&](const auto &v) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(v)>, DateTime>) {
          return v.toString(dateTimeFormat);
        } else {
          return v.toString();
        }
      },
      value);
}

// a unit may fix how much of the fraction is shown, as money does: none for
// jpy, two for eur. only a sole unit carries that; "eur/hr" is a rate, not money
static std::optional<int> unitDecimals(const detail::Num &v) {
  if (v.unit && v.unit->def()) return v.unit->def()->decimals;
  return std::nullopt;
}

static ComputedValue toPublic(const detail::Computed &c, const NumberLocale &nloc) {
  auto out = [&](ValueType v) { return ComputedValue{.value = std::move(v), .conversion = c.conversion}; };

  if (auto n = c.asNumber()) {
    return out(Number{.n = n->n.toDouble(),
                      .text = n->n.render(n->format, unitDecimals(*n), nloc),
                      .format = n->format,
                      .unit = n->unit,
                      .isPercentage = n->isPercentage});
  }
  if (auto d = c.asDateTime()) return out(*d);
  if (auto d = c.asDuration()) return out(*d);
  return out(std::get<Boolean>(c.value));
}

std::expected<ComputedValue, std::string> Numen::compute(std::string_view expr, const EvalOptions &opts) {
  try {
    Parser parser{expr, m_unitDb, opts.parseOptions};
    auto ast = parser.parse();

    return toPublic(interpret(*ast.root, m_unitDb, opts),
                    NumberLocale::from(opts.parseOptions.effectiveLocale()));
  } catch (const std::exception &e) { return std::unexpected(e.what()); }
}

std::expected<std::string, std::string> Numen::evaluate(const std::string_view expr,
                                                        const EvalOptions &opts) {
  return compute(expr, opts).transform([&](const ComputedValue &v) {
    return v.toString(opts.effectiveDateTimeFormat());
  });
}

void Numen::printAST(const std::string &expr) const {
  Parser parser{expr, m_unitDb, {}};
  auto ast = parser.parse();

  detail::printAST(std::cout, *ast.root);
}

Numen::Numen() { setCurrencyProvider(std::make_unique<DummyCurrencyProvider>()); }

} // namespace numen
