
#include "emulator.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <stdlib.h>
#include <string>
#include <vector>
#include <cassert>

#include "fceu.h"
#include "git.h"
#include "types.h"
#include "utils/md5.h"
#include "state.h"
#include "sound.h"
#include "palette.h"
#include "input.h"
#include "ppu.h"
#include "opcodes.h"
#include "cart.h"

#include "fc.h"

// Compression yields 2x slowdown, but states go from ~80kb to 1.4kb
// Without screenshot, ~1.3kb and only 40% slowdown
// XXX External interface now allows client to specify, so maybe just
// make this a guarantee.
#define USE_COMPRESSION 1

#if USE_COMPRESSION
#include <zconf.h>
#include <zlib.h>
#endif

using namespace std;

static constexpr int RAM_BYTE_SIZE = 0x800;
static constexpr int IMAGE_BYTE_SIZE = 256 * 256 * 4;

// 0xRRGGBBAA. Alpha is always FF for these.
static constexpr uint32_t ntsc_palette_rgba[] = {
  0x808080FF, 0x003DA6FF, 0x0012B0FF, 0x440096FF,
  0xA1005EFF, 0xC70028FF, 0xBA0600FF, 0x8C1700FF,
  0x5C2F00FF, 0x104500FF, 0x054A00FF, 0x00472EFF,
  0x004166FF, 0x000000FF, 0x050505FF, 0x050505FF,
  0xC7C7C7FF, 0x0077FFFF, 0x2155FFFF, 0x8237FAFF,
  0xEB2FB5FF, 0xFF2950FF, 0xFF2200FF, 0xD63200FF,
  0xC46200FF, 0x358000FF, 0x058F00FF, 0x008A55FF,
  0x0099CCFF, 0x212121FF, 0x090909FF, 0x090909FF,
  0xFFFFFFFF, 0x0FD7FFFF, 0x69A2FFFF, 0xD480FFFF,
  0xFF45F3FF, 0xFF618BFF, 0xFF8833FF, 0xFF9C12FF,
  0xFABC20FF, 0x9FE30EFF, 0x2BF035FF, 0x0CF0A4FF,
  0x05FBFFFF, 0x5E5E5EFF, 0x0D0D0DFF, 0x0D0D0DFF,
  0xFFFFFFFF, 0xA6FCFFFF, 0xB3ECFFFF, 0xDAABEBFF,
  0xFFA8F9FF, 0xFFABB3FF, 0xFFD2B0FF, 0xFFEFA6FF,
  0xFFF79CFF, 0xD7E895FF, 0xA6EDAFFF, 0xA2F2DAFF,
  0x99FFFCFF, 0xDDDDDDFF, 0x111111FF, 0x111111FF,
};

void Emulator::GetMemory(vector<uint8> *mem) const {
  mem->resize(RAM_BYTE_SIZE);
  memcpy(mem->data(), fc->fceu->RAM, RAM_BYTE_SIZE);
}

vector<uint8> Emulator::GetMemory() const {
  vector<uint8> mem(fc->fceu->RAM, fc->fceu->RAM + RAM_BYTE_SIZE);
  return mem;
}

void Emulator::CopyMemory(std::span<uint8_t> out) const {
  memcpy(out.data(), fc->fceu->RAM, RAM_BYTE_SIZE);
}

uint8 Emulator::ReadRAM(int idx) const {
  return fc->fceu->RAM[idx];
}

void Emulator::SetRAM(int idx, uint8 value) {
  fc->fceu->RAM[idx] = value;
}

static inline uint64 MD5ToChecksum(const uint8 digest[16]) {
  uint64 res = 0ULL;
  for (int i = 0; i < 8; i++) {
    res <<= 8;
    res |= 255 & digest[i];
  }
  return res;
}

uint64 Emulator::ImageChecksum() const {
#if DISABLE_VIDEO
  return 0ULL;
#else
  md5_context ctx;
  md5_starts(&ctx);
  vector<uint8> img = GetImage();
  // n.b. why not use 256x256x4?
  md5_update(&ctx, img.data(), 256 * 240 * 4);
  uint8 digest[16];
  md5_finish(&ctx, digest);
  return MD5ToChecksum(digest);
#endif
}

uint64 Emulator::Registers() const {
  const X6502 *x = fc->X;
  uint64 ret = 0LL;
  ret <<= 16; ret |= x->reg_PC;
  ret <<= 8; ret |= x->reg_A;
  ret <<= 8; ret |= x->reg_X;
  ret <<= 8; ret |= x->reg_Y;
  ret <<= 8; ret |= x->reg_S;
  ret <<= 8; ret |= x->reg_P;
  return ret;
}

uint64 Emulator::MachineChecksum() const {
  md5_context ctx;
  md5_starts(&ctx);

  // All of RAM.
  md5_update(&ctx, fc->fceu->RAM, RAM_BYTE_SIZE);

  // CPU registers. Be insensitive to endianness here.
  uint8 pc_high = fc->X->reg_PC >> 8;
  uint8 pc_low = fc->X->reg_PC & 0xFF;
  md5_update(&ctx, &pc_high, 1);
  md5_update(&ctx, &pc_low, 1);
  md5_update(&ctx, &fc->X->reg_A, 1);
  md5_update(&ctx, &fc->X->reg_X, 1);
  md5_update(&ctx, &fc->X->reg_Y, 1);
  md5_update(&ctx, &fc->X->reg_S, 1);
  md5_update(&ctx, &fc->X->reg_P, 1);
  // Not including PI, which I think is an implementation detail.
  // counts are also excluded.
  md5_update(&ctx, &fc->X->DB, 1);

  // Status registers.
  md5_update(&ctx, fc->ppu->PPU_values, 4);
  // Nametable and palette. I think that these can just be read
  // directly, but for sure the nametable can be tested using
  // sprite 0 hit.
  md5_update(&ctx, fc->ppu->NTARAM, 0x800);
  md5_update(&ctx, fc->ppu->PALRAM, 0x20);

  // This is observable through roundabout means (sprite 0 hit,
  // putting 8+ sprites on a scanline), but also can be read using
  // OAMDATA (wait, can it? I thought it was write-only -tom7 2018).
  md5_update(&ctx, fc->ppu->SPRAM, 0x100);

  uint8 digest[16];
  md5_finish(&ctx, digest);
  return MD5ToChecksum(digest);
}

/**
 * Initialize all of the subsystem drivers: video, audio, and joystick.
 */
// Return true on success.
bool Emulator::DriverInitialize(FCEUGI *gi) {
  // Used to init video. I think it's safe to skip.

  // Here we initialized sound. Assuming it's safe to skip,
  // because of an early return if config turned it off.

  // Used to init joysticks. Don't care about that.

  // No fourscore support.
  // eoptions &= ~EO_FOURSCORE;

  fc->sound->InitSound();

  // Why do both point to the same joydata? -tom
  fc->input->FCEUI_SetInput(0, SI_GAMEPAD, &joydata, 0);
  fc->input->FCEUI_SetInput(1, SI_GAMEPAD, &joydata, 0);

  fc->input->FCEUI_SetInputFourscore(false);
  return true;
}

/* Loads a game, given a full path/filename. The driver code must be
   initialized after the game is loaded, because the emulator code
   provides data necessary for the driver code (number of scanlines to
   render, what virtual input devices to use, etc.).

   Returns true on success. */
bool Emulator::LoadGame(const string &path) {
  fc->fceu->CloseGame();
  fc->fceu->GameInfo = nullptr;

  if (!fc->fceu->FCEUI_LoadGame(path.c_str(), 1)) {
    return false;
  }

  // Here we used to do ParseGIInput, which allows the gameinfo
  // to override our input config, or something like that. No
  // weird stuff. Skip it.

  if (!DriverInitialize(fc->fceu->GameInfo)) {
    return false;
  }

  // Set NTSC (1 = pal). Note that cartridges don't contain this information
  // and some parts of the code tried to figure it out from the presence of
  // (e) or (pal) in the ROM's *filename*. Maybe should be part of the external
  // intface.
  fc->fceu->SetVidSystem(GIV_NTSC);

  return true;
}

// TODO PERF: Looks like Emulator leaks a few hundred bytes
// (tetris.nes, see pingu/tetris/size.cc).
Emulator::~Emulator() {
  fc->fceu->CloseGame();
  fc->fceu->GameInfo = nullptr;

  delete fc;
}

Emulator::Emulator(FC *fc) : fc(fc) {}

Emulator *Emulator::Create(const std::string &romfile) {
  // XXX TODO: If AOT is enabled, then we should give an error if the
  // romfile doesn't match (or just make it possible to have multiple
  // AOT games compiled in, with fallback).
  FC *fc = new FC;

  // initialize the infrastructure
  int error = fc->fceu->Initialize();
  if (error != 1) {
    fprintf(stderr, "Error initializing.\n");
    delete fc;
    return nullptr;
  }

  // (init video was here.)
  // I don't think it's necessary -- just sets up the SDL window and so on.

  // (input config was here.) InputCfg(string value of --inputcfg)

  // UpdateInput(g_config) was here.
  // This is just a bunch of fancy stuff to choose which controllers we have
  // and what they're mapped to.
  // I think the important functions are FCEUD_UpdateInput()
  // and FCEUD_SetInput
  // Calling FCEUI_SetInputFC ((ESIFC) CurInputType[2], InputDPtr, attrib);
  //   and FCEUI_SetInputFourscore ((eoptions & EO_FOURSCORE) != 0);

  fc->palette->ResetPalette();

  // Set NTSC (1 = pal)
  fc->fceu->SetVidSystem(GIV_NTSC);

  // Default. Maybe this should be off, though, as it does appear to
  // affect the CPU (and anyway makes things slower). -tom7  22 May 2016
  fc->ppu->DisableSpriteLimitation(1);

  Emulator *emu = new Emulator(fc);
  // Load the game.
  if (1 != emu->LoadGame(romfile.c_str())) {
    fprintf(stderr, "Couldn't load [%s]\n", romfile.c_str());
    delete emu;
    return nullptr;
  }

  return emu;
}

enum Skip : int {
  // Compute the screen pixels, audio
  DO_VIDEO_AND_SOUND = 0,
  // Limited ability to skip video and sound.
  SKIP_VIDEO_AND_SOUND = 2,
};

// Make one emulator step with the given input.
// Bits from MSB to LSB are
//    RLDUTSBA (Right, Left, Down, Up, sTart, Select, B, A)
void Emulator::Step(uint8 controller1, uint8 controller2) {
  // The least significant byte is player 0 and
  // the bits are in the same order as in the fm2 file.
  joydata = ((uint32)controller2 << 8) | controller1;
  // Emulate a single frame.
  fc->fceu->EmulateFrame(SKIP_VIDEO_AND_SOUND);
}

void Emulator::Step16(uint16 controllers) {
  // The least significant byte is player 0 and
  // the bits are in the same order as in the fm2 file.
  joydata = (uint32)controllers;
  // Emulate a single frame.
  fc->fceu->EmulateFrame(SKIP_VIDEO_AND_SOUND);
}

void Emulator::StepFull(uint8 controller1, uint8 controller2) {
  joydata = ((uint32)controller2 << 8) | controller1;
  // Emulate a single frame.
  fc->fceu->EmulateFrame(DO_VIDEO_AND_SOUND);
}

void Emulator::StepFull16(uint16 controllers) {
  joydata = (uint32)controllers;
  // Emulate a single frame.
  fc->fceu->EmulateFrame(DO_VIDEO_AND_SOUND);
}

const uint8 *Emulator::RawIndexedImage() const {
  return fc->fceu->XBuf;
}

vector<uint8> Emulator::IndexedImage() const {
  vector<uint8> ret;
  ret.resize(256 * 240);
  for (int i = 0; i < 256 * 240; i++)
    ret[i] = fc->fceu->XBuf[i] & INDEX_MASK;
  return ret;
}

void Emulator::GetImage(vector<uint8> *rgba) const {
  if (rgba->size() != IMAGE_BYTE_SIZE) {
    rgba->clear();
    rgba->resize(IMAGE_BYTE_SIZE);
  }

  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      uint8 r, g, b;

      // XBackBuf? or XBuf?
      fc->palette->GetPalette(fc->fceu->XBuf[(y * 256) + x],
                                    &r, &g, &b);

      (*rgba)[y * 256 * 4 + x * 4 + 0] = r;
      (*rgba)[y * 256 * 4 + x * 4 + 1] = g;
      (*rgba)[y * 256 * 4 + x * 4 + 2] = b;
      (*rgba)[y * 256 * 4 + x * 4 + 3] = 0xFF;
    }
  }
}

vector<uint8> Emulator::GetImage() const {
  vector<uint8> ret(IMAGE_BYTE_SIZE);
  GetImage(&ret);
  return ret;
}

void Emulator::GetImageARGB(vector<uint8> *argb) const {
  if (argb->size() != IMAGE_BYTE_SIZE) {
    argb->clear();
    argb->resize(IMAGE_BYTE_SIZE);
  }

  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      uint8 r, g, b;

      // XBackBuf? or XBuf?
      fc->palette->GetPalette(fc->fceu->XBuf[(y * 256) + x],
                                    &r, &g, &b);

      (*argb)[y * 256 * 4 + x * 4 + 0] = b;
      (*argb)[y * 256 * 4 + x * 4 + 1] = g;
      (*argb)[y * 256 * 4 + x * 4 + 2] = r;
      (*argb)[y * 256 * 4 + x * 4 + 3] = 0xFF;
    }
  }
}

vector<uint8> Emulator::GetImageARGB() const {
  vector<uint8> ret(IMAGE_BYTE_SIZE);
  GetImageARGB(&ret);
  return ret;
}

void Emulator::GetSound(vector<int16> *wav) {
  int32 *buffer = nullptr;
  int samples = fc->sound->GetSoundBuffer(&buffer);
  if (buffer == nullptr) {
    fprintf(stderr, "No sound buffer?\n");
    abort();
  }

  if ((int)wav->size() != samples) {
    wav->clear();
    wav->resize(samples);
  }

  for (int i = 0; i < samples; i++) {
    (*wav)[i] = (int16)buffer[i];
  }
}

void Emulator::Save(vector<uint8> *out) {
  SaveEx(out, nullptr);
}

void Emulator::GetBasis(vector<uint8> *out) {
  fc->state->SaveRAW(out);
}

void Emulator::SaveUncompressed(vector<uint8> *out) const {
  fc->state->SaveRAW(out);
}

vector<uint8> Emulator::SaveUncompressed() const {
  vector<uint8> ret;
  SaveUncompressed(&ret);
  return ret;
}

void Emulator::LoadUncompressed(const vector<uint8> &in) {
  if (!fc->state->LoadRAW(in)) {
    fprintf(stderr, "Couldn't restore from state\n");
    abort();
  }
}

void Emulator::Load(const vector<uint8> &state) {
  LoadEx(nullptr, state);
}

uint32 Emulator::GetXScroll() const {
  const PPU *ppu = fc->ppu;
  const uint8 ppu_ctrl = ppu->PPU_values[0];
  // Nametable select.
  const uint8 xtable_select = !!(ppu_ctrl & 1);
  const uint8 xpos = ppu->GetXScroll8();

  return (xtable_select << 8) | xpos;
}

uint32 Emulator::GetYScroll() const {
  const PPU *ppu = fc->ppu;
  const uint8 ppu_ctrl = ppu->PPU_values[0];
  const uint8 ytable_select = (ppu_ctrl & 2) ? 240 : 0;
  const uint8 ypos = ppu->GetYScroll8();

  return ytable_select + (uint32)ypos;
}

const char *Emulator::Opcode(uint8_t op) {
  return Opcodes::opcode_name[op];
}

vector<uint8> Emulator::OAM() const {
  vector<uint8> oam;
  oam.resize(256);
  memcpy(oam.data(), fc->ppu->SPRAM, 256);
  return oam;
}

std::vector<Emulator::Sprite> Emulator::Sprites() const {
  const PPU *ppu = fc->ppu;
  std::span<const uint8_t> oam(ppu->SPRAM, 256);
  const uint8_t ppu_ctrl = ppu->PPU_values[0];
  const bool tall_sprites = !!(ppu_ctrl & (1 << 5));

  // Note: This is ignored if sprites are tall (and determined instead
  // from the tile's low bit).
  const bool spr_pat_high = !!(ppu_ctrl & (1 << 3));

  const uint8_t *palette_table = fc->ppu->PALRAM;

  std::vector<Sprite> sprites;
  sprites.reserve(64);
  for (int s = 0; s < 64; s++) {
    Sprite sprite;
    sprite.y = oam[s * 4 + 0];
    sprite.tile_idx = oam[s * 4 + 1];
    uint8_t attrs = oam[s * 4 + 2];
    sprite.palette_idx = attrs & 0b00000011;
    sprite.behind_background = !!(attrs & 0b00100000);
    sprite.flip_horiz = !!(attrs & 0b01000000);
    sprite.flip_vert = !!(attrs & 0b10000000);
    sprite.x = oam[s * 4 + 3];

    sprite.tall = tall_sprites;
    sprite.rgba.resize(8 * (tall_sprites ? 16 : 8));

    // Render one 8x8 tile into the RGBA pointer, using pattern table $0000
    // if first arg is false, $1000 if true. The tile index is the
    // index into that pattern.
    auto OneTile = [this, &sprite, palette_table](
        bool patterntable_high,
        uint8_t tile_idx,
        uint32_t *rgba) {

        const uint32_t spr_pat_addr = patterntable_high ? 0x1000 : 0x0000;
        // PERF Really need to keep computing this?
        const uint8_t *vram = fc->cart->VPagePointer(spr_pat_addr);

        // This came from code that could write the sprite anywhere in
        // some output image, but we always place it at (0, 0).
        //
        // upper-left corner of this tile within the rgba array.
        // const int x0 = xdest - root.min_x;
        // const int y0 = ydest - root.min_y;
        static constexpr int x0 = 0;
        static constexpr int y0 = 0;

        const int addr = tile_idx * 16;
        for (int row = 0; row < 8; row++) {
          const uint8_t row_low = vram[addr + row];
          const uint8_t row_high = vram[addr + row + 8];

          // bit from msb to lsb.
          for (int bit = 0; bit < 8; bit++) {
            const uint8_t value =
              ((row_low >> (7 - bit)) & 1) |
              (((row_high >> (7 - bit)) & 1) << 1);

            const int px = sprite.flip_horiz ? x0 + (7 - bit) : (x0 + bit);
            const int py = sprite.flip_vert ? y0 + (7 - row) : (y0 + row);

            const int pixel_idx = py * 8 + px;

            // For sprites, transparent pixels need to be drawn with
            // alpha 0. The palette doesn't matter; 0 means transparent
            // in every palette.
            if (value == 0) {
              rgba[pixel_idx] = 0x00000000;
            } else {
              // Offset with palette table. Sprite palette entries come
              // after the bg ones, so add 0x10.
              const uint8_t palette_idx = 0x10 + ((sprite.palette_idx << 2) | value);
              // ID of global NES color gamut.
              const uint8_t color_id = palette_table[palette_idx];

              // Put pixel in sprite image. These have alpha=FF.
              rgba[pixel_idx] = ntsc_palette_rgba[color_id];
            }
          }
        }
      };

    // Now draw one or two tiles.
    if (tall_sprites) {
      // Odd and even tile numbers are treated differently.
      if ((sprite.tile_idx & 1) == 0) {
        // This page:
        // http://noelberry.ca/nes
        // verifies that tiles t and t+1 are drawn top then bottom.
        if (sprite.flip_vert) {
          // in y-flip scenarios, we have to flip the
          // y positions here so that the whole 8x16 sprite is flipping,
          // rather than its two 8x8 components. So tile_idx actually goes
          // on bottom.
          OneTile(false, sprite.tile_idx, sprite.rgba.data() + 8 * 8);
          OneTile(false, sprite.tile_idx + 1, sprite.rgba.data());
        } else {
          OneTile(false, sprite.tile_idx, sprite.rgba.data());
          OneTile(false, sprite.tile_idx + 1, sprite.rgba.data() + 8 * 8);
        }
      } else {
        // XXX I assume this drops the low bit? I don't see that
        // documented but it wouldn't really make sense otherwise
        // (unless tile 255 wraps to 0?)
        if (sprite.flip_vert) {
          OneTile(true, sprite.tile_idx - 1, sprite.rgba.data() + 8 * 8);
          OneTile(true, sprite.tile_idx, sprite.rgba.data());
        } else {
          OneTile(true, sprite.tile_idx - 1, sprite.rgba.data());
          OneTile(true, sprite.tile_idx, sprite.rgba.data() + 8 * 8);
        }
      }
    } else {
      // this is much easier but not used in zelda
      // (I think it's needed for mario though)
      OneTile(spr_pat_high, sprite.tile_idx, sprite.rgba.data());
    }

    sprites.push_back(sprite);
  }
  return sprites;
};


#if USE_COMPRESSION

void Emulator::SaveEx(const vector<uint8> *basis, vector<uint8> *state) {
  // TODO PERF
  // Saving is not as efficient as we'd like for a pure in-memory operation
  //  - uses tags to tell you what's next, even though we could already know
  //  - takes care for endianness; no point
  //  - saves lots of pointless stuff we don't need (sound?, both PPUs, probably
  //    all mapper data even if we're not using it)

  vector<uint8> raw;
  fc->state->SaveRAW(&raw);

  // Encode.
  int blen = (basis == nullptr) ? 0 : (min(basis->size(), raw.size()));
  for (int i = 0; i < blen; i++) {
    raw[i] -= (*basis)[i];
  }

  // Compress.
  int len = raw.size();
  // worst case compression:
  // zlib says "0.1% larger than sourceLen plus 12 bytes"
  uLongf comprlen = (len >> 9) + 12 + len;

  // Make sure there is contiguous space. Need room for header too.
  state->resize(4 + comprlen);

  if (Z_OK != compress2(&(*state)[4], &comprlen, raw.data(), len,
                        Z_DEFAULT_COMPRESSION)) {
    fprintf(stderr, "Couldn't compress.\n");
    abort();
  }

  *(uint32*)state->data() = len;

  // Trim to what we actually needed.
  state->resize(4 + comprlen);
  state->shrink_to_fit();
}

void Emulator::LoadEx(const vector<uint8> *basis, const vector<uint8> &state) {
  // Decompress. First word tells us the decompressed size.
  int uncomprlen = *(const uint32*)state.data();
  vector<uint8> uncompressed;
  uncompressed.resize(uncomprlen);
  uLongf uncomprlenf = uncomprlen;

  switch (uncompress(uncompressed.data(), &uncomprlenf,
                     &state[4], state.size() - 4)) {
  case Z_OK: break;
  case Z_BUF_ERROR:
    fprintf(stderr, "zlib: Not enough room in output. Uncompressed length\n"
            "is supposedly %d.\n", uncomprlen);
    abort();
    break;
  case Z_MEM_ERROR:
    fprintf(stderr, "Not enough memory\n");
    abort();
    break;
  default:
    fprintf(stderr, "Unknown decompression error\n");
    abort();
    break;
  }
  // fprintf(stderr, "After uncompression: %d\n", uncomprlen);

  // Why doesn't this equal the result from before?
  uncompressed.resize(uncomprlen);

  // Decode.
  int blen = (basis == nullptr) ? 0 : (min(basis->size(), uncompressed.size()));
  for (int i = 0; i < blen; i++) {
    uncompressed[i] += (*basis)[i];
  }

  if (!fc->state->LoadRAW(uncompressed)) {
    fprintf(stderr, "Couldn't restore from state\n");
    abort();
  }
}

#else

// When compression is disabled, we ignore the basis (no point) and
// don't store any size header. These functions become very simple.
void Emulator::SaveEx(vector<uint8> *state, const vector<uint8> *basis) {
  fc->state->FCEUSS_SaveRAW(out);
}

void Emulator::LoadEx(vector<uint8> *state, const vector<uint8> *basis) {
  if (!fc->state->FCEUSS_LoadRAW(*state)) {
    fprintf(stderr, "Couldn't restore from state\n");
    abort();
  }
}


#endif
