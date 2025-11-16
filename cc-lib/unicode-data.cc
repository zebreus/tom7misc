#include "unicode-data.h"

#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "string-table.h"
#include "util.h"

namespace {

struct Row {
  uint32_t codepoint = 0;
  StringTable::Entry name = {};
};

// With some attempt to be compact in memory.
struct UnicodeData_ : public UnicodeData {
  UnicodeData_() {}

  std::optional<CodepointData> GetByName(std::string_view name) const override {
    if (auto it = name_to_row.find(name); it != name_to_row.end()) {
      const Row &row = rows[it->second];
      return {CodepointData{
          .codepoint = row.codepoint,
          .name = string_table.GetView(row.name),
        }};
    }
    return std::nullopt;
  }

  std::optional<CodepointData> GetByCodepoint(uint32_t codepoint) const override {
    if (auto it = cp_to_row.find(codepoint); it != cp_to_row.end()) {
      const Row &row = rows[it->second];
      return {CodepointData{
          .codepoint = row.codepoint,
          .name = string_table.GetView(row.name),
        }};
    }
    return std::nullopt;
  }


  void AddLine(std::string_view line) {
    Util::RemoveOuterWhitespace(&line);
    if (line.empty()) return;
    std::vector<std::string> cols = Util::Split(line, ';');
    // 2077;SUPERSCRIPT SEVEN;No;0;EN;<super> 0037;;7;7;N;SUPERSCRIPT DIGIT SEVEN;;;;
    CHECK(cols.size() >= 2) << "Bad line: " << line;
    auto co = Util::ParseHex(cols[0]);
    CHECK(co.has_value() && co.value() <= (uint64_t)std::numeric_limits<uint32_t>::max) <<
      "Bad line: " << line;
    rows.push_back(Row{
        .codepoint = (uint32_t)co.value(),
        .name = string_table.Add(cols[1]),
      });
  }

  void Finalize() {
    CHECK(rows.size() <= (size_t)std::numeric_limits<uint32_t>::max);
    // We don't do this until all rows have been added, since we want to use the
    // string_views in the intern table as keys in the hash map.
    string_table.Finalize();
    rows.shrink_to_fit();
    for (size_t idx = 0; idx < rows.size(); idx++) {
      const Row &row = rows[idx];
      cp_to_row[row.codepoint] = (uint32_t)idx;
      // There are duplicate names like <control>. We just allow overwriting.
      // A future version could do something smarter..?
      name_to_row[string_table.GetView(row.name)] = (uint32_t)idx;
    }
  }

  StringTable string_table;
  std::vector<Row> rows;
  std::unordered_map<uint32_t, uint32_t> cp_to_row;
  std::unordered_map<std::string_view, uint32_t> name_to_row;
};

}

std::unique_ptr<UnicodeData> UnicodeData::FromContent(std::string_view content) {
  std::unique_ptr<UnicodeData_> ud(new UnicodeData_);

  Util::ForEachLineInString(content, [&ud](std::string_view line) {
      ud->AddLine(line);
    });

  ud->Finalize();

  return std::unique_ptr<UnicodeData>(ud.release());
}

std::unique_ptr<UnicodeData> UnicodeData::FromContent(std::span<const uint8_t> content) {
  return FromContent(std::string_view((const char *)content.data(), content.size()));
}
