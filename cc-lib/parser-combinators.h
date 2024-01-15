/* Parser combinators by Tom 7.
   I got fed up! */

#ifndef _CC_LIB_PARSE_H
#define _CC_LIB_PARSE_H

#include <span>
#include <variant>
#include <optional>
#include <functional>
#include <concepts>
#include <utility>

#include "base/logging.h"

struct Unit { };

// Like std::optional<T>, but with the length of the match
// (number of tokens at the start of the span).
template<class T>
struct Parsed {
  constexpr Parsed() {}
  static constexpr Parsed<T> None = Parsed();
  Parsed(T t, size_t length) : ot_(t), length_(length) {}
  bool HasValue() const { return ot_.has_value(); }
  const T &Value() const { return ot_.value(); }
  size_t Length() const { return length_; }
private:
  std::optional<T> ot_;
  size_t length_ = 0;
};

template <typename P>
concept Parser = requires(P p, std::span<const typename P::token_type> toks) {
  typename P::token_type;
  typename P::out_type;
  { p(toks) } -> std::convertible_to<Parsed<typename P::out_type>>;
};

template <typename Token, typename Out>
struct ParserWrapper {
  template <typename F>
  ParserWrapper(F&& f) : func(f) {}

  Parsed<Out> operator ()(std::span<const Token> t) const {
    return func(t);
  }

  using token_type = Token;
  using out_type = Out;

 private:
  std::function<Parsed<Out>(std::span<const Token>)> func;
};

static_assert(Parser<ParserWrapper<char, double>>);

// Always fails to parse.
template<class Token, class Out>
struct Fail {
  using token_type = Token;
  using out_type = Out;
  Parsed<Out> operator ()(std::span<const Token> unused_) const {
    return Parsed<Out>::None;
  }
};

template<class Token, class Out>
struct Succeed {
  using token_type = Token;
  using out_type = Out;
  explicit Succeed(Out r) : r(r) {}
  Parsed<Out> operator ()(std::span<const Token> unused_) const {
    // consuming no input
    return Parsed(r, 0);
  }
  const Out r;
};

template<class Token>
struct End {
  using token_type = Token;
  using out_type = Unit;
  explicit End() {}
  Parsed<Unit> operator ()(std::span<const Token> toks) const {
    if (toks.empty()) return Parsed(Unit{}, 0);
    return Parsed<Unit>::None;
  }
};

// Matches any token; returns it.
template<class Token>
struct Any {
  using token_type = Token;
  using out_type = Token;
  explicit Any() {}
  Parsed<Token> operator ()(std::span<const Token> toks) const {
    if (toks.empty()) return Parsed<Token>::None;
    return Parsed(toks[0], 1);
  }
};

// Matches only the argument token.
template<class Token>
struct Is {
  using token_type = Token;
  using out_type = Token;
  explicit Is(Token r) : r(r) {}
  Parsed<Token> operator ()(std::span<const Token> toks) const {
    if (toks.empty()) return Parsed<Token>::None;
    if (toks[0] == r) return Parsed(toks[0], 1);
    else return Parsed<Token>::None;
  }
  const Token r;
};

// Expands the length of a parsed result forward by the given amount.
// If the parse failed, also fails.
template<class Out>
Parsed<Out> ExpandLength(Parsed<Out> o, size_t len) {
  if (o.HasValue()) {
    return Parsed<Out>(o.Value(), o.Length() + len);
  } else {
    return o;
  }
}

// A >> B
// parses A then B, and fails if either fails.
// Both must have the same input type.
// If successful, the result is the result of B.
template<Parser A, Parser B>
requires std::same_as<typename A::token_type,
                      typename B::token_type>
inline auto operator >>(const A &a, const B &b) {
  using in = A::token_type;
  using out = B::out_type;
  return ParserWrapper<in, out>(
      [a, b](std::span<const in> toks) -> Parsed<out> {
        auto o1 = a(toks);
        if (!o1.HasValue()) return Parsed<out>::None;
        const size_t start = o1.Length();
        auto o2 = b(toks.subspan(start));
        return ExpandLength(o2, start);
      });
}

// A << B
// As above, but the result on success is the result of A.
template<Parser A, Parser B>
requires std::same_as<typename A::token_type,
                      typename B::token_type>
inline auto operator <<(const A &a, const B &b) {
  using in = A::token_type;
  using out = A::out_type;
  return ParserWrapper<in, out>(
      [a, b](std::span<const in> toks) -> Parsed<out> {
        auto o1 = a(toks);
        if (!o1.HasValue()) return Parsed<out>::None;
        const size_t start = o1.Length();
        auto o2 = b(toks.subspan(start));
        if (!o2.HasValue()) return Parsed<out>::None;
        return ExpandLength(o1, o2.Length());
      });
}

template<Parser A, Parser B>
requires std::same_as<typename A::token_type,
                      typename B::token_type>
inline auto operator &&(const A &a,
                        const B &b) {
  using in = A::token_type;
  using out1 = A::out_type;
  using out2 = B::out_type;
  return ParserWrapper<in, std::pair<out1, out2>>(
      [a, b](std::span<const in> toks) ->
      Parsed<std::pair<out1, out2>> {
        auto o1 = a(toks);
        if (!o1.HasValue())
          return Parsed<std::pair<out1, out2>>::None;
        const size_t start = o1.Length();
        auto o2 = b(toks.subspan(start));
        if (!o2.HasValue())
          return Parsed<std::pair<out1, out2>>::None;
        return Parsed<std::pair<out1, out2>>(
            std::make_pair(o1.Value(), o2.Value()),
            start + o2.Length());
      });
}

template<Parser A, Parser B>
requires std::same_as<typename A::token_type,
                      typename B::token_type> &&
         std::same_as<typename A::out_type,
                      typename B::out_type>
inline auto operator ||(const A &a, const B &b) {
  using in = A::token_type;
  using out = A::out_type;
  return ParserWrapper<in, out>(
      [a, b](std::span<const in> toks) ->
      Parsed<out> {
        auto o1 = a(toks);
        if (o1.HasValue()) {
          return o1;
        } else {
          return b(toks);
        }
      });
}

// One or zero. Always succeeds with std::optional<>.
template<Parser A>
inline auto Opt(const A &a) {
  using in = A::token_type;
  using out = std::optional<typename A::out_type>;
  return ParserWrapper<in, out>(
      [a](std::span<const in> toks) ->
      Parsed<out> {
        auto o = a(toks);
        if (!o.HasValue())
          return Parsed<out>(std::nullopt, 0);
        return Parsed<out>(
            std::make_optional(o.Value()),
            o.Length());
      });
}

// Zero or more times.
// If A accepts the empty sequence, this will
// loop forever.
template<Parser A>
inline auto operator *(const A &a) {
  using in = A::token_type;
  using out = std::vector<typename A::out_type>;
  return ParserWrapper<in, out>(
      [a](std::span<const in> toks) ->
      Parsed<out> {
        std::vector<typename A::out_type> ret;
        size_t total_len = 0;
        for (;;) {
          auto o = a(toks);
          if (!o.HasValue())
            return Parsed<out>(ret, total_len);
          const size_t one_len = o.Length();
          total_len += one_len;
          ret.push_back(o.Value());
          toks = toks.subspan(one_len);
        }
      });
}

// A >f
// Maps the function f to the result of A if
// it was successful.
template<Parser A, class F>
requires std::invocable<F, typename A::out_type>
inline auto operator >(const A &a, const F &f) {
  using in = A::token_type;
  using a_out = A::out_type;
  // Get the result type of applying f to the result of a.
  using out = decltype(f(std::declval<a_out>()));
  return ParserWrapper<in, out>(
      [a, f](std::span<const in> toks) ->
      Parsed<out> {
        auto o = a(toks);
        if (!o.HasValue()) return Parsed<out>::None;
        else return Parsed<out>(f(o.Value()), o.Length());
      });
}

#if 0
// A >=f
// Calls f on the result of A, whether successful or not.
template<Parser A, class F>
requires std::invocable<F, Parsed<typename A::out_type>>
inline auto operator >=(const A &a, const F &f) {
  using in = A::token_type;
  using a_out = A::out_type;
  // Get the result type of applying f to the result of a.
  using f_arg_type = std::declval<Parsed<a_out>>();
  using out = decltype(f());
  return ParserWrapper<in, out>(
      [a, f](std::span<const in> toks) ->
      Parsed<out> {
        return f(a)(toks);
      });
}
#endif

// Parse one or more A; returns vector.
template<Parser A>
inline auto operator +(const A &a) {
  return (a && *a) >[](const auto &p) {
      const auto &[x, xs] = p;
      std::vector<typename A::out_type> ret;
      ret.reserve(xs.size() + 1);
      ret.push_back(x);
      for (const auto &y : xs)
        ret.push_back(y);
      return ret;
    };
}

// Fix<Token, Out>([](const auto &Self) { ...  Self ... })
// Creates a parser that can refer to itself via Self.
// TODO: I get crashes (infinite loop?) if f doesn't take
// Self by const reference. Rule this out statically.
template<class Token, class Out, class F>
requires std::invocable<F, ParserWrapper<Token, Out>>
// TODO: requirements on f
inline auto Fix(const F &f) -> ParserWrapper<Token, Out> {
  ParserWrapper<Token, Out> self(
      [&self, &f](std::span<const Token> toks) {
        decltype(f(std::declval<ParserWrapper<Token, Out>>()))
          parser = f(self);
        return parser(toks);
      });
  return self;
};

// Parses a b a b .... b a.
// Returns the vector of a's results.
// There must be at least one a.
template<Parser A, Parser B>
requires std::same_as<typename A::token_type,
                      typename B::token_type>
inline auto Separate(const A &a, const B &b) {
  using out = A::out_type;
  return (a && *(b >> a))
    >[](const std::tuple<out, std::vector<out>> &p) {
        const auto &[x, xs] = p;
        std::vector<out> ret;
        ret.reserve(1 + xs.size());
        ret.push_back(x);
        for (const out &y : xs) ret.push_back(y);
        return ret;
      };
}

#if 0
requires(P p, std::span<const typename P::token_type> toks) {
  typename P::token_type;
  typename P::out_type;
  { p(toks) } -> std::convertible_to<Parsed<typename P::out_type>>;
};
#endif

// TODO:
// failure handler
// Alternation
// sequence, making tuple
// separate, separate0
// parsefixity

#endif
