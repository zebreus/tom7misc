
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
      bool actual = EvaluateProp(empty_assignments, attacked);

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
          bool actual = EvaluateProp(empty_assignments, prop);

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
  Print("Starting position OK.\n");
}

static void TestEnPassant() {
  Position pos;
  CHECK(Position::ParseFEN(
            "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR "
            "w KQkq d6 0 2", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("En passant OK.\n");
}

static void TestCastling() {
  Position pos;
  CHECK(Position::ParseFEN(
            // White can castle on both sides.
            "qr2k2r/8/8/8/8/8/8/R3K2R w KQk - 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("Castling OK.\n");
}

static void TestKingMoving() {
  Position pos;
  CHECK(Position::ParseFEN(
            "k3r3/8/8/8/4K3/8/8/8 w - - 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("King Moving OK.\n");
}

static void TestOutOfCheck() {
  Position pos;
  // White in check; lots of options to block or escape.
  CHECK(Position::ParseFEN(
            "rnb1kbnr/pppp1ppp/1B6/q2Np3/8/8/1PP1P1PP/RNBQK2R "
            "w KQkq - 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("Out of Check OK.\n");
}

static void PropSizeHisto() {
  AutoHisto hist(10000);
  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);
  CHECK(world.symbol_names.size() == ChessProp::NUM_BOARD_PROPS);


  int unit_count = 0;
  for (int srcr = 0; srcr < 8; srcr++) {
    for (int srcc = 0; srcc < 8; srcc++) {
      for (int dstr = 0; dstr < 8; dstr++) {
        for (int dstc = 0; dstc < 8; dstc++) {
          Prop prop =
            SimplifyProp(ChessProp::IsLegal(
                             board, srcr, srcc, dstr, dstc,
                             ChessProp::KID_CHESS));
          if (prop == False()) {
            unit_count++;
          } else {
            hist.Observe(PropSize(prop));
          }
        }
      }
    }
  }

  Print("False: {}\n", unit_count);
  Print("{}", hist.SimpleANSI(40));
}

int main(int argc, char **argv) {
  ANSI::Init();

  Print("\nTest Attacked / IsLegal...\n");

  #if 0
  TestStartingPosition();
  TestEnPassant();
  TestCastling();
  TestOutOfCheck();
  TestKingMoving();
  #endif

  Print("Prop size histo:\n");

  PropSizeHisto();

  Print("OK\n");
  return 0;
}
