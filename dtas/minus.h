
#ifndef _DTAS_MINUS_H
#define _DTAS_MINUS_H

#include "database.h"

#include <cstdint>
#include <ctime>
#include <memory>
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
  return StringPrintf(ACYAN("%d") AGREY("-") ACYAN("%d"), major, minor);
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

  // Get all existing solutions as (level, movie).
  std::vector<std::pair<LevelId, std::vector<uint8_t>>> GetSolutions() {

    std::vector<std::pair<LevelId, std::vector<uint8_t>>> sols;
    std::unique_ptr<Query> q =
      db->ExecuteString("select level, fm7 from solutions");
    while (std::unique_ptr<Row> r = q->NextRow()) {
      const LevelId level = r->GetInt(0);
      std::vector<uint8_t> movie = SimpleFM7::ParseString(r->GetString(1));
      sols.emplace_back(level, std::move(movie));
    }
    return sols;
  }

  void ExecuteAndPrint(const std::string &s) {
    db->ExecuteAndPrint(s);
  }

  std::unique_ptr<Database> db;
};


#endif
