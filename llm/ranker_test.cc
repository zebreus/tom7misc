
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
  std::string target = "upholder";

  std::vector<Answer> answers;
#if 0
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
#endif

  for (const char *def : {
      "understanding precisely how overcome levers developer employment requirements",
      "under propeller helps opposer lift do elevator regent",
      "upward pounder handling overall lifting defending establishment radical",
      "undried parmesan hovering on loo dimensional elevator riding",
      "undeniable participant helping orator loudmouthed defender exactor repenter",
      "underneath pacts holding obligations leaning deflected extent reminder",
      "unlocking platforms highly occupy laboratory doctrine expertise respectful",
      "unemployed person hanging onto lowerer droves ending rubber",
      "uses points highlighting one leads detailing election racy",
      "understanding parts hemisphere orbiting lupus defenses excavated reading",
      "upholding positioned holder of leaving defender enforcing responsible",
      "upholsterer participating habitually owning living dwellings erected residentially",
      "understanding person holding opposite localization directionally ether reaches",
      "upholding powerful helpful opposes learn defending examples relentless",
      "undergird protecting habitually overlapping lampposts downright excitedly really",
      "undid proposer helper oppose lover defender eagerly reprieve",
      "unified person holding ordinary leadership description example responsive",

      "upraiser procures helper oversees leader defender endorser re",
      "upright person headed onerously longer direction easily raises",
      "upwardly pushing holders of legal detainment explanations related",
      "undoubtedly pushes hoarders onto leading door elevator returning",
      "undisclosed planetary healthy oyster lover device erroneously resembling",
      "universal people hinder our leader dictating ethics rightfully",
      "unshakably proper hulking oozier lugger demonstrator extoller roof",
      "upwardly prevails holding oneself likewise dictionaries elevating renting",
      "upkeep precipitation hanger of lugging devotion exacting reinforcement",
        "using particularly helpful officers like deterrents employing restraints",
        "underscore proponent handler of limits dominator enthusiast restraint",
        "ultimate powerful heroic objective leader defending everyone resplendently",
        "unattended personifying heightened obscure lady defender evenhanded realm",
        "ultimate powerhouse holds one lone device endorser responsible",
        "underestimated person helps overall leader defend exemplary recognized",
        "underneath possibly headless overturned locomotive device erected rescuing",
        "uniformly perpetrates huge ordinary loads devotedly empowering reducers",

    }) {
    answers.push_back(Answer(def));
  }

  ranker.RunTournament(
      StringPrintf("Which of the following is a better definition "
                   "for the word '%s'?", target.c_str()),
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
