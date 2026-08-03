
#ifndef _HTTPC_MODEL_TASKS_H
#define _HTTPC_MODEL_TASKS_H

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "model-client.h"
#include "model-util.h"

// Utilities that use the LLM.
struct ModelTasks {

  struct ChosenFiles {
    std::vector<std::string> files;
    std::string message;
  };

  struct Failure {
    std::string message;
    std::string raw_content;
  };

  struct Answer {
    std::string message;
  };

  using ChooseFilesResult =
    std::variant<ChosenFiles, Failure, Answer>;

  struct ChooseFilesOptions {
    // e.g. "AI programming assistance"
    std::string domain;
    // e.g. "For this task, you should usually only include header files."
    std::string task_hints;
    // This must be a key in the available files map, or can be empty.
    std::string current_filename;
    bool include_current_file = true;
    bool can_fail = false;
    bool can_answer = false;
    bool guess_match = true;
    bool include_mentioned = true;
  };

  // Using the given model,
  static ChooseFilesResult
  ChooseFiles(
      ModelClient *client,
      std::string_view user_request,
      const ModelUtil::AvailableFiles &available,
      const ChooseFilesOptions &opt);

};

#endif
