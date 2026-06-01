
#include "model-tasks.h"

#include "gtest/gtest.h"

#include <string>
#include <variant>
#include <vector>

#include "model-client.h"
#include "model-util.h"

TEST(ModelTasksTest, ChooseFilesSuccess) {
  auto client = TestModelClient::Create([](std::string_view) {
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

  ASSERT_TRUE(std::holds_alternative<ModelTasks::ChosenFiles>(result));
  const ModelTasks::ChosenFiles &chosen =
      std::get<ModelTasks::ChosenFiles>(result);
  EXPECT_EQ(chosen.files.size(), 2);
  EXPECT_EQ(chosen.files[0], "model-tasks_test.cc");
  EXPECT_EQ(chosen.files[1], "model-tasks.h");
  EXPECT_EQ(chosen.message, "Success message");
}

TEST(ModelTasksTest, ChooseFilesFailure) {
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

  ASSERT_TRUE(std::holds_alternative<ModelTasks::Failure>(result));
  const ModelTasks::Failure &failure =
      std::get<ModelTasks::Failure>(result);
  EXPECT_EQ(failure.message, "Failure message");
}

TEST(ModelTasksTest, ChooseFilesSolve) {
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

  ASSERT_TRUE(std::holds_alternative<ModelTasks::Answer>(result));
  const ModelTasks::Answer &answer =
      std::get<ModelTasks::Answer>(result);
  EXPECT_EQ(answer.message, "Solve message");
}

