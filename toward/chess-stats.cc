
#include <cstdlib>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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

struct NodeCounts {
  size_t num_ite = 0;
  size_t num_and = 0;
  size_t num_or = 0;
  size_t num_not = 0;
  size_t num_xor = 0;
  size_t num_nand = 0;
  size_t num_nor = 0;
};

static NodeCounts &operator+=(NodeCounts &a, const NodeCounts &b) {
  a.num_ite += b.num_ite;
  a.num_and += b.num_and;
  a.num_or += b.num_or;
  a.num_not += b.num_not;
  a.num_xor += b.num_xor;
  a.num_nand += b.num_nand;
  a.num_nor += b.num_nor;
  return a;
}

static NodeCounts Count(const Prop &prop) {
  NodeCounts counts;
  std::vector<const Prop*> stack = {&prop};
  while (!stack.empty()) {
    const Prop *p = stack.back();
    stack.pop_back();

    if (const Unop *unop = std::get_if<Unop>(&p->p)) {
      if (unop->op == UnopOp::NOT) {
        counts.num_not++;
      }
      stack.push_back(unop->a.get());

    } else if (const Binop *binop = std::get_if<Binop>(&p->p)) {
      switch (binop->op) {
        case BinopOp::AND: counts.num_and++; break;
        case BinopOp::OR: counts.num_or++; break;
        case BinopOp::XOR: counts.num_xor++; break;
        case BinopOp::NAND: counts.num_nand++; break;
        case BinopOp::NOR: counts.num_nor++; break;
      }
      stack.push_back(binop->a.get());
      stack.push_back(binop->b.get());

    } else if (const Ternop *ternop = std::get_if<Ternop>(&p->p)) {
      if (ternop->op == TernopOp::ITE) {
        counts.num_ite++;
      }
    }
  }

  return counts;
}

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
  NodeCounts total;
  std::optional<std::pair<Position, Position::Move>> example_error;
  int done = 0;
  int missing = 0;
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
        if (!oprop.has_value()) {
          MutexLock ml(&mu);
          missing++;
          return;
        }

        CHECK(oprop.has_value()) << file;
        const Prop &prop = oprop.value();

        size_t size = PropSize(prop);
        size_t shared = SharedPropSize(prop);
        NodeCounts prop_counts = Count(prop);

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
          total += prop_counts;
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
        "Missing: {}\n"
        "{} props loaded.\n"
        "Total size: {} nodes\n"
        "Total shared: {} nodes\n"
        "Errors: {}\n"
        "Node counts:\n"
        "  ITE: {}\n"
        "  AND: {}\n"
        "   OR: {}\n"
        "  NOT: {}\n"
        "  XOR: {}\n"
        " NAND: {}\n"
        "  NOR: {}\n",
        dir, missing, done,
        total_size, total_shared, errors,
        total.num_ite,
        total.num_and, total.num_or, total.num_not,
        total.num_xor, total.num_nand, total.num_nor);

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
