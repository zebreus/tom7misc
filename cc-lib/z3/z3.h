
#ifndef _RUPERTS_Z3_H
#define _RUPERTS_Z3_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct Z3 {

  enum class SatResult {
    SAT,
    UNSAT,
    UNKNOWN,
  };

  static SatResult RunSat(std::string_view content,
                          std::optional<double> timeout_seconds = {});

  struct SExp {
    enum class Type {
      ATOM,
      LIST,
    };

    Type type = Type::ATOM;
    std::string atom;
    std::vector<SExp> list;
    // keywords (with arg, like ":depth 2") and flags.
    std::vector<std::pair<std::string, std::unique_ptr<SExp>>> attrs;
  };

  static std::string ToString(const SExp &sexp);

  static std::vector<SExp> Run(std::string_view content,
                               std::optional<double> timeout_seconds = {});

  static std::optional<SExp> ParseSExp(std::string_view content);

  // Consume complete SExps from the beginning of the string.
  // Doesn't fail; check whether content is empty to understand
  // whether the input was completely parsed.
  static std::vector<SExp> ConsumeSExps(std::string_view *content);
};

#endif
