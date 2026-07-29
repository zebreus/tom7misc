
#ifndef _RUPERTS_Z3_H
#define _RUPERTS_Z3_H

#include <optional>
#include <string>
#include <string_view>
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
  };

  std::optional<SExp> ParseSExp(std::string_view content);

};

#endif
