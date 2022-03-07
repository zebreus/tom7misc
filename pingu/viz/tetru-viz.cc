
#include "tetru-viz.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "SDL.h"

#include "sdl/sdlutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "re2/re2.h"
#include "image.h"
#include "timer.h"

using namespace std;

// static constexpr int BLOCKSW = 256;
// static constexpr int BLOCKSH = 64;
// static constexpr int BLOCKWIDTH = 11;
// static constexpr int BLOCKHEIGHT = 20;


static constexpr int BLOCKSW = 200;
static constexpr int BLOCKSH = 32;
static constexpr int NUM_BLOCKS = BLOCKSW * BLOCKSH;

static constexpr int BLOCKWIDTH = 12;
static constexpr int BLOCKHEIGHT = 22;

static constexpr int IMAGEW = BLOCKWIDTH * BLOCKSW;
static constexpr int IMAGEH = BLOCKHEIGHT * BLOCKSH;

static constexpr int SCREENW = IMAGEW; // 1600;
static constexpr int SCREENH = IMAGEH; // 900;

// first pixel in IMAGE that is displayed on SCREEN.
static int scrollx = 0;
static int scrolly = 0;

static SDL_Surface *screen = nullptr;

[[maybe_unused]]
static int64_t frame = 0;

struct Block {
  Block() {}

  std::vector<uint8_t> board;

  int outstanding_reads = 0;
  int outstanding_writes = 0;
  // Number of seconds since visualization began.
  int64 lastupdate = 0;
  int seed = 0;
  string op;

  bool uninitialized = true;
  bool busy = false;
};

static std::array<Block, NUM_BLOCKS> blocks;
[[maybe_unused]]
static int last_read = -1;
[[maybe_unused]]
static int last_write = -1;
[[maybe_unused]]
static int last_processed = -1;

[[maybe_unused]]
static void BlitImage(const ImageRGBA &img, int xpos, int ypos) {
  // PERF should invest in fast blit of ImageRGBA to SDL screen
  for (int y = 0; y < img.Height(); y++) {
    for (int x = 0; x < img.Width(); x++) {
      int xx = xpos + x;
      int yy = ypos + y;
      if (yy >= 0 && yy < SCREENH &&
          xx >= 0 && xx < SCREENW) {
        auto [r, g, b, _] = img.GetPixel(x, y);
        sdlutil::drawpixel(screen, xpos + x, ypos + y, r, g, b);
      }
    }
  }
}

static void BlitImagePart(const ImageRGBA &img,
                          int srcx, int srcy,
                          int srcw, int srch,
                          int destx, int desty) {
  // PERF should invest in fast blit of ImageRGBA to SDL screen
  for (int y = 0; y < srch; y++) {
    for (int x = 0; x < srcw; x++) {
      int sx = srcx + x;
      int sy = srcy + y;
      int dx = destx + x;
      int dy = desty + y;
      if (sx >= 0 && sx < img.Width() &&
          sy >= 0 && sy < img.Height() &&
          dy >= 0 && dy < SCREENH &&
          dx >= 0 && dx < SCREENW) {
        auto [r, g, b, _] = img.GetPixel(sx, sy);
        sdlutil::drawpixel(screen, dx, dy, r, g, b);
      }
    }
  }
}

// Redraw from scratch to offscreen image.
static void Redraw(ImageRGBA *img) {
  printf("Redraw!\n");
  img->Clear32(0x000000FF);
  for (int yblock = 0; yblock < BLOCKSH; yblock++) {
    for (int xblock = 0; xblock < BLOCKSW; xblock++) {
      const int idx = yblock * BLOCKSW + xblock;
      const Block &block = blocks[idx];

      const int px = xblock * BLOCKWIDTH;
      const int py = yblock * BLOCKHEIGHT;
      bool parity = !!((idx + yblock) & 1);

      // has data?
      if (block.uninitialized || block.board.empty()) {
        img->BlendRect32(px, py, BLOCKWIDTH, BLOCKHEIGHT,
                         (xblock == 0 || xblock == BLOCKSW - 1) ? 0x440044FF :
                         (parity ? 0x000044FF : 0x440000FF));
      } else {
        // Draw board
        for (int y = 0; y < 20; y++) {
          for (int x = 0; x < 10; x++) {
            const uint8_t p = block.board[y * 10 + x];
            static std::array<uint32_t, 6> colors = {
              0x000000FF,
              0x44FF44FF,
              0xFF4444FF,
              0x4444FFFF,
              0xFF44FFFF,
            };

            img->SetPixel32(px + x + 1, py + y +1, colors[p]);
          }
        }
      }

      if (block.outstanding_reads > 0) {
        img->BlendText32(px, py + 10, 0x00FF0044,
                         StringPrintf("%d", block.outstanding_reads));
      }

      if (block.outstanding_writes > 0) {
        img->BlendText32(px, py, 0xFF000044,
                         StringPrintf("%d", block.outstanding_writes));
      }

      if (block.busy) {
        /*
        img->BlendRect32(px + 4, py + 4, 2, 2, 0xFF3333FF);
        img->BlendBox32(px + 3, py + 3, 4, 4,
                        0xFF3333FF, 0xFF333377);
        */
        img->SetPixel32(px, py, 0xFF0000FF);
      }

      if (idx == last_read) {
        img->BlendBox32(px, py, BLOCKWIDTH, BLOCKHEIGHT,
                        0x00FF0077, 0x00FF0033);
        /*
        img->BlendRect32(px + 1, py + BLOCKHEIGHT / 2 - 1,
                         BLOCKWIDTH - 2, 2, 0x00FF0033);
        */
      }

      if (idx == last_write) {
        img->BlendBox32(px, py, BLOCKWIDTH, BLOCKHEIGHT,
                        0xFF000077, 0xFF000033);
        /*
        img->BlendRect32(px + BLOCKWIDTH / 2 - 1, py + 1,
                         2, BLOCKHEIGHT - 2, 0xFF000033);
        */
      }

      /*
      img->BlendText32(px + 2, py + 2,
                       0xFFFFFF3F,
                       StringPrintf("%03x", idx));
      */
    }
  }
}

static void Loop() {
  ImageRGBA img(SCREENW, SCREENH);

  RE2 viz_command{".*TVIZ\\[(.+)\\]ZIVT.*"};
  RE2 blockinfo_command("b ([0-9]+) ([0-9]+) ([0-9]+)");
  RE2 update_command("([rw]) ([0-9]+)");
  RE2 board_command("o ([0-9]+) ([0-9]+) ([0-9]+) ([a-z]*)");
  // id, seed, bytes
  RE2 op_command("p ([0-9]+) ([0-9]+) ([-0-9a-zA-Z]+)");

  // initial display...
  Redraw(&img);
  BlitImagePart(img, scrollx, scrolly, SCREENW, SCREENH, 0, 0);
  SDL_Flip(screen);

  Timer run_timer;
  int64 last_redraw = 0;
  for (;;) {

    string line, cmd;
    std::getline(cin, line);
	const int64 sec = run_timer.Seconds();

    if (RE2::FullMatch(line, viz_command, &cmd)) {

      int idx, uninitialized, busy;
      char rw;
      int oreads, owrites;
	  int seed;
      string encoded_board, op;
      if (RE2::FullMatch(cmd, blockinfo_command,
                         &idx, &uninitialized, &busy)) {
        CHECK(idx >= 0 && idx < NUM_BLOCKS);
        Block *block = &blocks[idx];
        block->uninitialized = !!uninitialized;
        block->busy = !!busy;
		block->lastupdate = sec;
	  } else if (RE2::FullMatch(cmd, op_command, &idx, &seed, &op)) {
        CHECK(idx >= 0 && idx < NUM_BLOCKS);
        Block *block = &blocks[idx];
        block->seed = seed;
		block->op = op;
		block->lastupdate = sec;
      } else if (RE2::FullMatch(cmd, update_command, &rw, &idx)) {
        if (rw == 'r') {
          last_read = idx;
        } else if (rw == 'w') {
          last_write = idx;
        }
      } else if (RE2::FullMatch(cmd, board_command,
                                &idx, &oreads, &owrites, &encoded_board)) {
        /*
        printf("%d Board %d: %s\n", (int)run_timer.Seconds(), idx,
               encoded_board.c_str());
        */
        Block *block = &blocks[idx];
        block->uninitialized = false;
        block->outstanding_reads = oreads;
        block->outstanding_writes = owrites;
		block->lastupdate = sec;
        if (encoded_board.empty()) {
          // OK to leave out board.
        } else if (encoded_board.size() != (20 * 10) >> 1) {
          printf("Bad board\n");
        } else {
          block->board = BoardPic::ToPixels(encoded_board);
          CHECK(block->board.size() == 20 * 10);
        }
      }

      if (sec != last_redraw) {
        // PERF: Only sometimes...
        Redraw(&img);

        // XXX update scrollx

        BlitImagePart(img, scrollx, scrolly, SCREENW, SCREENH, 0, 0);
        SDL_Flip(screen);
        last_redraw = sec;
      }
    } else if (!line.empty()) {
      printf("[%s]\n", line.c_str());
    }


    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
#if 0
      case SDL_MOUSEMOTION: {
        SDL_MouseMotionEvent *e = (SDL_MouseMotionEvent*)&event;

        const int oldx = mousex, oldy = mousey;

        mousex = e->x;
        mousey = e->y;

        break;
      }
#endif

      case SDL_QUIT:
        return;
      case SDL_KEYDOWN:
        switch (event.key.keysym.sym) {

		  // disabled since I keep accidentally exiting!
		  /*
        case SDLK_ESCAPE:
          return;
		  */

        case SDLK_s:
          break;

		case SDLK_i: {
		  printf("Dump info:\n");
		  for (int i = 0; i < NUM_BLOCKS; i++) {
			const Block &block = blocks[i];

			if (block.outstanding_reads > 0 ||
				block.outstanding_writes > 0) {
			  int64 ago = sec - block.lastupdate;
			  std::string boardstring;
			  for (uint8_t b : block.board)
				StringAppendF(&boardstring, "%02x", b);
			  printf("Block %d: %d r %d w; %ld sec ago. seed %d op %s\n"
					 "  busy %c, uninit %c, board: %s\n"
					 ,
					 i,
					 block.outstanding_reads,
					 block.outstanding_writes,
					 ago,
					 block.seed,
					 block.op.c_str(),
					 block.busy ? 'y' : 'n',
					 block.uninitialized ? 'y' : 'n',
					 boardstring.c_str());
			}
		  }
		  break;
		}

        default:
          break;
        }
        break;
      default:
        break;
      }
    }
  }
}

int main (int argc, char **argv) {
  CHECK(SDL_Init(SDL_INIT_VIDEO) >= 0);
  fprintf(stderr, "SDL initialized OK.\n");

  screen = sdlutil::makescreen(SCREENW, SCREENH);
  CHECK(screen != nullptr);

  Loop();

  SDL_Quit();
  return 0;
}
