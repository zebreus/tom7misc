
#include "model-util.h"

#include <cmath>
#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "util.h"
#include "rapidjson/document.h"

static void TestStripMarkdown() {
  CHECK(ModelUtil::StripMarkup("").empty());

  CHECK(Util::NormalizeWhitespace(
            ModelUtil::StripMarkup(
                "```json\n"
                "{ \"value\": 3 }\n"
                "```\n")) == "{ \"value\": 3 }");
}

static void TestIsBalancedJSON() {
  CHECK(ModelUtil::IsBalancedJSON("{}"));
  CHECK(ModelUtil::IsBalancedJSON("[]"));
  CHECK(ModelUtil::IsBalancedJSON(""));
  CHECK(ModelUtil::IsBalancedJSON("[{\"a\": 1}, {\"b\": 2}]"));
  CHECK(ModelUtil::IsBalancedJSON("[{\"a\": 1},\r\n{\"b\": 2}]"));

  CHECK(ModelUtil::IsBalancedJSON("{'a': 'b'}"));

  CHECK(ModelUtil::IsBalancedJSON("{\"a\": \"}\"}"));
  CHECK(ModelUtil::IsBalancedJSON("[\"{\"]"));
  CHECK(ModelUtil::IsBalancedJSON("{'key': ']'}"));

  CHECK(ModelUtil::IsBalancedJSON("{\"key\": \"\\\"\"}"));
  CHECK(ModelUtil::IsBalancedJSON("['\\\'']"));
  CHECK(ModelUtil::IsBalancedJSON("{\"key\": \"a\\nb\"}"));

  CHECK(!ModelUtil::IsBalancedJSON("{"));
  CHECK(!ModelUtil::IsBalancedJSON("}"));
  CHECK(!ModelUtil::IsBalancedJSON("["));
  CHECK(!ModelUtil::IsBalancedJSON("]"));
  CHECK(!ModelUtil::IsBalancedJSON("}{"));
  CHECK(!ModelUtil::IsBalancedJSON("]["));
  CHECK(!ModelUtil::IsBalancedJSON("{{]]"));
  CHECK(!ModelUtil::IsBalancedJSON("[{][}"));

  CHECK(!ModelUtil::IsBalancedJSON("{\"key"));
  CHECK(!ModelUtil::IsBalancedJSON("['key]"));

  CHECK(!ModelUtil::IsBalancedJSON("\\"));
  CHECK(!ModelUtil::IsBalancedJSON("{\\}"));
}


static void TestFindOneJSONObject() {
  CHECK(ModelUtil::FindOneJSONObject("{}") == "{}");
  CHECK(ModelUtil::FindOneJSONObject("[]") == "[]");

  CHECK(ModelUtil::FindOneJSONObject("hi {\"a\": 1} bye") ==
        "{\"a\": 1}");
  CHECK(ModelUtil::FindOneJSONObject("before [{}] after") == "[{}]");

  // Extracts from markdown code blocks
  CHECK(ModelUtil::FindOneJSONObject(
            "prefix, which contains code:\n"
            "\n"
            "```\n"
            "for (;;) { return false; }\n"
            "```\n"
            "\n"
            "\n```json\n"
            "{\"key\": \"val\"}\n"
            "```\n"
            "suffix\n") ==
            "{\"key\": \"val\"}");

  CHECK(ModelUtil::FindOneJSONObject("```json\n  [1, 2]  \n```") ==
        "[1, 2]");

  CHECK(!ModelUtil::FindOneJSONObject("Just some text.").has_value());
  CHECK(!ModelUtil::FindOneJSONObject("Unbalanced { object").has_value());
  CHECK(!ModelUtil::FindOneJSONObject("```json\n [1, 2 \n```").has_value());
}

// Technically, JSON strings cannot contain newlines. But sometimes
// the model will do it (e.g. when outputting code blocks).
static constexpr std::string_view BAD_JSON1 = R"(
{
  "notes": "v5 is coplanar with f2 but outside edge 6.",
  "solution": "This situation is indeed organically possible.\n\nMathematically, `e6` is a \"dead end\" and cannot be extended. The bug is at the very end of `ComputeFeasibleAngles`:\n\n```cpp
  if (max_angle < min_angle) {
    max_angle = min_angle;
  }

  return {min_angle, max_angle};
```\n\nThis clamp forces `max_angle` to `0.005`.\n\n**Diagnostic Step:**\nRemove the clamp:\n\n```cpp

  // Remove or comment out this clamp so the caller can detect dead-end edges
  // if (max_angle < min_angle) {
  //   max_angle = min_angle;
  // }

  return {min_angle, max_angle};
```\n\nYour `CHECK(subtended > 1.0e-5)` will still fail.",
  "confidence": 95
}

)";

static constexpr std::string_view UNRECOVERABLE1 = R"(
} it ain\'t

even close‽\n

\\" "
{)";

static void TestRecoverJSON() {
  rapidjson::Document document;
  CHECK(document.Parse(std::string(BAD_JSON1)).HasParseError()) << "This "
    "test wants an input that does not parse!";

  std::string rescued(ModelUtil::RescueJSON(BAD_JSON1));

  CHECK(!document.Parse(rescued).HasParseError() &&
        document.IsObject());
}

static void TestParseSloppy() {
  rapidjson::Document doc = ModelUtil::ParseSloppyOrDie(BAD_JSON1);
  CHECK(doc.HasMember("notes") &&
        doc.HasMember("solution") &&
        doc.HasMember("confidence"));

  CHECK(doc["confidence"].IsNumber());
  CHECK((int)std::round(doc["confidence"].GetDouble()) == 95);

  CHECK(ModelUtil::ParseSloppy(BAD_JSON1).has_value());

  CHECK(!ModelUtil::ParseSloppy(UNRECOVERABLE1).has_value());
}

static void TestEscapeJSON() {
  const std::string orig = "a <test> with \"quotes\" \n and \\ 'yes' ";
  const std::string escaped = ModelUtil::EscapeJSON(orig);

  CHECK(escaped.find('<') == std::string::npos);
  CHECK(escaped.find('>') == std::string::npos);

  std::string json = "{\"k\": \"" + escaped + "\"}";
  rapidjson::Document doc = ModelUtil::ParseSloppyOrDie(json);

  CHECK(doc.HasMember("k"));
  CHECK(doc["k"].IsString());
  CHECK(std::string(doc["k"].GetString()) == orig);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestStripMarkdown();
  TestIsBalancedJSON();
  TestFindOneJSONObject();

  TestEscapeJSON();
  TestRecoverJSON();
  TestParseSloppy();

  Print("OK\n");
  return 0;
}
