
#include <string>
#include <optional>
#include <format>
#include <cstdlib>

#include "prop.h"
#include "util.h"
#include "threadutil.h"
#include "chess.h"
#include "chessprop.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void PrintStats(std::string_view dir) {
  std::mutex mu;
  size_t total_size = 0, total_shared = 0;
  int done = 0;
  ParallelComp2D(
      64, 64,
      [&](int src, int dst) {
        Position::Move m;
        m.src_row = src / 8;
        m.src_col = src % 8;
        m.dst_row = dst / 8;
        m.dst_col = dst % 8;

        std::string file = std::format("{}/islegal-{}-{}.prop",
                                       dir,
                                       ChessProp::Square(m.src_row,
                                                         m.src_col),
                                       ChessProp::Square(m.dst_row,
                                                         m.dst_col));
        std::optional<Prop> oprop =
          ParseProp(Util::ReadFile(file));
        CHECK(oprop.has_value()) << file;
        const Prop &prop = oprop.value();

        size_t size = PropSize(prop);
        size_t shared = SharedPropSize(prop);

        {
          MutexLock ml(&mu);
          total_size += size;
          total_shared += shared;
          done++;
        }
      },
      8);

  Print("Chess lib: {}\n"
        "{} props loaded.\n"
        "Total size: {} nodes\n"
        "Total shared: {} nodes\n",
        dir, done,
        total_size, total_shared);
};

int main(int argc, char **argv) {
  ANSI::Init();

  std::string dir = "chess";
  if (argc > 1) {
    dir = argv[1];
  }

  PrintStats(dir);

  return 0;
}
