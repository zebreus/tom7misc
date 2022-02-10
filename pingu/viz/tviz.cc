
#include <cstdint>
#include <iostream>
#include <string>

#include "SDL.h"

#include "sdl/sdlutil.h"
#include "base/logging.h"
#include "re2/re2.h"
#include "image.h"

using namespace std;

static constexpr int BLOCKSW = 128;
static constexpr int BLOCKSH = 128;
static constexpr int NUM_BLOCKS = BLOCKSW * BLOCKSH;

static constexpr int BLOCKSIZE = 5;

static constexpr int SCREENW = BLOCKSIZE * BLOCKSW;
// plus status bar etc.
static constexpr int SCREENH = BLOCKSIZE * BLOCKSH;

static SDL_Surface *screen = nullptr;

[[maybe_unused]]
static int64_t frame = 0;

struct Block {
  Block() {}

  bool uinitialized = true;
  bool busy = false;
};

static std::array<Block, NUM_BLOCKS> blocks;
[[maybe_unused]]
static int last_read = -1;
[[maybe_unused]]
static int last_write = -1;
[[maybe_unused]]
static int last_processed = -1;

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

static void Redraw(ImageRGBA *img) {
  img->Clear32(0x000000FF);
  for (int yblock = 0; yblock < BLOCKSH; yblock++) {
	for (int xblock = 0; xblock < BLOCKSW; xblock++) {
	  const int idx = yblock * BLOCKSW + xblock;
	  const Block &block = blocks[idx];

	  const int px = xblock * BLOCKSIZE;
	  const int py = yblock * BLOCKSIZE;

	  // has data?
	  if (block.uninitialized) {
		img->BlendRect32(px + 2, py + 2, BLOCKSIZE - 4, BLOCKSIZE - 4,
						 0x222244FF);
	  }

	  #if 0
	  if (block.pending_reads > 0) {
		img->BlendBox32(px + 1, py + 1, BLOCKSIZE - 2, BLOCKSIZE - 2,
						0xCCFFCCFF, {0x77AA77FF});
	  }
	  #endif
	  
	  if (block.busy) {
		img->BlendRect32(px + 4, py + 4, 2, 2, 0xFF3333FF);
		img->BlendBox32(px + 3, py + 3, 4, 4,
						0xFF3333FF, 0xFF333377);
	  }
	  
	  if (idx == last_read) {
		img->BlendRect32(px + 1, py + BLOCKSIZE / 2 - 1,
						 BLOCKSIZE - 2, 2, 0x00FF0077);
	  }

	  if (idx == last_write) {
		img->BlendRect32(px + BLOCKSIZE / 2 - 1, py + 1, 
						 2, BLOCKSIZE - 2, 0xFF000077);
	  }
	}
  }
}

static void Loop() {
  ImageRGBA img(SCREENW, SCREENH);

  RE2 viz_command{".*TVIZ\\[(.+)\\]ZIVT.*"};
  RE2 blockinfo_command("b ([0-9]+) ([0-9]+) ([0-9]+)");
  RE2 update_command("([rw]) ([0-9]+)");

  for (;;) {

	string line, cmd;
	std::getline(cin, line);

	if (RE2::FullMatch(line, viz_command, &cmd)) {

	  int idx, uninitialized, busy;
	  char rw;
	  if (RE2::FullMatch(cmd, blockinfo_command,
						 &idx, &uninitialized, &busy)) {
		CHECK(idx >= 0 && idx < NUM_BLOCKS);
		Block *block = &blocks[idx];
		block->uninitialized = !!zero;
		block->busy = !!busy;
	  } else if (RE2::FullMatch(cmd, update_command, &rw, &idx)) {
		if (rw == 'r') {
		  last_read = idx;
		} else if (rw == 'w') {
		  last_write = idx;
		}
	  }

	  Redraw(&img);
	  BlitImage(img, 0, 0);
	  SDL_Flip(screen);
	} else if (!line.empty()) {
	  printf("[%s]\n", line.c_str());
	}


	SDL_Event event;
	while (SDL_PollEvent(&event)) {
	  switch (event.type) {
	  case SDL_QUIT:
		return;
	  case SDL_KEYDOWN:
		switch (event.key.keysym.sym) {

		case SDLK_ESCAPE:
		  return;

		case SDLK_s:
		  break;

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
