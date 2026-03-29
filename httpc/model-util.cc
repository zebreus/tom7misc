
#include "model-util.h"

#include <cctype>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/logging.h"
#include "process-util.h"
#include "util.h"

static void ConsumeWS(std::string_view *s) {
  while (!s->empty() && std::isspace((*s)[0])) {
    s->remove_prefix(1);
  }
}

std::vector<std::string> ModelUtil::SvnList(std::string_view dir) {
  // "svn list" uses the server's version of the files at
  // some revision, which might not be the newest one. This
  // is not what we want.
  //
  // "svn status -vq" will show everything we want, but we
  // have to remove some stuff to find the filename:
  // M             6997     6996 tom7         makefile

  std::string cmd = std::format("svn st -vq \"{}\"", dir);
  std::optional<std::string> out = ProcessUtil::GetOutput(cmd);
  CHECK(out.has_value()) << "Command failed (is svn in your PATH?): " << cmd;

  // Parse.
  std::vector<std::string> ret;
  for (std::string_view line : Util::SplitToLines(out.value())) {
    if (line.empty())
      continue;

    CHECK(line.size() > 8) << "Must have at least the 8 status chars?";
    line.remove_prefix(8);

    ConsumeWS(&line);
    // Now two columns of revision numbers or "-".
    while (!line.empty() && (line[0] == '-' || std::isdigit(line[0]))) {
      line.remove_prefix(1);
    }
    ConsumeWS(&line);

    while (!line.empty() && (line[0] == '-' || std::isdigit(line[0]))) {
      line.remove_prefix(1);
    }
    ConsumeWS(&line);

    // Now looking at username.
    while (!line.empty() && !std::isspace(line[0])) {
      line.remove_prefix(1);
    }
    ConsumeWS(&line);

    CHECK(!line.empty()) << "Line didn't contain filename?";

    // Don't include the directory itself.
    if (line != dir) {
      // svn st will already include the path. But use minimal
      // relative filenames when it's right here.
      if (dir == "." && Util::StartsWith(line, "./")) {
        line.remove_prefix(2);
      }
      ret.emplace_back(line);
    }
  }
  return ret;
}

// Gemini likes to wrap json output in markdown.
std::string ModelUtil::StripMarkup(std::string_view json) {
  Util::RemoveOuterWhitespace(&json);
  if (json.starts_with("```json")) {
    json.remove_prefix(7);
  } else if (json.starts_with("```")) {
    json.remove_prefix(3);
  }

  if (json.ends_with("```")) {
    json.remove_suffix(3);
  }

  return std::string(json);
}

std::string ModelUtil::GetAPIKey() {
  // First, check if the GEMINI_API_KEY environment variable is
  // set, and use that if so.
  if (const char* env_key = std::getenv("GEMINI_API_KEY")) {
    std::string api_key = Util::NormalizeWhitespace(env_key);
    if (!api_key.empty()) {
      return api_key;
    }
  }

  std::string api_key =
    Util::NormalizeWhitespace(Util::ReadFile("d://tom//GEMINI_API_KEY"));
  CHECK(!api_key.empty());
  return api_key;
}
