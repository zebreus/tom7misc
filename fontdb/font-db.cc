#include "font-db.h"

#include <algorithm>
#include <format>
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

FontDB::FontDB() {
  std::unordered_map<string, Type> string_type;
  for (const Type t : {Type::SANS, Type::SERIF, Type::FANCY,
        Type::TECHNO, Type::DECORATIVE,
        Type::MESSY, Type::DINGBATS, Type::OTHER, Type::BROKEN,
        Type::UNKNOWN}) {
    string_type[TypeString(t)] = t;
  }

  for (string line : Util::ReadFileToLines(DATABASE_FILENAME)) {
    const string flagstring = Util::chop(line);
    const double diffscore = Util::ParseDouble(Util::chop(line), -1.0);
    const string typestring = Util::chop(line);
    const string filename = Util::LoseWhiteL(line);

    auto it = string_type.find(typestring);
    CHECK(it != string_type.end()) << "Unknown type " << typestring;
    const Type type = it->second;
    CHECK(files.find(filename) == files.end()) <<
      "Duplicate in fontdb: " << filename;

    Info info;
    info.type = type;
    if (type != Type::UNKNOWN) num_sorted++;
    info.bitmap_diffs = diffscore;
    for (char c : flagstring) {
      if (c == '_') continue;
      auto [flag, on] = CharFlag(c);
      info.flags[flag] = on;
    }
    files[filename] = info;
  }

  Print("Total in FontDB: {}\n", files.size());
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
