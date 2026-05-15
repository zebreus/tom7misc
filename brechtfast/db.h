
#ifndef _ALBRECHT_DB_H
#define _ALBRECHT_DB_H

#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "bit-string.h"
#include "database.h"
#include "geom/polyhedra.h"
#include "yocto-math.h"

struct DB {
  using vec3 = yocto::vec<double, 3>;
  using frame3 = yocto::frame<double, 3>;

  using Query = Database::Query;
  using Row = Database::Row;

  static constexpr int METHOD_INVALID = 0;
  static constexpr int METHOD_RANDOM_CYCLIC = 1;
  static constexpr int METHOD_RANDOM_SYMMETRIC = 2;
  static constexpr int METHOD_OPT = 3;
  static constexpr int METHOD_CONSTRUCT = 4;

  static constexpr int FIRST_METHOD = 1;
  static constexpr int LAST_METHOD = 1;

  static const char *MethodName(int m) {
    switch (m) {
    case METHOD_INVALID: return "INVALID";
    case METHOD_RANDOM_CYCLIC: return "METHOD_RANDOM_CYCLIC";
    case METHOD_RANDOM_SYMMETRIC: return "METHOD_RANDOM_SYMMETRIC";
    case METHOD_OPT: return "METHOD_OPT";
    case METHOD_CONSTRUCT: return "METHOD_CONSTRUCT";
    default: return "UNKNOWN";
    }
  }

  // Also looks for the database in parent directories
  // before creating it.
  DB(const char *dbfile = "albrecht.sqlite");

  void Init();

  static constexpr int WHY_ANY = 0;
  static constexpr int WHY_LEAF_IH = 1;

  // Reason why it is hard.
  struct Any {};

  // Hard for the leaf IH, identifying the specific leaf.
  struct LeafIH {
    int edge_idx = 0;
    int face_idx = 0;
  };

  using Why = std::variant<Any, LeafIH>;

  struct Hard {
    int id = 0;
    std::vector<vec3> poly_points;

    // Database entries can be hard under different constraints.
    // Results refer to the constrained problem.
    Why why = Any{};

    // Informational metadata about how/when we found it.
    int method = 0;
    int64_t createdate = 0;

    // Results.
    int64_t netness_numer = 0;
    int64_t netness_denom = 0;

    std::optional<BitString> example_net;
  };

  // Or abort.
  Hard GetHard(int id);

  std::vector<Hard> AllHard(bool include_invalid = false);

  void MarkValidity(int id, bool valid);

  void AddHard(const Polyhedron &poly,
               const Why &why,
               int method,
               int64_t netness_numer, int64_t netness_denom,
               std::optional<BitString> example_net);

  void ExecuteAndPrint(const std::string &s) {
    db->ExecuteAndPrint(s);
  }

  // Write a TSV file with the data. The polyhedra points are
  // not included.
  void Spreadsheet(std::string_view filename);

  void DeleteHard(int id);
  void UpdateHard(int id, int64_t netness_numer, int64_t netness_denom,
                  std::optional<BitString> example_net);

  // Perform one-time fixes after schema updates.
  void Fixup();

 private:
  std::unique_ptr<Database> db;
};


#endif
