#ifndef _FCEULIB_MMC5_H
#define _FCEULIB_MMC5_H

#include <cstdint>

#include "cart.h"
#include "fc.h"
#include "fceu.h"

// Unlike most mappers, this one has a "public" interface so that
// hacks in the PPU can commune with it. It's gross. I've tried to
// move as much MMC5-specific stuff in here as possible, but it's
// rather a mess. -tom7

struct MMC5 final : public CartInterface {
 public:
  // PPU.
  void MMC5HackHB(int scanline);

  const uint8_t *SPRVPagePtr(uint32_t v) { return &MMC5SPRVPage[v >> 10][v]; }
  const uint8_t *BGVPagePtr(uint32_t v) { return &MMC5BGVPage[v >> 10][v]; }

  void Power() final override;

  // Should just be used by internal mapper creators.
  MMC5(FC *fc, CartInfo *info, int wsize, int battery);

 private:

  // This used to be in Cart and referenced from PPU, but I moved it
  // here in an attempt to keep MMC5 hacks in MMC5 as much as
  // possible. These are video pages (presumably sprite and bg)
  // indexed like [A >> 10][A] (the addresses are pre-offset by
  // subtracting A when initializing them). -tom7
  uint8_t *MMC5SPRVPage[8] = {};
  uint8_t *MMC5BGVPage[8] = {};

  struct MMC5APU {
    uint16_t wl[2] = {};
    uint8_t env[2] = {};
    uint8_t enable = 0;
    uint8_t running = 0;
    uint8_t raw = 0;
    uint8_t rawcontrol = 0;
    int32_t dcount[2] = {};
    int32_t BC[3] = {};
    int32_t vcount[2] = {};
  };

  MMC5APU MMC5Sound;

  uint8_t PRGBanks[4] = {};
  uint8_t WRAMPage = 0;
  uint16_t CHRBanksA[8] = {}, CHRBanksB[4] = {};
  uint8_t WRAMMaskEnable[2] = {};
  // Used in ppu -tom7
  // uint8_t mmc5ABMode = 0; /* A=0, B=1 */

  uint8_t IRQScanline = 0, IRQEnable = 0;
  uint8_t CHRMode = 0, NTAMirroring = 0, NTFill = 0, ATFill = 0;

  uint8_t MMC5IRQR = 0;
  uint8_t MMC5LineCounter = 0;
  uint8_t mmc5psize = 0, mmc5vsize = 0;
  uint8_t mul[2] = {};

  // PERF Initial backing ram for SPRVpage and BGVPage. I copied this
  // from cart. Perhaps it's not necessary if this mapper always
  // initializes them? -tom7
  uint8_t nothing[8192] = {};

  uint8_t *WRAM = nullptr;
  uint8_t *MMC5fill = nullptr;
  uint8_t *ExRAM = nullptr;

  uint8_t MMC5WRAMsize = 0;
  uint8_t MMC5WRAMIndex[8] = {};

  uint8_t MMC5ROMWrProtect[4] = {};
  uint8_t MMC5MemIn[5] = {};

  void (MMC5::*sfun)(int P) = nullptr;
  void (MMC5::*psfun)() = nullptr;

  inline void MMC5SPRVROM_BANK1(uint32_t A, uint32_t V) {
    if (fc->cart->CHRptr[0]) {
      V &= fc->cart->CHRmask1[0];
      MMC5SPRVPage[(A) >> 10] =
        &fc->cart->CHRptr[0][(V) << 10] - (A);
    }
  }

  inline void MMC5BGVROM_BANK1(uint32_t A, uint32_t V) {
    if (fc->cart->CHRptr[0]) {
      V &= fc->cart->CHRmask1[0];
      MMC5BGVPage[(A) >> 10] =
        &fc->cart->CHRptr[0][(V) << 10] - (A);
    }
  }

  inline void MMC5SPRVROM_BANK2(uint32_t A, uint32_t V) {
    if (fc->cart->CHRptr[0]) {
      V &= fc->cart->CHRmask2[0];
      MMC5SPRVPage[(A) >> 10] =
        MMC5SPRVPage[((A) >> 10) + 1] =
        &fc->cart->CHRptr[0][(V) << 11] - (A);
    }
  }

  inline void MMC5BGVROM_BANK2(uint32_t A, uint32_t V) {
    if (fc->cart->CHRptr[0]) {
      V &= fc->cart->CHRmask2[0];
      MMC5BGVPage[(A) >> 10] =
        MMC5BGVPage[((A) >> 10) + 1] =
        &fc->cart->CHRptr[0][(V) << 11] - (A);
    }
  }

  inline void MMC5SPRVROM_BANK4(uint32_t A, uint32_t V) {
    if (fc->cart->CHRptr[0]) {
      V &= fc->cart->CHRmask4[0];
      MMC5SPRVPage[(A) >> 10] =
        MMC5SPRVPage[((A) >> 10) + 1] =
        MMC5SPRVPage[((A) >> 10) + 2] =
        MMC5SPRVPage[((A) >> 10) + 3] =
        &fc->cart->CHRptr[0][(V) << 12] - (A);
    }
  }

  inline void MMC5BGVROM_BANK4(uint32_t A, uint32_t V) {
    if (fc->cart->CHRptr[0]) {
      V &= fc->cart->CHRmask4[0];
      MMC5BGVPage[(A) >> 10] =
        MMC5BGVPage[((A) >> 10) + 1] =
        MMC5BGVPage[((A) >> 10) + 2] =
        MMC5BGVPage[((A) >> 10) + 3] =
        &fc->cart->CHRptr[0][(V) << 12] - (A);
    }
  }

  inline void MMC5SPRVROM_BANK8(uint32_t V) {
    if (fc->cart->CHRptr[0]) {
      V &= fc->cart->CHRmask8[0];
      MMC5SPRVPage[0] = MMC5SPRVPage[1] =
        MMC5SPRVPage[2] = MMC5SPRVPage[3] =
        MMC5SPRVPage[4] = MMC5SPRVPage[5] =
        MMC5SPRVPage[6] = MMC5SPRVPage[7] =
        &fc->cart->CHRptr[0][(V) << 13];
    }
  }

  inline void MMC5BGVROM_BANK8(uint32_t V) {
    if (fc->cart->CHRptr[0]) {
      V &= fc->cart->CHRmask8[0];
      MMC5BGVPage[0] = MMC5BGVPage[1] =
        MMC5BGVPage[2] = MMC5BGVPage[3] =
        MMC5BGVPage[4] = MMC5BGVPage[5] =
        MMC5BGVPage[6] = MMC5BGVPage[7] =
        &fc->cart->CHRptr[0][(V) << 13];
    }
  }

  void BuildWRAMSizeTable();
  void MMC5CHRA();
  void MMC5CHRB();
  void MMC5WRAM(uint32_t A, uint32_t V);
  void MMC5PRG();
  void Mapper5_write(DECLFW_ARGS);

  DECLFR_RET MMC5_ReadROMRAM(DECLFR_ARGS);
  void MMC5_WriteROMRAM(DECLFW_ARGS);
  void MMC5_ExRAMWr(DECLFW_ARGS);

  DECLFR_RET MMC5_ExRAMRd(DECLFR_ARGS);
  DECLFR_RET MMC5_read(DECLFR_ARGS);

  void MMC5Synco();

  static void MMC5_StateRestore(FC *fc, int version);

  void Do5PCM();
  void Do5PCMHQ();
  void Mapper5_SW(DECLFW_ARGS);
  void Do5SQ(int P);
  void Do5SQHQ(int P);
  void MMC5RunSoundHQ();
  void MMC5HiSync(int32_t ts);
  void MMC5RunSound(int count);
  void Mapper5_ESI();
};

#endif
