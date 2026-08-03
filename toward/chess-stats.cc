
#include <mutex>
#include <string>
#include <optional>
#include <format>
#include <cstdlib>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "chess.h"
#include "chessprop.h"
#include "periodically.h"
#include "prop.h"
#include "status-bar.h"
#include "threadutil.h"
#include "util.h"

static void PrintStats(std::string_view dir) {
  ArcFour rc("chessing");
  std::vector<Position> pool = ChessProp::LegalPositions(&rc, 100);

  std::vector<std::vector<bool>> pool_assignments;
  pool_assignments.reserve(pool.size());
  for (const Position &pos : pool) {
    CHECK(!pos.BlackMove());
    ChessProp::Board board = ChessProp::BoardFromPosition(pos);
    std::vector<bool> assignments;
    assignments.reserve(board.props.size());
    for (const Prop &p : board.props) {
      assignments.push_back(std::get<Value>(p.p).value);
    }
    pool_assignments.push_back(assignments);
  }

  StatusBar status(1);
  Periodically status_per(1);

  std::mutex mu;
  size_t total_size = 0, total_shared = 0;
  size_t errors = 0;
  std::optional<std::pair<Position, Position::Move>> example_error;
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

        int local_errors = 0;
        std::optional<std::pair<Position, Position::Move>> local_example_error;
        for (size_t i = 0; i < pool.size(); i++) {
          const Position &pos = pool[i];
          const std::vector<bool> &assignments = pool_assignments[i];

          Position::Move ref_m = m;
          // If one promotion is legal; all are.
          ref_m.promote_to =
            (m.src_row == 1 && m.dst_row == 0 &&
             pos.PieceAt(m.src_row, m.src_col) == Position::PAWN) ?
            (Position::WHITE | Position::QUEEN) :
            0;

          bool expected = Position(pos).IsLegal(ref_m);
          bool actual = EvaluateProp(assignments, prop);

          if (expected != actual) {
            local_errors++;
            if (!local_example_error.has_value()) {
              local_example_error = {pos, ref_m};
            }
          }
        }

        {
          MutexLock ml(&mu);
          total_size += size;
          total_shared += shared;
          errors += local_errors;
          if (local_example_error.has_value() && !example_error.has_value()) {
            example_error = local_example_error;
          }
          done++;
        }

        status_per.RunIf([&]{
            status.Progress(done, 64 * 64, "Checking ({} err)", errors);
          });
      },
      8);

  Print("Chess lib: {}\n"
        "{} props loaded.\n"
        "Total size: {} nodes\n"
        "Total shared: {} nodes\n"
        "Errors: {}\n",
        dir, done,
        total_size, total_shared, errors);

  if (example_error.has_value()) {
    const auto &[pos, m] = example_error.value();
    Print("Example error:\n"
          "FEN: {}\n"
          "Move: {}\n",
          pos.ToFEN(0, 1),
          Position::DebugMoveString(m));
  }
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
