
#include <memory>
#include <string>

#include "nnchess.h"
#include "image.h"
#include "randutil.h"
#include "arcfour.h"
#include "base/logging.h"


int main(int argc, char **argv) {
  constexpr int across = 48;
  constexpr int down = 28;
  constexpr int SQUARE = 28;
  constexpr int PAD = 4;
  constexpr int BOARD = SQUARE * 8 + PAD;
  ImageRGBA out(across * BOARD,
                down * BOARD);
  out.Clear32(0x000000FF);

  constexpr int CHESSFONT_ROW = 8;
  constexpr int CHESSFONT_COL = 6;
  constexpr int CHESSFONT_CELLW = 18;
  constexpr int CHESSFONT_CELLH = 32;
  std::unique_ptr<ImageRGBA> chessfont;
  chessfont.reset(ImageRGBA::Load("../bit7/fixedersys2x.png"));
  CHECK(chessfont.get() != nullptr);

  auto PlotPiece = [&out, &chessfont](uint32_t color,
                                      int dx, int dy,
                                      int off) {
      // Keep only pixels that are exactly white.
      for (int y = 0; y < CHESSFONT_CELLH; y++) {
        for (int x = 0; x < CHESSFONT_CELLW; x++) {
          int xx = CHESSFONT_CELLW * (CHESSFONT_COL + off) + x;
          int yy = CHESSFONT_CELLH * CHESSFONT_ROW + y;

          uint32_t c = chessfont->GetPixel32(xx, yy);
          if (c == 0xFFFFFFFF) {
            out.SetPixel32(dx + x, dy + y, color);
          }
        }
      }
    };


  static constexpr const char *EVAL_PGN =
    "d:\\chess\\lichess_db_standard_rated_2020-09.pgn";

  static constexpr int EVAL_POSITIONS = 10000;

  ExamplePool *example_pool = new ExamplePool;
  example_pool->PopulateExamples(EVAL_PGN, EVAL_POSITIONS);
  example_pool->WaitForAll();

  ArcFour rc("test");

  int pool_size = example_pool->pool.size();

  for (int y = 0; y < down; y++) {
    for (int x = 0; x < across; x++) {
      const Position &pos =
        std::get<0>(example_pool->pool[RandTo(&rc, pool_size)]);

      for (int yy = 0; yy < 8; yy++) {
        for (int xx = 0; xx < 8; xx++) {
          const uint32 square_color =
            (xx + yy) & 1 ? 0x666E5EFF : 0xBABA227FF;
          const int yyy = y * BOARD + yy * SQUARE;
          const int xxx = x * BOARD + xx * SQUARE;
          out.BlendRect32(xxx, yyy, SQUARE, SQUARE, square_color);

          const uint8_t p = pos.PieceAt(yy, xx);
          const uint32 piece_color =
            ((p & Position::COLOR_MASK) == Position::BLACK) ?
            0x000000FF : 0xFFFFFFFF;

          auto PlotThePiece = [&](int off) {
              PlotPiece(piece_color, xxx + 4, yyy, off);
            };

          switch (p & Position::TYPE_MASK) {
          case Position::EMPTY: break;
          case Position::PAWN: PlotThePiece(5); break;
          case Position::KNIGHT: PlotThePiece(4); break;
          case Position::BISHOP: PlotThePiece(3); break;
          case Position::ROOK:
          case Position::C_ROOK: PlotThePiece(2); break;
          case Position::QUEEN: PlotThePiece(1); break;
          case Position::KING: PlotThePiece(0); break;
          }
        }
      }
    }
  }

  out/* .ScaleBy(2) */ .Save("chess-test.png");

  return 0;
}
