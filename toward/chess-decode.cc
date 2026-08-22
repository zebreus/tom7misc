
#include "ansi.h"
#include "util.h"
#include "chess.h"
#include "base/print.h"

static Position Decode(std::string_view in) {
  auto tokens = Util::Tokens(in, [](char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' ||
           c == '(' || c == ')';
  });

  Position pos;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      pos.SetPiece(r, c, Position::EMPTY);
    }
  }
  pos.SetBlackMove(false);
  pos.SetEnPassantColumn(std::nullopt);

  bool cast_Q = false, cast_K = false, cast_q = false, cast_k = false;

  for (size_t i = 0; i < tokens.size(); i++) {
    if (tokens[i] == "sat" || tokens[i] == "unsat") continue;

    if (i + 1 < tokens.size()) {
      std::string_view var = tokens[i];
      std::string_view val = tokens[i + 1];
      i++; // consume val

      if (val != "true") continue;

      if (var.size() == 4 && var[2] == '_' &&
          var[0] >= 'a' && var[0] <= 'h' &&
          var[1] >= '1' && var[1] <= '8') {
        int col = var[0] - 'a';
        int row = 8 - (var[1] - '0');
        uint8_t piece = Position::EMPTY;
        switch (var[3]) {
          case 'p': piece = Position::BLACK | Position::PAWN; break;
          case 'n': piece = Position::BLACK | Position::KNIGHT; break;
          case 'b': piece = Position::BLACK | Position::BISHOP; break;
          case 'r': piece = Position::BLACK | Position::ROOK; break;
          case 'q': piece = Position::BLACK | Position::QUEEN; break;
          case 'k': piece = Position::BLACK | Position::KING; break;
          case 'P': piece = Position::WHITE | Position::PAWN; break;
          case 'N': piece = Position::WHITE | Position::KNIGHT; break;
          case 'B': piece = Position::WHITE | Position::BISHOP; break;
          case 'R': piece = Position::WHITE | Position::ROOK; break;
          case 'Q': piece = Position::WHITE | Position::QUEEN; break;
          case 'K': piece = Position::WHITE | Position::KING; break;
        }
        if (piece != Position::EMPTY) {
          pos.SetPiece(row, col, piece);
        }
      } else if (var.starts_with("ep_") && var.size() == 4) {
        int col = var[3] - 'a';
        if (col >= 0 && col <= 7) {
          pos.SetEnPassantColumn(col);
        }
      } else if (var.starts_with("cast_") && var.size() == 6) {
        char c = var[5];
        if (c == 'Q') cast_Q = true;
        else if (c == 'K') cast_K = true;
        else if (c == 'q') cast_q = true;
        else if (c == 'k') cast_k = true;
      }
    }
  }

  // Add castling rooks at the very end to avoid being overwritten
  // by the plain 'R' or 'r' that z3 also outputs for those squares.
  if (cast_Q) pos.SetPiece(7, 0, Position::WHITE | Position::C_ROOK);
  if (cast_K) pos.SetPiece(7, 7, Position::WHITE | Position::C_ROOK);
  if (cast_q) pos.SetPiece(0, 0, Position::BLACK | Position::C_ROOK);
  if (cast_k) pos.SetPiece(0, 7, Position::BLACK | Position::C_ROOK);

  return pos;
}


int main(int argc, char **argv) {
  ANSI::Init();

  Position pos = Decode(Util::ReadStdin());
  Print("FEN: {}\n\n", pos.ToFEN(0, 0));
  Print("Which is:\n{}\n", pos.UnicodeAnsiBoardString());

  return 0;
}
