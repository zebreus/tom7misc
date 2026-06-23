
#include "model-tasks.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "model-client.h"
#include "model-util.h"
#include "rapidjson/document.h"
#include "util.h"

using ChooseFilesOptions = ModelTasks::ChooseFilesOptions;

static std::string ChooseFilesPrompt(
    const ModelUtil::AvailableFiles &available,
    const ChooseFilesOptions &opt,
    std::string_view user_request) {
  std::string ret;

  std::string current_file_contents;
  if (!opt.current_filename.empty() &&
      opt.include_current_file) {
    auto it = available.files.find(opt.current_filename);
    CHECK(it != available.files.end()) << "With a filename, it must "
      "be one of the available files: " << opt.current_filename;
    std::filesystem::path path = it->second.path;
    current_file_contents = Util::ReadFile(path.string());
  }

  if (opt.domain.empty()) {
    AppendFormat(&ret, "Domain: AI programming assistance.\n\n");
  } else {
    AppendFormat(&ret, "Domain: {}.\n\n", opt.domain);
  }

  ret +=
R"(In this task, you'll see a file that the user is currently looking
at. You'll also see a request or question from the user, which is
likely to involve making edits to this file and/or related files. Your
task is not to answer the question directly, but to guess what
additional files would be necessary to do a good job completing the
task. For example, in the common case that the user's request is to
write some code, you should try to determine what source files would
need to be edited, as well as files that would be needed purely for
context in order to write that code correctly. To understand or edit
code, you might want to load the header file for a non-standard
library that is related to the request. If the user's request is to
write tests for some code, then we likely need to see both the header
and implementation for that code in order to know how to test it well.
If the request from the user mentions specific files, you should
usually include those files (using the path from the file list). A
file that describes the current project (e.g. project.txt) is often
useful for background when the request has any subtlety. When writing
code, especially new code, a style guide for the current language is
useful. The "llm" directory contains style guides that apply to all
projects.)";
  if (!opt.task_hints.empty())
    AppendFormat(&ret, " {}", opt.task_hints);

  ret += "\n\n";

  ret +=
R"(You may only choose from the list of available files. The available
files will be given below, with their sizes. Your repsonse to the task
will be in JSON format, and will consist of your notes about the
thought process, and the list of files you would like to open.)";

  if (opt.can_fail) {
    ret += "If you determine that there is something wrong (like\n"
      "the request from the user is missing or nonsensical), or\n"
      "that the task would not be possible even if we select from\n"
      "the available files, you may fail with a message to the user.\n"
      "\n";
  }

  if (opt.can_answer) {
    ret += "Occasionally the request will be easy enough that you\n"
      "can accomplish it directly without additional information.\n"
      "In this case, you may succeed with a message to the user\n"
      "containing the solution.\n"
      "\n";
  }

  if (!opt.current_filename.empty()) {
    AppendFormat(&ret,
                 "The file that the user is looking at is called `{}`",
                 opt.current_filename);

    if (current_file_contents.empty()) {
      ret += ".\n\n";
    } else {
      AppendFormat(&ret,
                   "and it contains:\n"
                   "```\n"
                   "{}\n"
                   "```\n\n",
                   current_file_contents);
    }
  }

  AppendFormat(&ret,
               "The user's request or question is:\n"
               "```\n"
               "{}\n"
               "```\n\n",
               user_request);

 if (!opt.can_answer) {
   ret +=
     "Remember: You're not trying to perform the request yet; you\n"
     "should just select files that we might need to see or edit to\n"
     "solve it.\n\n";
 }

 AppendFormat(&ret,
              "The available files are:\n"
              "{}\n\n",
              available.Textualize());

 ret +=
R"(Each file is listed with its byte size. The cost is directly
proportional to the file size. The priority is to complete the task,
but as a secondary concern, try to minimize the total size of files
chosen. Large files (more than 50kb) should be rarely chosen unless
they are clearly vital to the question. When looking at source code,
header files are often sufficient to understand the interface to a
library.

Now it's time for your output. Given the user's request, what would be
the most important files to read for background information, or to
edit in order to accomplish the task? Sometimes the task will be
self-evident, or only require the context of the current file, so your
answer might be the empty list. You may only name files from the list
but can describe other missing information in an optional separate
field. Use the exact path from the list to name a file, including
any listed subdirectories. Use the exact path even if the file is
referred to with a different name (e.g. without the path) elsewhere.)";

 ret += "\n\n";
 ret += "Your result is a JSON object. It begins with a \"notes\"\n"
   "field with your freeform notes from considering the problem. It\n"
   "ends with a \"message\" field that will be shown to the user.\n"
   "The message field may use light markdown (code blocks, bullet\n"
   "points, and bold text) but should be brief; for example avoid\n"
   "section headings.\n";

 if (opt.can_fail || opt.can_answer) {
   if (opt.can_fail) {
     ret += "If you decide that the task does not make sense or\n"
       "cannot be completed with the set of available files and want\n"
       "to fail, next include the field `\"fail\": true` and use\n"
       "the message field to explain the issue to the user.\n";
   }
   if (opt.can_answer) {
     ret += "If you decide that you can answer the user's question\n"
       "without looking at any additional files, include\n"
       "the field `\"solve\": true` and use the message field to\n"
       "explain the answer to the user.\n";
   }

   ret += "\nThe typical case is that you succeed with a list of files\n"
     "to open, by giving a JSON object that looks like this:\n\n";
 } else {
   ret += "\nYour result is a JSON object that looks like this:\n\n";
 }

 ret +=
R"({ "notes": "My own notes from considering the problem. Do I already know how to do it without additional context? Why do I believe the contents of the files would be useful? How did I consider the file size?",
  "files": ["file1.h", "path/file2.h", ...],
  "message": "Message to the user. This is optional when succeeding with a list of files, but it might contain remarks about information that you are missing. The message must be present when failing or solving. Light markdown is acceptable.",
}

JSON:
)";

 return ret;
}

ModelTasks::ChooseFilesResult
ModelTasks::ChooseFiles(
    ModelClient *client,
    std::string_view user_request,
    const ModelUtil::AvailableFiles &available,
    const ChooseFilesOptions &opt) {

  std::string prompt = ChooseFilesPrompt(available, opt, user_request);
  std::string raw = client->Infer(prompt);
  std::string json = ModelUtil::FindOneJSONObject(raw).value_or("");

  if (json.empty()) {
    return Failure{"Failed to find JSON object in model response."};
  }

  auto doc_opt = ModelUtil::ParseSloppy(json);
  if (!doc_opt.has_value()) {
    return Failure{"Failed to parse JSON response."};
  }

  const auto &document = doc_opt.value();
  if (!document.IsObject()) {
    return Failure{"JSON response was not an object."};
  }

  bool fail = false;
  bool solve = false;
  std::vector<std::string> files;
  std::string message;

  if (document.HasMember("files") && document["files"].IsArray()) {
    for (const auto &v : document["files"].GetArray()) {
      if (v.IsString()) {
        files.push_back(v.GetString());
      }
    }
  }
  if (document.HasMember("message") &&
      document["message"].IsString()) {
    message = document["message"].GetString();
  }
  if (document.HasMember("fail") && document["fail"].IsBool()) {
    fail = document["fail"].GetBool();
  }
  if (document.HasMember("solve") && document["solve"].IsBool()) {
    solve = document["solve"].GetBool();
  }

  if (fail) {
    return Failure{std::move(message)};
  } else if (solve) {
    return Answer{std::move(message)};
  } else {
    return ChosenFiles{std::move(files), std::move(message)};
  }
}

