
#include <array>
#include <memory>
#include <string_view>

#include "ansi.h"
#include "base/print.h"
#include "eval.h"
#include "letters.h"
#include "threadutil.h"
#include "timer.h"

static void Evaluate(std::string_view filename) {
  std::unique_ptr<Letters> letters = Letters::LoadFont(filename);

  Timer timer;
  std::array<double, 26> stabs;
  for (int i = 0; i < 100; i++) {
    ParallelComp(26,
                 [&](int i) {
                   auto it = letters->letter.find('A' + i);
                   if (it == letters->letter.end())
                     return;
                   const Letter &letter = it->second;
                   stabs[i] = Eval::Stability(letter);
                 },
                 8);
  }

  for (int i = 0; i < 26; i++) {
    Print("{:c}: {}\n", 'A' + i, stabs[i]);
  }

  Print("Done in {}\n", ANSI::Time(timer.Seconds()));
}


int main(int argc, char **argv) {
  ANSI::Init();

  Evaluate("helveticab.ttf");

  return 0;
}
