#include "ast-printer.hpp"
#include "datetime.hpp"
#include "rang/rang.hpp"
#include <chrono>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace numen::detail {

static void printASTNode(std::ostream &os, const Expression &expr, int depth = 0) {
  auto ident = [&]() {
    std::string s;
    for (int i = 0; i != depth; ++i)
      s += "  ";
    return s;
  };

  std::visit(
      [&](const auto &value) {
        using T = std::remove_cvref_t<decltype(value)>;

        if constexpr (std::is_same_v<T, BinaryExpression>) {
          os << ident() << "Binary " << rang::fg::green << value.op << rang::fg::reset << " {\n";
          printASTNode(os, *value.lhs, depth + 1);
          printASTNode(os, *value.rhs, depth + 1);
          os << ident() << "}\n";
        } else if constexpr (std::is_same_v<T, UnaryExpression>) {
          os << ident() << "Unary " << rang::fg::green << value.op << rang::fg::reset << " {\n";
          printASTNode(os, *value.lhs, depth + 1);
          os << ident() << "}\n";
        } else if constexpr (std::is_same_v<T, ConversionExpression>) {
          [[maybe_unused]] auto visitor = [](const auto &target) -> std::string {
            using V = std::remove_cvref_t<decltype(target)>;
            if constexpr (std::is_same_v<V, TimezoneOffset>) {
              return std::format("Timezone({})", target.name);
            } else if constexpr (std::is_same_v<V, NamedUnit>) {
              std::string name;
              for (const auto &term : target.terms) {
                if (!name.empty()) name += term.exponent < 0 ? "/" : "*";
                name += term.name;
              }
              return std::format("Unit({})", name);
            } else {
              static_assert(std::is_same_v<V, NamedNumberFormat>);
              return std::format("NumericFormat({})", target.name);
            }
          };

          /*
  os << ident() << "Convert " << rang::fg::green << std::visit(visitor, value.target)
     << rang::fg::reset << " {\n";
                 */

          printASTNode(os, *value.lhs, depth + 1);
          os << ident() << "}\n";
        } else if constexpr (std::is_same_v<T, UnitExpression>) {
          os << ident() << "Unit " << rang::fg::green << value.unit.simpleName() << rang::fg::reset << " {\n";
          printASTNode(os, *value.expr, depth + 1);
          os << ident() << "}\n";
        } else if constexpr (std::is_same_v<T, DateString>) {
          os << ident() << "Date " << " {\n";

          if (auto str = std::get_if<std::string_view>(&value.value)) {
            os << ident() << "\tvalue " << *str << "\n";
          }
          if (auto str = std::get_if<DateTimeLiteral>(&value.value)) {
            os << ident() << "\tvalue "
               << parseDateTime({.value = *str, .timezone = value.timezone}, *tz::current_zone(),
                                std::chrono::system_clock::now())
                      .toString()
               << "\n";
          }

          if (value.timezone) { os << ident() << "\ttimezone " << value.timezone->name << "\n"; }

          os << ident() << "}\n";
        } else if constexpr (std::is_same_v<T, NumberString>) {
          os << ident() << "Num " << rang::fg::yellow << value.render() << rang::fg::reset << "\n";
        } else if constexpr (std::is_same_v<T, FunctionCall>) {
          os << ident() << "Fn " << rang::fg::green << value.name << rang::fg::reset << " {\n";
          for (const auto &arg : value.args) {
            printASTNode(os, *arg, depth + 1);
          }
          os << ident() << "}\n";
        } else if constexpr (std::is_same_v<T, UntilExpression>) {
          os << ident() << "Until " << rang::fg::green
              << (value.unit ? value.unit->simpleName() : std::string_view{"time"})
              << rang::fg::reset << " {\n";
          printASTNode(os, *value.target, depth + 1);
          os << ident() << "}\n";
        } else if constexpr (std::is_same_v<T, Duration>) {
          os << ident() << "Duration " << rang::fg::green << value.total() << rang::fg::reset << "\n";
        }
      },
      expr.data);
}

void printAST(std::ostream &os, const Expression &expr) { printASTNode(os, expr, 0); }

} // namespace numen::detail
