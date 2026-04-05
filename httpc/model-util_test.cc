
#include "model-util.h"

#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "util.h"

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

int main(int argc, char **argv) {
  ANSI::Init();

  TestStripMarkdown();
  TestIsBalancedJSON();
  TestFindOneJSONObject();

  Print("OK\n");
  return 0;
}
