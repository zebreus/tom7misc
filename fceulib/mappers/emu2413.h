#ifndef _FCEULIB_EMU2413_H
#define _FCEULIB_EMU2413_H

#include <cstdint>

/* voice data */
struct OPLL_PATCH {
  uint32_t TL, FB, EG, ML, AR, DR, SL, RR, KR, KL, AM, PM, WF;
};

struct OPLL_SLOT {
  OPLL_PATCH patch;

  int32_t type; /* 0 : modulator 1 : carrier */

  /* OUTPUT */
  int32_t feedback;
  int32_t output[2]; /* Output value of slot */

  /* for Phase Generator (PG) */
  uint16_t *sintbl; /* Wavetable */
  uint32_t phase; /* Phase */
  uint32_t dphase; /* Phase increment amount */
  uint32_t pgout; /* output */

  /* for Envelope Generator (EG) */
  int32_t fnum; /* F-Number */
  int32_t block; /* Block */
  int32_t volume; /* Current volume */
  int32_t sustine; /* Sustine 1 = ON, 0 = OFF */
  uint32_t tll; /* Total Level + Key scale level*/
  uint32_t rks; /* Key scale offset (Rks) */
  int32_t eg_mode; /* Current state */
  uint32_t eg_phase; /* Phase */
  uint32_t eg_dphase; /* Phase increment amount */
  uint32_t egout; /* output */
};

struct OPLL {
  uint32_t adr;
  int32_t out;

  uint32_t realstep;
  uint32_t oplltime;
  uint32_t opllstep;
  int32_t prev, next;

  /* Register */
  uint8_t LowFreq[6];
  uint8_t HiFreq[6];
  uint8_t InstVol[6];

  uint8_t CustInst[8];

  int32_t slot_on_flag[6 * 2];

  /* Pitch Modulator */
  uint32_t pm_phase;
  int32_t lfo_pm;

  /* Amp Modulator */
  int32_t am_phase;
  int32_t lfo_am;

  uint32_t quality;

  /* Channel Data */
  int32_t patch_number[6];
  int32_t key_status[6];

  /* Slot */
  OPLL_SLOT slot[6 * 2];

  uint32_t mask;
};

/* Size of Sintable ( 8 -- 18 can be used. 9 recommended.)*/
#define PG_BITS 9
#define PG_WIDTH (1 << PG_BITS)
/* Bits for Pitch and Amp modulator */
#define PM_PG_BITS 8
#define PM_PG_WIDTH (1 << PM_PG_BITS)
#define PM_DP_BITS 16
#define PM_DP_WIDTH (1 << PM_DP_BITS)
#define AM_PG_BITS 8
#define AM_PG_WIDTH (1 << AM_PG_BITS)
#define AM_DP_BITS 16
#define AM_DP_WIDTH (1 << AM_DP_BITS)

/* Dynamic range (Accuracy of sin table) */
#define DB_BITS 8
#define DB_STEP (48.0 / (1 << DB_BITS))
#define DB_MUTE (1 << DB_BITS)

/* Dynamic range of envelope */
#define EG_STEP 0.375
#define EG_BITS 7
#define EG_MUTE (1 << EG_BITS)

/* Dynamic range of total level */
#define TL_STEP 0.75
#define TL_BITS 6
#define TL_MUTE (1 << TL_BITS)

struct EMU2413 {

  /* Create Object */
  OPLL *OPLL_new(uint32_t clk, uint32_t rate);
  void OPLL_delete(OPLL *);

  /* Setup */
  void OPLL_reset(OPLL *);
  void OPLL_set_rate(OPLL *opll, uint32_t r);
  // void OPLL_set_quality(OPLL *opll, uint32_t q);

  /* Port/Register access */
  // void OPLL_writeIO(OPLL *, uint32_t reg, uint32_t val);
  void OPLL_writeReg(OPLL *, uint32_t reg, uint32_t val);

  /* Misc */
  void OPLL_forceRefresh(OPLL *);

  void OPLL_FillBuffer(OPLL *opll, int32_t *buf, int32_t len, int shift);

private:
  void makeAdjustTable();
  void makeDB2LinTable();
  int32_t lin2db(double d);
  void makeSinTable();
  void makePmTable();
  void makeAmTable();
  void makeDphaseTable();
  void makeRksTable();
  void makeTllTable();
  void makeDphaseARTable();
  void makeDphaseDRTable();
  uint32_t calc_eg_dphase(OPLL_SLOT *slot);
  void slotOn(OPLL_SLOT *slot);
  void slotOff(OPLL_SLOT *slot);
  void keyOn(OPLL *opll, int32_t i);
  void keyOff(OPLL *opll, int32_t i);
  void setSustine(OPLL *opll, int32_t c, int32_t sustine);
  void setVolume(OPLL *opll, int32_t c, int32_t volume);
  void setFnumber(OPLL *opll, int32_t c, int32_t fnum);
  void setBlock(OPLL *opll, int32_t c, int32_t block);
  void update_key_status(OPLL *opll);
  void OPLL_SLOT_reset(OPLL_SLOT *slot, int type);
  void internal_refresh();
  void maketables(uint32_t c, uint32_t r);
  void setInstrument(OPLL *opll, uint32_t i, uint32_t inst);
  int16_t calc(OPLL *opll);
  int32_t calc_slot_mod(OPLL_SLOT *slot);
  void update_ampm(OPLL *opll);
  void calc_phase(OPLL_SLOT *slot, int32_t lfo);
  void calc_envelope(OPLL_SLOT *slot, int32_t lfo);
  int32_t calc_slot_car(OPLL_SLOT *slot, int32_t fm);

  /* Input clock */
  uint32_t clk = 844451141;
  /* Sampling rate */
  uint32_t rate = 3354932;

  /* WaveTable for each envelope amp */
  uint16_t fullsintable[PG_WIDTH] = {};
  uint16_t halfsintable[PG_WIDTH] = {};

  uint16_t *waveform[2] = {fullsintable, halfsintable};

  /* LFO Table */
  int32_t pmtable[PM_PG_WIDTH] = {};
  int32_t amtable[AM_PG_WIDTH] = {};

  /* Phase delta for LFO */
  uint32_t pm_dphase = 0;
  uint32_t am_dphase = 0;

  /* dB to Liner table */
  int16_t DB2LIN_TABLE[(DB_MUTE + DB_MUTE) * 2] = {};

  /* Liner to Log curve conversion table (for Attack rate). */
  uint16_t AR_ADJUST_TABLE[1 << EG_BITS] = {};

  /* Phase incr table for Attack */
  uint32_t dphaseARTable[16][16] = {};
  /* Phase incr table for Decay and Release */
  uint32_t dphaseDRTable[16][16] = {};

  /* KSL + TL Table */
  uint32_t tllTable[16][8][1 << TL_BITS][4] = {};
  int32_t rksTable[2][8][2] = {};

  /* Phase incr table for PG */
  uint32_t dphaseTable[512][8][16] = {};
};

#endif
