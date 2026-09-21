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
  UnicodeData::GeneralCategory category = UnicodeData::Cn;
};

static std::optional<UnicodeData::GeneralCategory> ParseCategory(
    std::string_view s) {
  static constexpr std::string_view NAMES[] = {
    "Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl",
    "No", "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po", "Sm", "Sc",
    "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Cn",
  };
  for (size_t i = 0; i < sizeof(NAMES) / sizeof(std::string_view); i++) {
    if (s == NAMES[i]) {
      return (UnicodeData::GeneralCategory)i;
    }
  }
  return std::nullopt;
}

// With some attempt to be compact in memory.
struct UnicodeData_ : public UnicodeData {
  UnicodeData_() {}

  std::optional<CodepointData> GetByName(std::string_view name) const override {
    if (auto it = name_to_row.find(name); it != name_to_row.end()) {
      const Row &row = rows[it->second];
      return {CodepointData{
          .codepoint = row.codepoint,
          .name = string_table.GetView(row.name),
          .category = row.category,
        }};
    }
    return std::nullopt;
  }

  std::optional<CodepointData> GetByCodepoint(
      uint32_t codepoint) const override {
    if (auto it = cp_to_row.find(codepoint); it != cp_to_row.end()) {
      const Row &row = rows[it->second];
      return {CodepointData{
          .codepoint = row.codepoint,
          .name = string_table.GetView(row.name),
          .category = row.category,
        }};
    }
    return std::nullopt;
  }

  size_t Size() const override {
    return rows.size();
  }

  CodepointData GetByIndex(size_t index) const override {
    CHECK(index < rows.size());
    const Row &row = rows[index];
    return CodepointData{
        .codepoint = row.codepoint,
        .name = string_table.GetView(row.name),
        .category = row.category,
    };
  }

  void AddLine(std::string_view line) {
    Util::RemoveOuterWhitespace(&line);
    if (line.empty()) return;
    std::vector<std::string> cols = Util::Split(line, ';');
    // 2077;SUPERSCRIPT SEVEN;No;0;EN;<super> 0037;;7;7;N;SUPERSCRIPT DIGIT SEVEN;;;;
    CHECK(cols.size() >= 3) << "Bad line: " << line;
    auto co = Util::ParseHex(cols[0]);
    CHECK(co.has_value() && co.value() <=
          (uint64_t)std::numeric_limits<uint32_t>::max) <<
      "Bad line: " << line;
    auto cat = ParseCategory(cols[2]);
    CHECK(cat.has_value()) << "Bad category: " << cols[2];
    rows.push_back(Row{
        .codepoint = (uint32_t)co.value(),
        .name = string_table.Add(cols[1]),
        .category = cat.value(),
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

std::unique_ptr<UnicodeData> UnicodeData::FromContent(
    std::string_view content) {
  std::unique_ptr<UnicodeData_> ud(new UnicodeData_);

  Util::ForEachLineInString(content, [&ud](std::string_view line) {
      ud->AddLine(line);
    });

  ud->Finalize();

  return std::unique_ptr<UnicodeData>(ud.release());
}

std::unique_ptr<UnicodeData> UnicodeData::FromContent(
    std::span<const uint8_t> content) {
  return FromContent(std::string_view((const char *)content.data(),
                                      content.size()));
}

bool UnicodeData::IsLetter(GeneralCategory c) {
  switch (c) {
    case Lu:
    case Ll:
    case Lt:
    case Lm:
    case Lo:
      return true;
    default:
      return false;
  }
}

bool UnicodeData::IsCasedLetter(GeneralCategory c) {
  switch (c) {
    case Lu:
    case Ll:
    case Lt:
      return true;
    default:
      return false;
  }
}

bool UnicodeData::IsMark(GeneralCategory c) {
  switch (c) {
    case Mn:
    case Mc:
    case Me:
      return true;
    default:
      return false;
  }
}

bool UnicodeData::IsNumber(GeneralCategory c) {
  switch (c) {
    case Nd:
    case Nl:
    case No:
      return true;
    default:
      return false;
  }
}

bool UnicodeData::IsPunctuation(GeneralCategory c) {
  switch (c) {
    case Pc:
    case Pd:
    case Ps:
    case Pe:
    case Pi:
    case Pf:
    case Po:
      return true;
    default:
      return false;
  }
}

bool UnicodeData::IsSymbol(GeneralCategory c) {
  switch (c) {
    case Sm:
    case Sc:
    case Sk:
    case So:
      return true;
    default:
      return false;
  }
}

bool UnicodeData::IsOther(GeneralCategory c) {
  switch (c) {
    case Cc:
    case Cf:
    case Cs:
    case Co:
    case Cn:
      return true;
    default:
      return false;
  }
}

bool UnicodeData::IsSeparator(GeneralCategory c) {
  switch (c) {
    case Zs:
    case Zl:
    case Zp:
      return true;
    default:
      return false;
  }
}

