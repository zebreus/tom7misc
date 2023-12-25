
#ifndef _LLM_RANKER_H
#define _LLM_RANKER_H

#include <string>
#include <vector>
#include <memory>

#include "llm.h"
#include "arcfour.h"

// Using a text description, orders the inputs (strings).

struct Ranker {

  // TODO: Params for model.
  Ranker(
      // "In this task we need to select the best definition of a
      //  word."
      const std::string &prompt,
      // "For the word 'topmost',
      // 'The overall peak magnitude of something tall.'
      // would be a good definition, because it explains what it means
      // to be topmost and flows grammatically.
      //
      // For the word 'vivacious',
      // 'Visually interested venereal arts criticism inspiration '
      //  of usage significance'
      // would be a very poor definition, because it has little to do
      // with the meaning of vivacious and does not make sense grammatically.
      //
      // For the word 'lavatory',
      // 'Loo alcove vestibule airliner toilet outhouse restroom yard'
      // is a poor definition, because although it does use words related
      // to a lavatory, it does not make sense grammatically.
      const std::vector<std::string> &examples);

  struct Answer {
    Answer(const std::string &answer) : answer(answer) {}
    Answer(Answer &&other) = default;
    Answer(const Answer &other) = default;
    Answer &operator =(const Answer &other) = default;
    Answer() = default;

    std::string answer;
    // these are filled in by the tournament.
    // Higher is better.
    double elo_score = 1600.0;
    int wins = 0, losses = 0;
  };

  void RunTournament(
      // "Which of the following is a better definition for
      //  the word 'causeway'?"
      const std::string &question,
      // The vector is modified in place, and reordered.
      std::vector<Answer> *answers,
      // Run until each answer has participated in at least
      // this many matchups (assuming we have at least two
      // answers).
      int min_matchups);

private:
  void AskOneQuestion(const LLM::State &state,
                      Answer *a, Answer *b);

  ArcFour rc;
  std::unique_ptr<LLM> llm;
  LLM::State start_state;
  const std::string question;
};

#endif
