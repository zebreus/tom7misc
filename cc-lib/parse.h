/* Parser combinators by Tom 7.
   I got fed up! */

#ifndef _CC_LIB_PARSE_H
#define _CC_LIB_PARSE_H

#include <span>
#include <variant>
#include <optional>
#include <functional>
#include <concepts>

#include "base/logging.h"

// A Parser transforms a sequence (span) of Tokens into
// the Out type, or fails (returning nullopt).
#if 0
template<class Token, class Out>
concept Parser = requires(Token t) {

};
#endif

/*
struct Parser {

};
*/

struct Unit { };

// Like std::optional<T>, but with position information.
template<class T>
struct Parsed {
  constexpr Parsed() {}
  static constexpr Parsed<T> None = Parsed();
  Parsed(T t, size_t length) : ot_(t), start_(0), length_(length) {}
  Parsed(T t, size_t start, size_t length) :
    ot_(t), start_(start), length_(length) {}
  bool HasValue() const { return ot_.has_value(); }
  const T &Value() const { return ot_.value(); }
  size_t Start() const { return start_; }
  size_t Length() const { return length_; }
private:
  std::optional<T> ot_;
  size_t start_ = 0, length_ = 0;
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

  Parsed<Out> operator()(std::span<const Token> t) const {
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
  Parsed<Out> operator()(std::span<const Token> unused_) const {
    return Parsed<Out>::None;
  }
};

template<class Token, class Out>
struct Succeed {
  using token_type = Token;
  using out_type = Out;
  explicit Succeed(Out r) : r(r) {}
  Parsed<Out> operator()(std::span<const Token> unused_) const {
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
  Parsed<Unit> operator()(std::span<const Token> toks) const {
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
  Parsed<Token> operator()(std::span<const Token> toks) const {
    if (toks.empty()) return Parsed<Token>::None;
    return Parsed(toks[0], 1);
  }
};

// Shifts the position of a parsed result forward by the
// given amount.
// If the parse failed, also fails.
template<class Out>
Parsed<Out> ShiftForward(Parsed<Out> o, size_t pos) {
  if (o.HasValue()) {
    return Parsed<Out>(o.Value(), o.Start() + pos, o.Length());
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
  using in1 = A::token_type;
  using out = B::out_type;
  return ParserWrapper<in1, out>(
      [a, b](std::span<const in1> toks) -> Parsed<out> {
        auto o1 = a(toks);
        if (!o1.HasValue()) return Parsed<out>::None;
        const size_t start = o1.Start() + o1.Length();
        auto o2 = b(toks.subspan(start));
        return ShiftForward(o2, start);
      });
}

// A << B
// As above, but the result on success is the result of A.
template<Parser A, Parser B>
requires std::same_as<typename A::token_type,
                      typename B::token_type>
inline auto operator <<(const A &a,
                        const B &b) {
  using in1 = A::token_type;
  using out = A::out_type;
  return ParserWrapper<in1, out>(
      [a, b](std::span<const in1> toks) -> Parsed<out> {
        auto o1 = a(toks);
        if (!o1.HasValue()) return Parsed<out>::None;
        const size_t start = o1.Start() + o1.Length();
        auto o2 = b(toks.subspan(start));
        if (!o2.HasValue()) return Parsed<out>::None;
        return o1;
      });
}

#endif
