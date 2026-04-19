
#ifndef _HTTPC_MODEL_UTIL_H
#define _HTTPC_MODEL_UTIL_H

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// C++ Utilities for writing little LLM-based utilities.
struct ModelUtil {
  // List the files that are tracked in the given directory.
  static std::vector<std::filesystem::path> SvnList(std::string_view dir);

  // Heuristically find directories that may have relevant files,
  // e.g. by looking in .clangd or makefiles.
  // Works best if seed_file is in the current directory.
  static std::set<std::string> IncludeDirs(std::string_view seed_file);

  // Gemini likes to wrap json output in markdown. Strip it if
  // present (heuristic).
  static std::string StripMarkup(std::string_view json);

  // Get the API key. This should be evolved to check environment
  // variables, command-lines, etc.
  static std::string GetAPIKey();

  // Try to find one JSON object in the response. Some models
  // just love to put a preamble even if you ask only for JSON.
  static std::optional<std::string> FindOneJSONObject(
      std::string_view response);

  // Supports cygwin/msys paths like "/d/tom/llm/file" meaning
  // "d:\\tom\\llm\\file".
  static std::filesystem::path NormalizePath(std::string_view path);

  // Make a unix path, turning e.g. "d:\dir\file" into "/d/dir/file".
  // Some tools insist on this. This is the identity function on unix
  // systems.
  static std::string UnixPath(std::filesystem::path p);

  // A file available to the LLM.
  struct AvailableFile {
    std::filesystem::path path;
    size_t bytes = 0;
  };

  struct FileCollection;
  struct AvailableFiles {
    std::map<std::string, AvailableFile> files;

    // Textualize the list of files, using the chosen short names,
    // including sizes, and directory descriptions.
    std::string Textualize() const;

   private:
    friend struct FileCollection;
    explicit AvailableFiles(const FileCollection *fc) : parent(fc) {}
    const FileCollection *parent = nullptr;
  };

  struct FileCollection {
    // Get all files, keyed by nice names. For example, a file
    // in the current directory just has its filename as the key.
    AvailableFiles GetAvailable(std::filesystem::path pwd = ".") const;

    // Add a description for a directory, which can be used
    // when textualizing the listing.
    void DescribeDir(std::filesystem::path dir,
                     std::string_view desc);

    // Pattern is applied to the base filename, and just supports
    // * wildcards. (You can call it multiple times for the
    // same directory, though.)
    // Not recursive.
    void AddWildcard(std::filesystem::path dir,
                     std::string_view pattern);

    // Add all files that are currently tracked in this dir.
    // Not recursive.
    void AddSvnFiles(std::string_view dir);

    // Add a specific file, regardless of whether it is tracked.
    // (Exclusion patterns still apply.)
    void AddFile(std::filesystem::path file);

    // Add an exclude pattern (with wildcards like *), which
    // prevent files from being returned in AvailableFiles.
    void AddExcludePattern(std::string_view exclude);

   private:
    friend struct AvailableFile;
    std::vector<std::string> exclude;
    // All files that have been added.
    std::unordered_set<std::filesystem::path> all;
    std::unordered_map<std::filesystem::path, std::string> dir_descs;
  };

  static std::string TextualizeChosenFiles(
      const AvailableFiles &available,
      std::span<const std::string> to_include);

  // Exposed mostly for testing:
  //
  // Return true if the string could be json. Checks:
  //  - we have balanced curly braces (skipping over
  //    string literals of course) and square brackets.
  //  - string literals are closed.
  //  - There aren't illegal characters.
  //
  // Permissive. Allows single-quoted values and unquoted property
  // names, for example.
  static bool IsBalancedJSON(std::string_view s);

};

#endif
