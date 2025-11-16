#ifndef _FCEULIB_CART_H
#define _FCEULIB_CART_H

#include <cstdint>

#include "fc.h"
#include "types.h"
#include "fceu.h"

struct CartInterface {
  explicit CartInterface(FC *fc) : fc(fc) {}
  virtual ~CartInterface() {}
  /* Set by mapper/board code: */
  virtual void Power() {}
  virtual void Reset() {}
  virtual void Close() {}

 protected:
  FC *fc = nullptr;
 private:
  CartInterface() = delete;
};

// Same idea, but for old _init style mappers that
// do their work by modifying global variables.
struct MapInterface {
  explicit MapInterface(FC *fc) : fc(fc) {}
  virtual ~MapInterface() {}
  virtual void StateRestore(int version) {}
  virtual void MapperReset() {}
  virtual void MapperClose() {}
 protected:
  FC *fc = nullptr;
 private:
  MapInterface() = delete;
};

struct CartInfo {
  // Maybe some of this should go into CartInterface.

  /* Pointers to memory to save/load. */
  uint8_t *SaveGame[4];
  /* How much memory to save/load. */
  uint32_t SaveGameLen[4];

  /* Set by iNES/UNIF loading code. */
  /* As set in the header or chunk.
     iNES/UNIF specific.  Intended
     to help support games like "Karnov"
     that are not really MMC3 but are
     set to mapper 4. */
  int mirror;
  /* Presence of an actual battery. */
  int battery;
  uint8_t MD5[16];
  /* Should be set by the iNES/UNIF loading
     code, used by mapper/board code, maybe
     other code in the future. */
  uint32_t CRC32;
};

struct Cart {
  static constexpr int MIRROR_H = 0;
  static constexpr int MIRROR_V = 1;
  static constexpr int MIRROR_0 = 2;
  static constexpr int MIRROR_1 = 3;

  void SaveGameSave(CartInfo *LocalHWInfo);
  void LoadGameSave(CartInfo *LocalHWInfo);
  void ClearGameSave(CartInfo *LocalHWInfo);

  // Should use these accessors instead of modifying the pages
  // directly.
  void WritePage(uint32_t A, uint8_t V) { Page[A >> 11][A] = V; }
  uint8_t ReadPage(uint32_t A) const { return Page[A >> 11][A]; }

  void WriteVPage(uint32_t A, uint8_t V) { VPage[A >> 10][A] = V; }
  uint8_t ReadVPage(uint32_t A) const { return VPage[A >> 10][A]; }
  const uint8_t *VPagePointer(uint32_t A) const { return &VPage[A >> 10][A]; }
  // TODO: Gross, but better than just modifying VPage from afar.
  // Maybe can update callers to use setvramb*.
  void SetVPage(uint32_t A, uint8_t *p) { VPage[A >> 10] = p - A; }
  // Ugh, even worse!
  void SetSpecificVPage(int num, uint32_t A, uint8_t *p) { VPage[num] = p - A; }

 private:
  // Each page is a 2k chunk of memory, corresponding to the address
  // (A >> 11), but the pointer is offset such that it is still
  // indexed by A, not A & 2047. (TODO: verify, and maybe "fix" -tom7)
  // TODO: Make private and use accessors so that we can either keep
  // the address offsetting trick internal, or even stamp it out
  // TODO: In the process of making these private. -tom7
  uint8_t *Page[32] = {};
  uint8_t *VPage[8] = {};

 public:
  // A cartridge consists of a set of PRG and CHR (video) ROMs (or RAMs),
  // each usually a chip on the board. These can be set up by the mapper
  // or by the cart format itself (e.g., iNES always sets chip 0 to "the
  // ROM" and the unif format describes the chips with metadata).
  void ResetCartMapping();
  void SetupCartPRGMapping(int chip, uint8_t *p, uint32_t size, bool is_ram);
  void SetupCartCHRMapping(int chip, uint8_t *p, uint32_t size, bool is_ram);
  // m is sometimes one of the mirroring types, sometimes 4; document!
  void SetupCartMirroring(int m, int hard, uint8_t *extra);

  // Maybe should always be true? -tom7
  static constexpr bool disableBatteryLoading = false;

  uint8_t *PRGptr[32] = {};
  uint8_t *CHRptr[32] = {};

  uint32_t PRGsize[32] = {};
  uint32_t CHRsize[32] = {};

  uint32_t PRGmask2[32] = {};
  uint32_t PRGmask4[32] = {};
  uint32_t PRGmask8[32] = {};
  uint32_t PRGmask16[32] = {};
  uint32_t PRGmask32[32] = {};

  uint32_t CHRmask1[32] = {};
  uint32_t CHRmask2[32] = {};
  uint32_t CHRmask4[32] = {};
  uint32_t CHRmask8[32] = {};


  // These functions perform bank switching. The versions without r
  // just assume r=0. A is the base address that gets switched (this
  // is basically always a constant at the call site). V is the value,
  // which I think is like the bank number to select. (In UNROM, we
  // use latch & 7, but then also ~0! It gets anded with one of the
  // PRGmasks, though.)
  //
  // 2, 4, 8, 16, 32 seem to refer to 2k, 4k, 8k, 16k and 32k banks.
  //
  // I haven't figured it out beyond that.
  // -tom7
  void setprg2(uint32_t A, uint32_t V);
  void setprg4(uint32_t A, uint32_t V);
  void setprg8(uint32_t A, uint32_t V);
  void setprg16(uint32_t A, uint32_t V);
  void setprg32(uint32_t A, uint32_t V);

  void setprg2r(int r, unsigned int A, unsigned int V);
  void setprg4r(int r, unsigned int A, unsigned int V);
  void setprg8r(int r, unsigned int A, unsigned int V);
  void setprg16r(int r, unsigned int A, unsigned int V);
  void setprg32r(int r, unsigned int A, unsigned int V);

  void setchr1r(int r, unsigned int A, unsigned int V);
  void setchr2r(int r, unsigned int A, unsigned int V);
  void setchr4r(int r, unsigned int A, unsigned int V);
  void setchr8r(int r, unsigned int V);

  void setchr1(unsigned int A, unsigned int V);
  void setchr2(unsigned int A, unsigned int V);
  void setchr4(unsigned int A, unsigned int V);
  void setchr8(unsigned int V);

  void setvram4(uint32_t A, uint8_t *p);
  void setvram8(uint8_t *p);

  void setvramb1(uint8_t *p, uint32_t A, uint32_t b);
  void setvramb2(uint8_t *p, uint32_t A, uint32_t b);
  void setvramb4(uint8_t *p, uint32_t A, uint32_t b);
  void setvramb8(uint8_t *p, uint32_t b);

  void setmirror(int t);
  void setmirrorw(int a, int b, int c, int d);
  void setntamem(uint8_t *p, int ram, uint32_t b);

  Cart(FC *fc);

  // Write to or read from the mapped address. The BROB version (OB is
  // presumably "out of bounds") returns the current value of the data
  // bus when reading from an unmapped page.
  DECLFR_RET CartBR_Direct(DECLFR_ARGS);
  DECLFR_RET CartBROB_Direct(DECLFR_ARGS);
  DECLFW_RET CartBW_Direct(DECLFW_ARGS);

  // TODO: Kill these static versions.
  static DECLFW_RET CartBW(DECLFW_ARGS);
  static DECLFR_RET CartBR(DECLFR_ARGS);
  static DECLFR_RET CartBROB(DECLFR_ARGS);

 private:
  bool PRGIsRAM[32] = { };  /* This page is/is not PRG RAM. */

  // Used as an initial destination for page and vpage.
  uint8_t nothing[8192] = { };

  /* 16 are (sort of) reserved for UNIF/iNES and 16 to map other stuff. */
  bool CHRram[32] = { };
  bool PRGram[32] = { };

  int mirrorhard = 0;

  void SetPagePtr(int s, uint32_t A, uint8_t *p, bool is_ram);

  FC *fc;
};

#endif
