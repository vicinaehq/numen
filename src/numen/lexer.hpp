#pragma once
#include "value.hpp"
#include <cctype>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>
#include <span>
#include <string_view>
#include <variant>

class Lexer {
public:
  struct Number {
    numen::Value n;
    unsigned fromBase = 10;
  };

  enum class OperatorType { Add, Subtract, Multiply, Divide, Pow };
  enum class State { Reset, Number, Operator, NumberBase, NumberExponentSign, NumberExponent, String };
  enum class TokenType { String, Number, Operator };

  struct String {
    std::string_view data;
  };
  struct Operator {
    std::string_view op;
  };

  using TokenData = std::variant<Number, String, Operator>;

  struct Token {
    std::string_view raw;
    TokenType type;
    TokenData data;
    std::string_view::size_type start = 0;
    std::string_view::size_type end = 0;

    bool isAdjacent(const Token &rhs) const { return end == rhs.start; }

    const Number *asNumber() const { return std::get_if<Number>(&data); }
    const Number *asNumber() { return std::get_if<Number>(&data); }
  };

  template <typename... Ts> std::optional<std::tuple<Ts...>> peakForward(std::size_t i = 0) {
    return [&]<std::size_t... I>(std::index_sequence<I...>) -> std::optional<std::tuple<Ts...>> {
      std::array<std::optional<Lexer::Token>, sizeof...(Ts)> p{peak(static_cast<int>(i + I))...};
      const bool ok =
          std::apply([](auto... q) { return (... && (q && std::holds_alternative<Ts>(q->data))); }, p);
      if (!ok) return std::nullopt;
      return std::tuple<Ts...>{std::get<Ts>(p[I]->data)...};
    }(std::index_sequence_for<Ts...>{});
  }

  Lexer(std::string_view data, const std::numpunct<char> &numpunct) : m_data(data), m_numpunct(numpunct) {}

  std::optional<Token> peakIf(TokenType type);
  std::optional<Token> peak(int n = 0);

  template <typename T> std::optional<T> peakAs() {
    if (auto tok = peak(); tok && std::holds_alternative<T>(tok->data)) { return std::get<T>(tok->data); }
    return std::nullopt;
  }

  Token peakOrThrow(std::string_view message, int n = 0) {
    auto tok = peak(n);
    if (!tok) throw std::runtime_error(std::string{message});
    return *tok;
  }
  // get as much string as we can and return that portion
  void advance(int n);
  std::optional<std::string_view> peakString(int n = 1);

  size_t cursor() const { return m_cursor; }
  void setCursor(size_t cursor) { m_cursor = cursor; }

  // Return a function that will restore the lexer cursor
  // to the position it was at when creating the checkpoint.
  auto checkpoint() {
    return [this, cursor = m_cursor]() { setCursor(cursor); };
  }

  bool isGluedLeft(const Token &tok) const {
    return tok.start > 0 && std::isspace(static_cast<unsigned char>(m_data[tok.start - 1])) == 0;
  }

  // e.g can check whether "to the power of" is next
  bool isWordSequence(std::span<const std::string_view> words);
  std::optional<Token> next();

private:
  std::string_view m_data;
  const std::numpunct<char> &m_numpunct;
  std::string_view::size_type m_cursor{0};
};
