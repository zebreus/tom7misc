
#include "ranker.h"

#include <string>
#include <vector>
#include <memory>

#include "llm.h"
#include "arcfour.h"
#include "randutil.h"
#include "ansi.h"

using namespace std;

Ranker::Ranker(
    const std::string &prompt,
    const std::vector<std::string> &examples) :
  rc(StringPrintf("ranker.%lld", time(nullptr))) {

  ContextParams cparams;
  // cparams.model = "e:\\llama2\\7b\\ggml-model-q4_0.gguf";
  cparams.model = "e:\\llama2\\70b\\ggml-model-q8_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-f16.gguf";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  sparams.type = SampleType::GREEDY;
  sparams.regex = ".*";

  llm.reset(new LLM(cparams, sparams));
  CHECK(llm.get() != nullptr);

  std::string full_prompt = prompt + "\n\n";
  for (const std::string &ex : examples) {
    StringAppendF(&full_prompt, "%s\n\n", ex.c_str());
  }

  llm->DoPrompt(prompt);
  start_state = llm->SaveState();
}

static void RecordWin(Ranker::Answer *winner, Ranker::Answer *loser) {
  winner->wins++;
  loser->losses++;

  // Update elo.
  const double q_winner = pow(10.0, winner->elo_score / 400.0);
  const double q_loser = pow(10.0, loser->elo_score / 400.0);
  // Expected score.
  const double e_winner = q_winner / (q_winner + q_loser);
  const double e_loser = 1.0 - e_winner;
  // Actual score.
  const double s_winner = 1.0, s_loser = 0.0;

  constexpr double k_winner = 40.0;
  constexpr double k_loser = 40.0;

  double delta_winner = k_winner * (s_winner - e_winner);
  double delta_loser = k_loser * (s_loser - e_loser);

  printf(ABLUE("%s") " [" ACYAN("%.1f") "+" AGREEN("%.1f") "]"
    " beat\n" AYELLOW("%s") " [" ACYAN("%.1f") "-" ARED("%.1f") "]\n",
         winner->answer.c_str(),
         winner->elo_score, delta_winner,
         loser->answer.c_str(),
         loser->elo_score, -delta_loser);

  winner->elo_score += delta_winner;
  loser->elo_score += delta_loser;
}

void Ranker::AskOneQuestion(
    const LLM::State &q_state,
    Answer *a, Answer *b) {
  // Reduce bias by randomizing the order.
  if (rc.Byte() & 1) {
    std::swap(a, b);
  }

  llm->LoadState(q_state);
  std::string s =
    StringPrintf(" %s\n"
                 "(2) %s\n"
                 "Answer:",
                 a->answer.c_str(),
                 b->answer.c_str());

  llm->InsertString(s);

  llm->sampler.SetRegEx(" \\([12]\\)");

  // TODO: Compare using the actual probability distributions,
  // as this gives us a natural "strength of victory."
  string reply = llm->GenerateUntilDone();
  printf("Reply: " APURPLE("%s") "\n", reply.c_str());
  if (reply == " (1)") {
    RecordWin(a, b);
  } else if (reply == " (2)") {
    RecordWin(b, a);
  } else {
    CHECK(false) << "Only (1) or (2) should be possible. Got: ["
                 << reply << "]";
  }
}

void Ranker::RunTournament(
    // "Which of the following is a better definition for
    //  the word 'causeway'?"
    const std::string &question,
    std::vector<Answer> *answers,
    // Run until each answer has participated in at least
    // this many matchups.
    int min_matchups) {

  if (answers->size() <= 1) return;

  // Save some work by playing the common prefix of each
  // question first.
  std::string q =
    StringPrintf("Question: %s\n"
                 "(1)", question.c_str());
  llm->LoadState(start_state);
  llm->InsertString(q);
  LLM::State q_state = llm->SaveState();

  bool done = true;
  do {
    Shuffle(&rc, answers);

    done = true;
    for (int i = 0; i < (int)answers->size(); i++) {
      const Answer &a = (*answers)[i];
      if (a.wins + a.losses < min_matchups) {
        printf(AWHITE("%s") " has a record of %d+%d\n",
               a.answer.c_str(),
               a.wins, a.losses);
        // Needs a matchup.
        int j = RandTo(&rc, answers->size() - 1);
        // Can't select self.
        if (j >= i) j++;
        AskOneQuestion(q_state, &(*answers)[i], &(*answers)[j]);
        done = false;
      }
    }
  } while (!done);

}
