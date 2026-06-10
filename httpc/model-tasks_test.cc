
#include "model-tasks.h"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "model-client.h"
#include "model-util.h"

static void ChooseFilesSuccess() {
  auto client = TestModelClient::Create([](std::string_view prompt) {
    return R"({
      "notes": "Testing success case.",
      "files": ["model-tasks_test.cc", "model-tasks.h"],
      "message": "Success message"
    })";
  });

  ModelUtil::FileCollection collection;
  ModelUtil::AvailableFiles available = collection.GetAvailable();
  ModelTasks::ChooseFilesOptions options;
  ModelTasks::ChooseFilesResult result = ModelTasks::ChooseFiles(
      client.get(), "Help me", available, options);

  const ModelTasks::ChosenFiles *chosen =
      std::get_if<ModelTasks::ChosenFiles>(&result);
  CHECK(chosen != nullptr);
  CHECK(chosen->files.size() == 2);
  CHECK(chosen->files[0] == "model-tasks_test.cc");
  CHECK(chosen->files[1] == "model-tasks.h");
  CHECK(chosen->message == "Success message");
}

static void ChooseFilesFailure() {
  auto client = TestModelClient::Create([](std::string_view) {
    return R"({
      "notes": "Testing failure case.",
      "fail": true,
      "message": "Failure message"
    })";
  });

  ModelUtil::FileCollection collection;
  ModelUtil::AvailableFiles available = collection.GetAvailable();
  ModelTasks::ChooseFilesOptions options;
  options.can_fail = true;
  ModelTasks::ChooseFilesResult result = ModelTasks::ChooseFiles(
      client.get(), "Help me", available, options);

  CHECK(std::holds_alternative<ModelTasks::Failure>(result));
  const ModelTasks::Failure &failure =
      std::get<ModelTasks::Failure>(result);
  CHECK(failure.message == "Failure message");
}

static void ChooseFilesSolve() {
  auto client = TestModelClient::Create([](std::string_view) {
    return R"({
      "notes": "Testing solve case.",
      "solve": true,
      "message": "Solve message"
    })";
  });

  ModelUtil::FileCollection collection;
  ModelUtil::AvailableFiles available = collection.GetAvailable();
  ModelTasks::ChooseFilesOptions options;
  options.can_answer = true;
  ModelTasks::ChooseFilesResult result = ModelTasks::ChooseFiles(
      client.get(), "Help me", available, options);

  CHECK(std::holds_alternative<ModelTasks::Answer>(result));
  const ModelTasks::Answer &answer =
      std::get<ModelTasks::Answer>(result);
  CHECK(answer.message == "Solve message");
}

int main(int argc, char **argv) {
  ANSI::Init();

  ChooseFilesSuccess();
  ChooseFilesFailure();
  ChooseFilesSolve();

  Print("OK\n");
  return 0;
}
