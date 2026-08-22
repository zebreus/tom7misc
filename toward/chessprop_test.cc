
#include "chessprop.h"

#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "auto-histo.h"
#include "base/logging.h"
#include "base/print.h"
#include "chess.h"
#include "prop.h"

using Move = Position::Move;

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

static void CheckMove(const Position &pos_in, std::string_view ms) {
  Position pos(pos_in);

  std::optional<Move> omove = pos_in.ParseLongMove(ms);
  CHECK(omove.has_value()) << ms;
  const Move &move = omove.value();

  bool expected = pos.IsLegal(move);

  ChessProp::Board board = ChessProp::BoardFromPosition(pos);
  World empty_world;
  std::vector<bool> empty_assignments;

  Prop prop = ChessProp::IsLegal(board,
                                 move.src_row, move.src_col,
                                 move.dst_row, move.dst_col);
  bool actual = EvaluateProp(empty_assignments, prop);

  if (expected != actual) {
    Print("FEN: {}\n", pos.ToFEN(0, 1));
    Print("Actual: {}, Expected: {}\n",
          actual ? "legal" : "illegal",
          expected ? "legal" : "illegal");
    std::string ms;
    if (expected) {
      ms = std::format(" ({}{})",
                       Position(pos).ShortMoveString(move),
                       pos.PGNMoveSuffix(move));
    }
    Print("For move: {}{}\n", Position::DebugMoveString(move), ms);
    Print("Source contains: {:c}\n",
          Position::DebugPieceChar(
              pos.SimplePieceAt(move.src_row, move.src_col)));
    LOG(FATAL) << "Failed";
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

static void TestNotEnPassant() {
  Position pos;
  CHECK(Position::ParseFEN(
            "rnbqkbnr/pppp2pp/8/4Pp2/8/8/PPPP1PPP/RNBQKBNR "
            "w KQkq - 0 1", &pos));
  CheckMove(pos, "Pe5f6");
  Print("Not en passant OK.\n");
}

static void TestEnPassantOutOfCheck() {
  Position pos;
  CHECK(Position::ParseFEN(
            "k7/8/8/3Pp3/8/8/8/K6q w - e6 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("En passant (out of check) OK.\n");
}

// Tricky case. en passant is not possible here because the
// rook on a5 would then be attacking the white king. We
// actually need to vacate the source square, fill the
// destination square, AND vacate the captured pawn.
static void TestEnPassantNotIntoCheck() {
  Position pos;
  CHECK(Position::ParseFEN(
            "1nbqkbnr/1ppp2pp/8/r3Pp1K/8/8/PPPP1PPP/RNBQ1BNR "
            "w - f6 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("En passant (not into check) OK.\n");
}

// Test en passant with a check discovered only through the source square.
static void TestEnPassantDiscoveredCheck() {
  for (std::string_view fen : {
      // Not legal: Discovery through source square
      "b7/8/8/3Pp3/8/8/8/k6K w - e6 0 1",
      "3r4/8/8/3Pp3/8/8/3K3k/8 w - e6 0 1",
      // Legal here, since the pawn ends up blocking the
      // check on the destination square.
      "6b1/8/8/3Pp3/8/8/K6k/8 w - e6 0 1",

      // Discovered check + existing check. None are legal.
      "8/1K3r2/8/3Pp3/8/8/8/k6b w - e6 0 1",
      "3r4/8/8/3Pp3/3K4/8/7k/8 w - e6 0 1",
      "8/2r2K2/8/3Pp3/8/8/b6k/8 w - e6 0 1",
      "8/3q4/8/1r1PpK2/8/8/8/1k6 w - e6 0 1",
      "5b2/8/8/2KPp1r1/8/8/8/1k6 w - e6 0 1",
    }) {
    Position pos;
    CHECK(Position::ParseFEN(fen, &pos));
    CheckMove(pos, "Pd5e6");
  }

  Print("En passant discovered check OK.\n");
}

// Another reason we need to clear the captured pawn is that it
// might have itself been delivering the only check.
static void TestEnPassantOutOfCheck2() {
  Position pos;
  CHECK(Position::ParseFEN(
            "rnb1kbnr/1pp2ppp/8/3pP3/2K5/8/PPPP1PPP/RNBQ1BNR "
            "w - d6 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("En passant (out of check #2) OK.\n");
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

static void TestNotCastling() {
  for (const std::string_view fen : {
      // in check
      "3kr3/8/8/8/8/8/8/R3K2R w KQ - 0 1",
      // attacked square that the king moves through
      "3k1r2/8/8/8/6b1/8/8/R3K2R w KQ - 0 1",
      // attacked final square
      "3k4/8/8/2b3b1/8/8/8/R3K2R w KQ - 0 1",
      // piece in the way
      "3k4/8/8/8/8/8/8/Rn2K1nR w KQ - 0 1",
      // piece in the way (2)
      "3k4/8/8/8/8/8/8/R1n1KR1R w KQ - 0 1",
      // no castling bits
      "3k4/8/8/8/8/8/8/R3K2R w - - 0 1",
    }) {
    Position pos;
    CHECK(Position::ParseFEN(fen, &pos));
    CheckMove(pos, "O-O");
    CheckMove(pos, "O-O-O");
  }

  Print("Not Castling OK.\n");
}

static void TestKingMoving() {
  Position pos;
  CHECK(Position::ParseFEN(
            "k3r3/8/8/8/4K3/8/8/8 w - - 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("King moving OK.\n");
}

static void TestOutOfCheck() {
  Position pos;
  // White in check; lots of options to block or escape.
  CHECK(Position::ParseFEN(
            "rnb1kbnr/pppp1ppp/1B6/q2Np3/8/8/1PP1P1PP/RNBQK2R "
            "w KQkq - 0 1", &pos));
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("Out of check OK.\n");
}

static void TestDoubleCheck() {
  Position pos;
  // White is in check from the rook and pawn. Can't
  // escape check except by moving the king.
  CHECK(Position::ParseFEN(
            "4Q3/6B1/2r4Q/1P5R/1N1p3R/2K1P3/4N3/7B w - - 0 1",
            &pos));
  CHECK(pos.IsInCheck());
  CHECK(!pos.IsMated());
  CheckAttacked(pos);
  CheckAllMovesAgree(pos);
  Print("Double check OK.\n");
}

static void TestPromotion() {
  Position pos;
  CHECK(Position::ParseFEN(
            "rr4KQ/PPPPPPPP/8/8/8/8/8/3k4 w - - 0 1",
            &pos));
  CHECK(pos.IsInCheck());
  CHECK(!pos.IsMated());
  CheckAllMovesAgree(pos);
  Print("Promotion OK.\n");
}

static void TestRegressions() {
  static std::initializer_list<std::pair<std::string_view,
                                         std::string_view>> CASES = {
    {"1nbqkbnr/pBpp1pp1/8/2p1p2p/4Q1P1/2B3N1/P1PPPP1P/R3K2R w KQk e6 0 1",
     "O-O-O"},
    {"rn2Nr2/pbppq1p1/4p3/1p6/3P2NP/6P1/PPP1BP1R/R3K1k1 w Q - 0 1",
     "O-O-O"},
    {"1nbqkb1r/prpppppp/7n/2p5/2Q3P1/2B2BN1/P1PPPP1P/R3K2R w KQk - 0 1",
     "O-O"},
  };

  for (const auto &[fen, ms] : CASES) {
    Position pos;
    CHECK(Position::ParseFEN(fen, &pos));
    CheckMove(pos, ms);
  }
  Print("Regressions OK.\n");
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
                             ChessProp::REAL_CHESS));
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

  TestStartingPosition();
  TestRegressions();
  TestCastling();
  TestNotCastling();
  TestOutOfCheck();
  TestKingMoving();
  TestEnPassant();
  TestNotEnPassant();
  TestEnPassantOutOfCheck();
  TestEnPassantOutOfCheck2();
  TestEnPassantNotIntoCheck();
  TestEnPassantDiscoveredCheck();
  TestDoubleCheck();
  TestPromotion();

  Print("Prop size histo:\n");

  PropSizeHisto();

  Print("OK\n");
  return 0;
}
