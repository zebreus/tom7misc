#include "font-db.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "base/print.h"
#include "map-util.h"
#include "threadutil.h"
#include "util.h"

using namespace std;

FontDB::FontDB() {}

std::unique_ptr<FontDB> FontDB::Create(std::string_view filename) {
  std::unique_ptr<FontDB> db(new FontDB);

  std::unordered_map<string, Type> string_type;
  for (const Type t : {Type::SANS, Type::SERIF, Type::FANCY,
        Type::TECHNO, Type::DECORATIVE,
        Type::MESSY, Type::DINGBATS, Type::OTHER, Type::BROKEN,
        Type::UNKNOWN}) {
    string_type[TypeString(t)] = t;
  }

  std::vector<std::string> lines = Util::ReadFileToLines(filename);
  // Non-existent (or empty).
  if (lines.empty()) return {nullptr};

  for (std::string line : lines) {
    const string flagstring = Util::chop(line);
    const double diffscore = Util::ParseDouble(Util::chop(line), -1.0);
    const string typestring = Util::chop(line);
    const string filename = Util::LoseWhiteL(line);

    auto it = string_type.find(typestring);
    if (it == string_type.end()) {
      Print(stderr, "Unknown type: {}\n", typestring);
      return {nullptr};
    }

    const Type type = it->second;
    if (db->files.find(filename) != db->files.end()) {
      Print(stderr, "Duplicate in fontdb: {}\n", filename);
      return {nullptr};
    }

    Info info;
    info.type = type;
    if (type != Type::UNKNOWN) db->num_sorted++;
    info.bitmap_diffs = diffscore;
    for (char c : flagstring) {
      if (c == '_') continue;
      auto [flag, on] = CharFlag(c);
      info.flags[flag] = on;
    }
    db->files[filename] = info;
  }

  // Print("Total in FontDB: {}\n", db->files.size());
  return db;
}

int64_t FontDB::Size() {
  MutexLock ml(&mu);
  return files.size();
}


bool FontDB::Dirty() {
  MutexLock ml(&mu);
  return dirty;
}

// XXX can probably assume success, fail if not
std::optional<FontDB::Info> FontDB::Lookup(const string &s) {
  MutexLock ml(&mu);
  auto it = files.find(s);
  if (it == files.end()) return {};
  else return {it->second};
}

void FontDB::SetBitmapDiffs(const std::string &s,
                            float bitmap_diffs) {
  MutexLock ml(&mu);
  files[s].bitmap_diffs = bitmap_diffs;
  dirty = true;
}

void FontDB::AssignType(const string &s, Type t) {
  MutexLock ml(&mu);
  if (files[s].type != Type::UNKNOWN) num_sorted--;
  files[s].type = t;
  if (files[s].type != Type::UNKNOWN) num_sorted++;
  dirty = true;
}

void FontDB::SetFlag(const string &s, Flag flag, bool on) {
  MutexLock ml(&mu);
  files[s].flags[flag] = on;
  dirty = true;
}

int64_t FontDB::NumSorted() {
  MutexLock ml(&mu);
  return num_sorted;
}


void FontDB::Save(bool verbose) {
  MutexLock ml(&mu);
  {
    vector<string> lines;
    for (const auto &[filename, info] : MapToSortedVec(files)) {
      lines.push_back(std::format("{} {:.5f} {} {}",
                                  FlagString(info.flags),
                                  info.bitmap_diffs,
                                  TypeString(info.type),
                                  filename));
    }
    Util::WriteLinesToFile(DATABASE_FILENAME, lines);
    if (verbose) {
      Print("Wrote {} entries to {}\n",
            (int64)lines.size(),
            DATABASE_FILENAME);
    }
  }

  dirty = false;
}

