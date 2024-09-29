
#ifndef _DTAS_MINUS_H
#define _DTAS_MINUS_H

#include "database.h"

#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../fceulib/simplefm7.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"

// MAJOR:MINOR, big endian.
using LevelId = uint16_t;
inline std::pair<uint8_t, uint8_t> UnpackLevel(LevelId id) {
  return std::make_pair((id >> 8) & 0xFF, id & 0xFF);
}

inline LevelId PackLevel(uint8_t major, uint8_t minor) {
  return (uint16_t(major) << 8) | minor;
}

inline std::string ColorLevel(LevelId id) {
  const auto &[major, minor] = UnpackLevel(id);
  return StringPrintf(ACYAN("%02x") AGREY("-") ACYAN("%02x"), major, minor);
}


struct MinusDB {
  static constexpr const char *DBFILE = "minus.sqlite";

  static constexpr int METHOD_SOLVE = 1;
  static constexpr int METHOD_CROSS = 2;

  using Query = Database::Query;
  using Row = Database::Row;

  MinusDB() {
    db = Database::Open(DBFILE);
    CHECK(db.get() != nullptr);
    Init();
  }

  void Init() {
    db->ExecuteAndPrint("create table "
                        "if not exists "
                        "solutions ("
                        "id integer primary key, "
                        "level integer not null, "
                        "fm7 mediumtext not null, "
                        "createdate integer not null"
                        ")");

    db->ExecuteAndPrint("create table "
                        "if not exists "
                        "partial ("
                        "id integer primary key, "
                        "level integer not null, "
                        "fm7 mediumtext not null, "
                        "createdate integer not null"
                        ")");
  }

  // In an arbitrary order.
  std::unordered_set<LevelId> GetDone() {
    // db->ExecuteAndPrint("select level from solutions");

    std::unordered_set<LevelId> done;
    std::unique_ptr<Query> q =
      db->ExecuteString("select level from solutions");
    while (std::unique_ptr<Row> r = q->NextRow()) {
      done.insert((uint16_t)r->GetInt(0));
    }
    return done;
  }

  bool HasSolution(LevelId level) {
    std::unique_ptr<Query> q =
      db->ExecuteString(
          StringPrintf("select level from solutions where level = %d",
                       level));
    while (std::unique_ptr<Row> r = q->NextRow()) {
      return true;
    }
    return false;
  }

  std::optional<std::vector<uint8_t>> GetSolution(LevelId level) {
    std::unique_ptr<Query> q =
      db->ExecuteString(
          StringPrintf("select fm7 from solutions where level = %d",
                       level));
    while (std::unique_ptr<Row> r = q->NextRow()) {
      return {SimpleFM7::ParseString(r->GetString(0))};
    }
    return std::nullopt;
  }

  // Get levels that have partial solutions. It may be in the done
  // set without having a partial solution, so to see all levels
  // with any result, get both.
  std::unordered_set<LevelId> GetAttempted() {
    std::unordered_set<LevelId> done;
    std::unique_ptr<Query> q =
      db->ExecuteString("select level from partial");
    while (std::unique_ptr<Row> r = q->NextRow()) {
      done.insert((uint16_t)r->GetInt(0));
    }
    return done;
  }

  void AddSolution(LevelId id, const std::vector<uint8_t> &sol,
                   int method) {
    std::string fm7 = SimpleFM7::EncodeOneLine(sol);
    db->ExecuteAndPrint(
        StringPrintf(
            "insert into solutions (level, fm7, createdate, method) "
            "values (%d, \"%s\", %lld, %d)",
            id, fm7.c_str(), time(nullptr), method));
  }

  void AddPartial(LevelId id, const std::vector<uint8_t> &sol) {
    std::string fm7 = SimpleFM7::EncodeOneLine(sol);
    db->ExecuteAndPrint(
        StringPrintf("insert into partial (level, fm7, createdate) "
                     "values (%d, \"%s\", %lld)",
                     id, fm7.c_str(), time(nullptr)));
  }

  // Get all existing solutions.
  struct SolutionRow {
    int64_t id = 0;
    LevelId level = 0;
    std::vector<uint8_t> movie;
    int64_t createdate = 0;
    int64_t method = 0;
  };

  template<class F>
  requires requires(F f) {
    f(std::declval<SolutionRow>());
  }
  void ForEachSolution(const F &f) {
    std::unique_ptr<Query> q =
      db->ExecuteString("select "
                        "id, level, fm7, createdate, method "
                        "from solutions");
    while (std::unique_ptr<Row> r = q->NextRow()) {
      SolutionRow row;
      row.id = r->GetInt(0);
      row.level = r->GetInt(1);
      row.movie = SimpleFM7::ParseString(r->GetString(2));
      row.createdate = r->GetInt(3);
      row.method = r->GetInt(4);
      f(std::move(row));
    }
  }

  void DeleteSolution(int64_t rowid) {
    db->ExecuteString(
        StringPrintf("delete from solutions where id = %lld",
                     rowid));
  }

  std::vector<SolutionRow> GetSolutions() {
    std::vector<SolutionRow> sols;
    ForEachSolution([&sols](SolutionRow r) {
        sols.push_back(std::move(r));
      });
    return sols;
  }

  void ExecuteAndPrint(const std::string &s) {
    db->ExecuteAndPrint(s);
  }

  std::unique_ptr<Database> db;
};


#endif
