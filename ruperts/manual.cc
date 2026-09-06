
#include "solutions.h"

static void Solve() {
  using Nopert = SolutionDB::Nopert;
  using Solution = SolutionDB::Solution;
  ArcFour rc(std::format("grind.noperts.{}", time(nullptr)));
  SolutionDB db;

  std::vector<Nopert> all_noperts = db.GetAllNoperts();



}


int main(int argc, char **argv) {
  ANSI::Init();


  return 0;
}
