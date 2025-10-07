
#ifndef _FCEULIB_MMC3_H
#define _FCEULIB_MMC3_H

#include <cstdint>
#include <vector>

#include "cart.h"
#include "fc.h"
#include "fceu.h"
#include "state.h"

struct MMC3 : public CartInterface {

  uint8_t MMC3_cmd = 0;
  uint8_t mmc3opts = 0;
  uint8_t A000B = 0;
  uint8_t A001B = 0;
  uint8_t DRegBuf[8] = {};

  static void MMC3_CMDWrite(DECLFW_ARGS);
  static void MMC3_IRQWrite(DECLFW_ARGS);

  virtual void PWrap(uint32_t A, uint8_t V);
  virtual void CWrap(uint32_t A, uint8_t V);
  virtual void MWrap(uint8_t V);

  void Close() override;
  void Reset() override;
  void Power() override;

  MMC3(FC *fc, CartInfo *info, int prg, int chr, int wram, int battery);

 protected:
  void FixMMC3PRG(int V);
  void FixMMC3CHR(int V);

  DECLFW_RET MMC3_CMDWrite_Direct(DECLFW_ARGS);
  DECLFW_RET MMC3_IRQWrite_Direct(DECLFW_ARGS);

  int isRevB = 1;

  uint8_t *MMC3_WRAM = nullptr;
  uint8_t *CHRRAM = nullptr;
  uint32_t CHRRAMSize = 0;

  uint8_t irq_count = 0, irq_latch = 0, irq_a = 0;
  uint8_t irq_reload = 0;

 private:
  std::vector<SFORMAT> MMC3_StateRegs;

  DECLFW_RET MBWRAMMMC6(DECLFW_ARGS);
  DECLFR_RET MAWRAMMMC6(DECLFR_ARGS);

  void GenMMC3Restore(FC *fc, int version);

  void ClockMMC3Counter();
  int wrams = 0;
};

#endif
