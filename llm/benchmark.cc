
#include "llama.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "timer.h"
#include "util.h"
#include "vector-util.h"
#include "arcfour.h"
#include "randutil.h"

#include "llm.h"

using namespace std;

static std::string AnsiTime(double seconds) {
  if (seconds < 1.0) {
    return StringPrintf(AYELLOW("%.2f") "ms", seconds * 1000.0);
  } else if (seconds < 60.0) {
    return StringPrintf(AYELLOW("%.3f") "s", seconds);
  } else if (seconds < 60.0 * 60.0) {
    int sec = std::round(seconds);
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "m" AYELLOW("%02d") "s",
                        omin, osec);
  } else {
    int sec = std::round(seconds);
    int ohour = sec / 3600;
    sec -= ohour * 3600;
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "h"
                        AYELLOW("%d") "m"
                        AYELLOW("%02d") "s",
                        ohour, omin, osec);
  }
}

static void EmitTimer(const std::string &name, const Timer &timer) {
  printf(AWHITE("%s") " in %s\n",
         name.c_str(),
         AnsiTime(timer.Seconds()).c_str());
}

// Question text; expected answer.
using Question = std::pair<std::string, std::string>;

struct Problem {
  // The problem name; a-z0-9_.
  string name;
  // Introductory prompt describing the problem.
  string prompt;
  // Unscored example input/output pair.
  Question example;
  std::vector<Question> questions;
};

Problem LoadFreeformProblem(const string &filename) {
  std::vector<string> lines = Util::ReadFileToLines(filename);
  for (string &line : lines) {
    line = Util::LoseWhiteL(Util::LoseWhiteR(std::move(line)));
  }

  FilterVector(&lines, [](const std::string &line) {
      if (line.empty()) return false;
      if (line[0] == '#') return false;
      return true;
    });

  CHECK(lines.size() >= 4);
  Problem problem;
  problem.name = std::move(lines[0]);
  problem.prompt = std::move(lines[1]);
  for (int idx = 2; idx < (int)lines.size(); idx++) {
    const string &line = lines[idx];
    vector<string> v = Util::Split(line, '|');
    CHECK(v.size() == 2) << line;
    Question q =
      make_pair(Util::LoseWhiteL(Util::LoseWhiteR(v[0])),
                Util::LoseWhiteL(Util::LoseWhiteR(v[1])));
    problem.questions.push_back(std::move(q));
  }
  CHECK(!problem.questions.empty());

  // The first question is the example.
  problem.example = std::move(problem.questions[0]);
  problem.questions.erase(problem.questions.begin());

  return problem;
}

Problem LoadMultipleChoiceProblem(const string &filename) {
  ArcFour rc(StringPrintf("%lld", time(nullptr)));
  std::vector<string> lines = Util::ReadFileToLines(filename);
  for (string &line : lines) {
    line = Util::LoseWhiteL(Util::LoseWhiteR(std::move(line)));
  }

  FilterVector(&lines, [](const std::string &line) {
      if (line.empty()) return false;
      if (line[0] == '#') return false;
      return true;
    });

  CHECK(lines.size() >= 4);
  Problem problem;
  problem.name = std::move(lines[0]);
  problem.prompt = std::move(lines[1]);

  // State of current question; we read line-by-line.
  string statement;
  // Exactly one choice should be marked correct.
  vector<std::pair<bool, string>> choices;

  auto EmitQuestion = [&rc, &problem, &statement, &choices]() {
      CHECK(!statement.empty());
      CHECK(!choices.empty());
      Shuffle(&rc, &choices);

      int correct_idx = -1;
      string rendered = StringPrintf("%s\n", statement.c_str());
      for (int i = 0; i < (int)choices.size(); i++) {
        if (choices[i].first) correct_idx = i;
        StringAppendF(&rendered, " (%c) %s", 'a' + i,
                      choices[i].second.c_str());
        if (i < (int)choices.size() - 1) StringAppendF(&rendered, "\n");
      }

      problem.questions.emplace_back(
          rendered,
          StringPrintf("%c", 'a' + correct_idx));

      statement.clear();
      choices.clear();
  };
  for (int idx = 2; idx < (int)lines.size(); idx++) {
    string &line = lines[idx];
    bool is_correct = false, is_choice = false;
    if (Util::TryStripPrefix("(*)", &line)) is_correct = is_choice = true;
    else if (Util::TryStripPrefix("*", &line)) is_choice = true;

    if (is_choice) {
      // Asterisk has already been stripped.
      choices.emplace_back(is_correct,
                           Util::LoseWhiteL(Util::LoseWhiteR(line)));
    } else {
      if (!statement.empty()) EmitQuestion();
      statement = Util::LoseWhiteL(Util::LoseWhiteR(line));
    }
  }

  // Emit final question.
  if (!statement.empty()) EmitQuestion();

  // The first question is the example.
  CHECK(!problem.questions.empty());
  problem.example = std::move(problem.questions[0]);
  problem.questions.erase(problem.questions.begin());

  return problem;
}

static std::pair<string, string> WrapQuestion(const Question &q) {
  return make_pair(
      StringPrintf("Question: %s\n"
                   "Answer: [", q.first.c_str()),
      StringPrintf("%s]\n", q.second.c_str()));
}

static void RunProblem(const Problem &problem, LLM *llm) {
  string prompt = problem.prompt + "\n";
  printf(AWHITE(" == ") APURPLE("%s") AWHITE(" == ") "\n",
         problem.name.c_str());
  printf(AGREY("%s"), prompt.c_str());
  Timer startup_timer;
  llm->Reset();
  llm->DoPrompt(prompt);

  const auto [wq, wa] = WrapQuestion(problem.example);
  string example_string = wq + wa;
  printf(AGREY("%s"), example_string.c_str());
  llm->InsertString(example_string);
  const LLM::State start_state = llm->SaveState();
  EmitTimer("started problem", startup_timer);

  int num_correct = 0;
  Timer answer_timer;
  bool first = true;
  for (const Question &question : problem.questions) {
    if (!first) {
      llm->LoadState(start_state);
    }
    first = false;

    const auto [wq, wa] = WrapQuestion(question);
    printf(AGREY("%s"), wq.c_str());
    llm->InsertString(wq);

    string answer = llm->GenerateUntil("]");
    bool correct = Util::lcase(answer) == Util::lcase(question.second);
    if (answer.empty()) {
      printf(ARED("INVALID") "\n");
    } else if (correct) {
      printf(AGREEN("%s") "\n", answer.c_str());
      num_correct++;
    } else {
      printf(ARED("%s") " (want %s)\n",
             answer.c_str(), question.second.c_str());
    }
  }

  double pct = (100.0 * num_correct) / problem.questions.size();
  // XXX return/record etc.
  printf("Done in %s. Scored %d/%d (%.2f%%)\n",
         AnsiTime(answer_timer.Seconds()).c_str(),
         num_correct, (int)problem.questions.size(), pct);
}

int main(int argc, char ** argv) {
  AnsiInit();
  Timer model_timer;

  LLM::Params lparams;
  // lparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  lparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  // lparams.model = "../llama/models/65B/ggml-model-f16.bin";
  lparams.mirostat = 2;

  LLM llm(lparams);
  EmitTimer("Loaded model", model_timer);

  // Would prefer for this to parse and validate PGN, as well as
  // prompt a different way (no need for Question and Answer).
  RunProblem(LoadFreeformProblem("chess.txt"), &llm);
  // RunProblem(LoadMultipleChoiceProblem("multiple.txt"), &llm);
  // RunProblem(LoadFreeformProblem("code.txt"), &llm);
  // RunProblem(LoadFreeformProblem("trivia.txt"), &llm);
  // RunProblem(LoadFreeformProblem("trivia-personal.txt"), &llm);

  llama_print_timings(llm.lctx);

  return 0;
}
