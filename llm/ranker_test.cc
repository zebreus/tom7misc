
#include "ranker.h"
#include <vector>
#include <cmath>

#include "ansi.h"

using namespace std;

static const char *task =
  "In this task we need to select the best definition of a "
  "word.";

using Answer = Ranker::Answer;

int main(int argc, char **argv) {
  ANSI::Init();

  std::vector<std::string> examples = {
  "For the word \"topmost\", "
  "\"The overall peak magnitude of something tall\" "
  "would be a good definition, because it explains what it means "
  "to be topmost and flows grammatically.",

  "For the word \"vivacious\", "
  "\"Visually interested venereal arts criticism inspiration "
  "of usage significance\" "
  "would be a very poor definition, because it has little to do "
  "with the meaning of vivacious and does not make sense grammatically.",

  "For the word \"lavatory\", "
  "\"Loo alcove vestibule airliner toilet outhouse restroom yard\" "
  "is a poor definition, because although it does use words related "
  "to a lavatory, it does not make sense grammatically.",
  };

  Ranker ranker(task, examples);

  // target word

  std::vector<Answer> answers;
  // This one's good.
  answers.push_back(Answer("elemental stuff so elusive nothing can explain"));
  answers.push_back(Answer("a type of phytoplankton in the Atlantic ocean"));
  answers.push_back(Answer("excellent subjects separately encapsulated nosesmart citrus experiment"));
  answers.push_back(Answer("eliminate substance secreting enhanced nature chez elle"));
  answers.push_back(Answer("essentially substance shouldna encompass nobleheartedness completely emphasizing"));
  answers.push_back(Answer("everyplace singing shades echoes networking cove evenings"));

  answers.push_back(Answer("essay says something eloquent natives certainty esoteric"));
  answers.push_back(Answer("essential substance stands equivalently next celestially ends"));
  answers.push_back(Answer("easily serviceable substantially emphasizing neighboring contexts example"));
  answers.push_back(Answer("essentially simple substantially ethereal naturedly compatible ether"));
  answers.push_back(Answer("existing stronger spiritually etheric nonpareil characteristic etheric"));
  answers.push_back(Answer("eat shape soap essence notebook coffee eye"));

  // from dictionary; not a backronym
  answers.push_back(Answer("the basic real and invariable nature of a thing"));
  // gibberish
  answers.push_back(Answer("aueieia juaobwe elllll minotaur ane"));

  ranker.RunTournament(
      "Which of the following is a better definition for the word 'essence'?",
      &answers,
      std::max(2 * (int)sqrt((float)answers.size()), 2));

  std::sort(answers.begin(),
            answers.end(),
            [](const Answer &a, const Answer &b) {
              return a.elo_score > b.elo_score;
            });

  printf("\n\n" AWHITE("Final results") ":\n");
  for (const Answer &answer : answers) {
    printf(ACYAN("%.1f") " " AGREEN("%d") " " ARED("%d") " %s\n",
           answer.elo_score, answer.wins, answer.losses,
           answer.answer.c_str());
  }

  return 0;
}
