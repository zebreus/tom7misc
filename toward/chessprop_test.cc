
#include "chessprop.h"

#include <format>
#include <string>
#include <vector>

#include "ansi.h"
#include "auto-histo.h"
#include "base/logging.h"
#include "base/print.h"
#include "chess.h"
#include "prop.h"

static void CheckAttacked(const Position &pos) {
  CHECK(!pos.BlackMove());
  ChessProp::Board board = ChessProp::BoardFromPosition(pos);
  World empty_world;
  std::vector<bool> empty_assignments;

  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      bool expected = pos.Attacked(r, c);

      Prop attacked = ChessProp::Attacked(board, r, c);
      bool actual = EvaluateProp(empty_world, empty_assignments, attacked);

      if (expected != actual) {
        Print("FEN: {}\n", pos.ToFEN(0, 1));
        Print("At r={}, c={}\n", r, c);
        Print("Actual: {}, Expected: {}\n",
              actual ? "attacked" : "safe",
              expected ? "attacked" : "safe");
        LOG(FATAL) << "Failed";
      }
    }
  }
}

static void CheckAllMovesAgree(const Position &pos) {
  CHECK(!pos.BlackMove());
  ChessProp::Board board = ChessProp::BoardFromPosition(pos);
  World empty_world;
  std::vector<bool> empty_assignments;

  for (int srcr = 0; srcr < 8; srcr++) {
    for (int srcc = 0; srcc < 8; srcc++) {
      for (int dstr = 0; dstr < 8; dstr++) {
        for (int dstc = 0; dstc < 8; dstc++) {
          Position::Move m;
          m.src_row = srcr;
          m.src_col = srcc;
          m.dst_row = dstr;
          m.dst_col = dstc;
          // If one promotion is legal; all are.
          m.promote_to =
            (srcr == 1 && dstr == 0 &&
             pos.PieceAt(srcr, srcc) == Position::PAWN) ?
            (Position::WHITE | Position::QUEEN) :
            0;

          bool expected = Position(pos).IsLegal(m);

          Prop prop = ChessProp::IsLegal(board, srcr, srcc, dstr, dstc);
          bool actual = EvaluateProp(empty_world, empty_assignments, prop);

          if (expected != actual) {
            Print("FEN: {}\n", pos.ToFEN(0, 1));
            Print("Actual: {}, Expected: {}\n",
                  actual ? "legal" : "illegal",
                  expected ? "legal" : "illegal");
            std::string ms;
            if (expected) {
              ms = std::format(" ({}{})",
                               Position(pos).ShortMoveString(m),
                               pos.PGNMoveSuffix(m));
            }
            Print("For move: {}{}\n", Position::DebugMoveString(m), ms);
            Print("Source contains: {:c}\n",
                  Position::DebugPieceChar(
                      pos.SimplePieceAt(srcr, srcc)));
            LOG(FATAL) << "Failed";
          }
        }
      }
    }
  }
}

static void TestStartingPosition() {
  Position pos;
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
}

static void TestEnPassant() {
  Position pos;
  CHECK(Position::ParseFEN(
            "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR "
            "w KQkq d6 0 2", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
}

static void TestCastling() {
  Position pos;
  CHECK(Position::ParseFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
}

static void TestOutOfCheck() {
  Position pos;
  // White in check; lots of options to block or escape.
  CHECK(Position::ParseFEN(
            "rnb1kbnr/pppp1ppp/1B6/q2Np3/8/8/1PP1P1PP/RNBQK2R "
            "w KQkq - 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
}

static void PropSizeHisto() {
  AutoHisto hist(10000);
  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);
  CHECK(world.symbol_names.size() == ChessProp::NUM_BOARD_PROPS);

  for (int srcr = 0; srcr < 8; srcr++) {
    for (int srcc = 0; srcc < 8; srcc++) {
      for (int dstr = 0; dstr < 8; dstr++) {
        for (int dstc = 0; dstc < 8; dstc++) {
          Prop prop =
            SimplifyProp(ChessProp::IsLegal(board, srcr, srcc, dstr, dstc));
          hist.Observe(PropSize(prop));
        }
      }
    }
  }

  Print("{}", hist.SimpleANSI(40));
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestStartingPosition();
  TestEnPassant();
  TestCastling();
  TestOutOfCheck();

  PropSizeHisto();

  Print("OK\n");
  return 0;
}
