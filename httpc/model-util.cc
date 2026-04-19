
#include "model-util.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "process-util.h"
#include "util.h"

static constexpr bool VERBOSE = false;

static void ConsumeWS(std::string_view *s) {
  while (!s->empty() && std::isspace((*s)[0])) {
    s->remove_prefix(1);
  }
}

// Translate both windows paths (which could be "..\file" or
// "c:\dir\file" or just "file") and unix-style cygwin/msys2
// paths (which could be "../file" or "./file" or "file" or
// "/d/dir/file") to native OS paths like "d:\dir\file".
std::filesystem::path ModelUtil::NormalizePath(std::string_view p) {
  if (p.empty())
    return "";

  std::string s = [&]() {
      // Intercept msys2 drive mappings like "/c/..." or "/c"
      if (p.length() >= 2 && p[0] == '/' && std::isalpha(p[1])) {
        if (p.length() == 2) {
          // Just the drive letter, like "/c"
          return std::string(p.substr(1, 1)) + ":\\";
        } else if (p[2] == '/') {
          return std::string(p.substr(1, 1)) + ":" + std::string(p.substr(2));
        }
      }
      return std::string(p);
    }();

  // 2. Pass to std::filesystem to handle normalization
  std::filesystem::path path(s);

  // Make the path absolute based on the Current Working Directory.
  // (If it's already absolute like C:\file, this does nothing).
  path = std::filesystem::absolute(path);

  // Resolve any "." or ".." and remove redundant slashes.
  // This will also make the drive letter have the appropriate
  // case (and similar OS-specific normalizations).
  path = std::filesystem::weakly_canonical(path);

  // Convert all forward slashes '/' to native backslashes '\'.
  path.make_preferred();

  return path;
}

std::string ModelUtil::UnixPath(std::filesystem::path p) {
  #ifdef _WIN32
  std::string s = Util::Replace(p.string(), "\\", "/");

  if (s.length() >= 2 && std::isalpha(s[0]) && s[1] == ':') {
    s[1] = std::tolower(s[0]);
    s[0] = '/';
    if (s.length() > 2 && s[2] != '/') {
      s.insert(2, 1, '/');
    }
  }

  return s;

  #else
  return p.string();
  #endif
}

void ModelUtil::FileCollection::DescribeDir(std::filesystem::path dir,
                                            std::string_view desc) {
  dir_descs[std::filesystem::weakly_canonical(dir)] = std::string(desc);
}

// Must be like *.h.
// Can call this multiple times for the same directory, though.
void ModelUtil::FileCollection::AddWildcard(std::filesystem::path dir,
                                            std::string_view pattern) {
  for (std::string_view f : Util::ListFiles(dir.string())) {
    if (Util::MatchesWildcard(pattern, f)) {
      std::filesystem::path p = dir / f;
      if (std::filesystem::is_regular_file(p)) {
        all.insert(p);
      }
    }
  }
}

void ModelUtil::FileCollection::AddConfig(std::string_view config_file) {
  // For interpreting relative paths.
  std::filesystem::path config_path =
    std::filesystem::weakly_canonical(Util::PathOf(config_file));

  for (std::string_view line : Util::ReadFileToLines(config_file)) {
    Util::RemoveOuterWhitespace(&line);
    if (line.empty() || line.starts_with("#")) continue;

    if (Util::TryStripPrefix("describe", &line)) {
      std::string_view dir = Util::Chop(&line);
      Util::RemoveLeadingWhitespace(&line);

      std::filesystem::path cc = config_path / dir;
      if (VERBOSE) {
        Print("Config describes {} / {} = {}\n",
              config_path.string(), dir, cc.string());
      }

      if (!line.empty()) {
        DescribeDir(cc, line);
      }

    } else {
      LOG(FATAL) << "Unexpected command in config: " << line;
    }
  }
}

std::vector<std::filesystem::path>
ModelUtil::SvnList(std::string_view dir) {
  // "svn list" uses the server's version of the files at
  // some revision, which might not be the newest one. This
  // is not what we want.
  //
  // "svn status -vq" will show everything we want, but we
  // have to remove some stuff to find the filename:
  // M             6997     6996 tom7         makefile
  //
  // We use --depth files to only show files, since otherwise
  // we get the full tree recursively. We might want to get the
  // tracked subdirectories too, so that they could be offered
  // for a second pass.

  std::string cmd = std::format("svn st -vq --depth files \"{}\"",
                                // svn insists on unix-style pathnames
                                UnixPath(dir));
  std::optional<std::string> out = ProcessUtil::GetOutput(cmd);
  CHECK(out.has_value()) << "Command failed (is svn in your PATH?): " << cmd;

  if (VERBOSE) {
    Print(AWHITE("{}") "\n", cmd);
  }

  // Parse.
  std::vector<std::filesystem::path> ret;
  for (std::string_view line : Util::SplitToLines(out.value())) {
    if (line.empty())
      continue;

    if (VERBOSE) {
      Print(AGREY("{}") "\n", line);
    }

    CHECK(line.size() > 8) << "Must have at least the 8 status chars?";
    line.remove_prefix(8);

    ConsumeWS(&line);

    // e.g. "> moved from original.cc"
    if (line.empty() || line[0] == '>')
      continue;

    // Now two columns of revision numbers or "-" or "?".
    auto OKChar = [](char c) {
        return std::isdigit(c) || c == '-' || c == '?';
      };

    while (!line.empty() && OKChar(line[0])) line.remove_prefix(1);
    ConsumeWS(&line);

    while (!line.empty() && OKChar(line[0])) line.remove_prefix(1);
    ConsumeWS(&line);

    // Now looking at username.
    while (!line.empty() && !std::isspace(line[0])) {
      line.remove_prefix(1);
    }
    ConsumeWS(&line);

    CHECK(!line.empty()) << "Line didn't contain filename?";

    std::filesystem::path p = NormalizePath(line);
    if (std::filesystem::is_regular_file(p)) {
      ret.push_back(p);
    }
  }
  return ret;
}

void ModelUtil::FileCollection::AddSvnFiles(std::string_view dir) {
  for (const std::filesystem::path &p : SvnList(dir)) {
    all.insert(p);
  }
}

void ModelUtil::FileCollection::AddFile(std::filesystem::path file) {
  all.insert(file);
}

void ModelUtil::FileCollection::AddExcludePattern(std::string_view pat) {
  exclude.emplace_back(pat);
}

static bool Excluded(const std::vector<std::string> &exclude,
                     std::string_view file) {
  for (const std::string &wc : exclude) {
    if (Util::MatchesWildcard(wc, file)) {
      return true;
    }
  }

  return false;
}

// Textualize the list of chosen files using the directory
// descriptions.
//
// Style guides:
//     3168  llm/cpp-style-guide.txt
//     2456  llm/makefile-style-guide.txt
// General C++ utilities:
//     2341  cc-lib/stats.h
//     1214  cc-lib/stats_test.cc
//     3218  cc-lib/base/logging.h
// C++ geometry libraries:
//     2156  cc-lib/geom/lines.h
// Other files:
//     3414  project.txt
std::string ModelUtil::AvailableFiles::Textualize() const {
  CHECK(parent != nullptr);
  std::string text;

  struct FileRef {
    const std::string *name;
    const AvailableFile *file;
  };
  std::map<std::string, std::vector<FileRef>> groups;
  std::vector<FileRef> other_files;

  if (VERBOSE) {
    for (const auto &[dir, desc] : parent->dir_descs) {
      Print("{}: {}\n", dir.string(), desc);
    }
  }

  for (const auto &[f, af] : files) {
    // Empty string means no description matched.
    std::string desc;

    std::filesystem::path p = af.path.parent_path();
    if (VERBOSE) {
      Print("Path to test: {}\n", p.string());
    }

    for (;;) {
      auto it = parent->dir_descs.find(p);
      if (it != parent->dir_descs.end()) {
        desc = it->second;
        break;
      }
      std::filesystem::path next = p.parent_path();
      if (next == p) break;
      p = next;
    }

    groups[desc].push_back({&f, &af});
  }

  auto OutputGroup = [&text](std::string_view desc,
                            const std::vector<FileRef> &files) {
      AppendFormat(&text, "{}:\n", desc);
      for (const auto &ref : files) {
        AppendFormat(&text, "{:8d}  {}\n",
                     ref.file->bytes, *ref.name);
      }
    };

  for (const auto &[desc, fs] : groups)
    if (!desc.empty())
      OutputGroup(desc, fs);

  std::string_view other_header =
    groups.size() > 1 ? "Other" : "Files";
  if (groups.contains("")) OutputGroup(other_header, groups[""]);

  return text;
}


ModelUtil::AvailableFiles
ModelUtil::FileCollection::GetAvailable(std::filesystem::path pwd) const {
  AvailableFiles ret(this);

  std::vector<AvailableFile> valid_files;
  std::unordered_set<std::filesystem::path> dirs;

  // Apply the exclusion filters, and get the sizes of the files.
  for (const std::filesystem::path &p : all) {
    std::string path_name = p.string();
    if (!Excluded(exclude, path_name)) {
      size_t sz = Util::FileSize(path_name).value_or(0);
      valid_files.push_back(AvailableFile{p, sz});
      dirs.insert(p.parent_path());
    }
  }

  pwd = pwd.lexically_normal();
  pwd.make_preferred();

  // Find the minimal number of trailing segments that we need
  // for each directory to make it unique.
  struct DirInfo {
    std::vector<std::string> segs;
    int segs_to_use = 1;
  };
  std::map<std::filesystem::path, DirInfo> dir_info;

  for (const std::filesystem::path &d : dirs) {
    if (d == pwd) continue;
    DirInfo info;
    for (const auto &part : d) {
      info.segs.push_back(part.string());
    }
    dir_info[d] = info;
  }

  bool changed = true;
  while (changed) {
    changed = false;
    std::map<std::string, std::vector<std::filesystem::path>> name_to_dirs;

    for (const auto &[d, info] : dir_info) {
      std::filesystem::path p;
      int start = (int)info.segs.size() - info.segs_to_use;
      if (start < 0) start = 0;
      for (int i = start; i < (int)info.segs.size(); ++i) {
        p /= info.segs[i];
      }

      std::string name = p.string();
      name = Util::Replace(name, "\\", "/");
      name_to_dirs[name].push_back(d);
    }

    for (const auto &[name, conflicts] : name_to_dirs) {
      if (conflicts.size() > 1) {
        for (const auto &d : conflicts) {
          DirInfo &info = dir_info[d];
          if (info.segs_to_use < (int)info.segs.size()) {
            info.segs_to_use++;
            changed = true;
          }
        }
      }
    }
  }

  // Build the map and return it.
  for (const auto &vf : valid_files) {
    std::filesystem::path d = vf.path.parent_path();
    std::string key;
    if (d == pwd) {
      key = vf.path.filename().string();
    } else {
      std::filesystem::path p;
      const DirInfo &info = dir_info[d];
      int start = (int)info.segs.size() - info.segs_to_use;
      if (start < 0) start = 0;
      for (int i = start; i < (int)info.segs.size(); ++i) {
        p /= info.segs[i];
      }
      p /= vf.path.filename();
      key = p.string();
      key = Util::Replace(key, "\\", "/");
    }
    ret.files[key] = vf;
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

// Return true if the string could be json. Checks:
//  - we have balanced curly braces (skipping over
//    string literals of course) and square brackets.
//  - string literals are closed.
//  - There aren't illegal characters.
//
// Permissive. Allows single-quoted values and unquoted property
// names, for example.
bool ModelUtil::IsBalancedJSON(std::string_view s) {
  std::string stack;

  while (!s.empty()) {
    size_t pos = s.find_first_of("\"'{}[]\\");
    if (pos == std::string_view::npos) {
      break;
    }

    char c = s[pos];
    s.remove_prefix(pos + 1);

    switch (c) {
    case '{':
    case '[':
      stack.push_back(c);
      break;
    case '}':
      if (stack.empty() || stack.back() != '{')
        return false;
      stack.pop_back();
      break;
    case ']':
      if (stack.empty() || stack.back() != '[')
        return false;
      stack.pop_back();
      break;
    case '\\':
      return false;

    case '"':
    case '\'': {
      bool closed = false;
      std::string_view search = (c == '"') ? "\"\\" : "'\\";
      while (!s.empty()) {
        size_t end = s.find_first_of(search);
        if (end == std::string_view::npos) {
          return false;
        }
        if (s[end] == '\\') {
          if (end + 1 >= s.size()) return false;
          s.remove_prefix(end + 2);
        } else {
          s.remove_prefix(end + 1);
          closed = true;
          break;
        }
      }
      if (!closed) return false;
      break;
    }

    default:
      LOG(FATAL) << "Bug";
      break;
    }
  }

  return stack.empty();
}

std::optional<std::string> ModelUtil::FindOneJSONObject(
    std::string_view response) {
  // First, prefer content from the first "```json"
  // block to the last "```".
  size_t json_start = response.find("```json");
  if (json_start != std::string_view::npos) {
    size_t json_end = response.rfind("```");
    if (json_end != std::string_view::npos && json_end > json_start + 7) {
      std::string_view candidate = response.substr(
          json_start + 7, json_end - (json_start + 7));
      Util::RemoveOuterWhitespace(&candidate);
      if (!candidate.empty() && IsBalancedJSON(candidate)) {
        return std::string(candidate);
      }
    }
  }

  // Fallback: extract from the first '{' or '[' to the last
  // '}' or ']'. This effectively skips preamble and postamble text.
  size_t open = response.find_first_of("{[");
  if (open != std::string_view::npos) {
    size_t close = response.find_last_of("}]");
    if (close != std::string_view::npos && close > open) {
      std::string_view candidate = response.substr(
          open, close - open + 1);
      if (!candidate.empty() && IsBalancedJSON(candidate)) {
        return std::string(candidate);
      }
    }
  }

  return std::nullopt;
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

std::set<std::string> ModelUtil::IncludeDirs(
    std::string_view seed_file) {
  std::set<std::string> dirs;
  // Try finding a .clangd file in the same directory as the
  // seed_file.

  std::string current_dir = ".";
  size_t last_slash = seed_file.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    current_dir = seed_file.substr(0, last_slash);
  }

  std::string clangd_contents = Util::ReadFile(
      Util::DirPlus(current_dir, ".clangd"));

  // This typically ends with something like
  // CompileFlags:
  // Add: [-xc++, -Wall, --std=c++23, -I., -I../cc-lib, -I../httpv]
  //
  // For simplicity, we just look for any instance of -Idir in the file.

  std::string_view clangd(clangd_contents);

  for (;;) {
    auto pos = clangd.find("-I");
    if (pos == std::string_view::npos)
      break;

    clangd.remove_prefix(pos + 2);
    // Can be -Idir or -I dir
    while (!clangd.empty() && clangd[0] == ' ') clangd.remove_prefix(1);

    auto end_pos = clangd.find_first_of(", ]\n\r\"");
    std::string_view relative = (end_pos == std::string_view::npos)
      ? clangd
      : clangd.substr(0, end_pos);

    if (!relative.empty()) {
      std::string dd =
        Util::NormalizePath(Util::DirPlus(current_dir, relative));
      // Even on windows use /, since this will be easier for the
      // model to understand.
      dirs.insert(Util::Replace(dd, "\\", "/"));
    }
  }

  return dirs;
}

std::string ModelUtil::TextualizeChosenFiles(
    const AvailableFiles &available,
    std::span<const std::string> to_include) {
  std::string text;
  for (const std::string &f : to_include) {
    auto it = available.files.find(f);
    CHECK(it != available.files.end());
    const ModelUtil::AvailableFile &af = it->second;
    AppendFormat(&text,
                 "The file {}:\n"
                 "```\n"
                 "{}"
                 "```\n", f, Util::ReadFile(af.path.string()));
  }
  return text;
}
