/* FCE Ultra - NES/Famicom Emulator
*
* Copyright notice for this file:
*  Copyright (C) 2002 Xodnizel
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

//  TODO: Add (better) file io error checking

#include <bit>
#include <cstdint>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>

#include <cassert>
#include <vector>

#include "driver.h"
#include "emufile.h"
#include "fc.h"
#include "fceu.h"
#include "input.h"
#include "ppu.h"
#include "sound.h"
#include "state.h"
#include "types.h"
#include "utils/endian.h"
#include "version.h"
#include "x6502.h"

#include "tracing.h"

using namespace std;

// Write the vector to the output file. If the file pointer is
// null, just return the size.
int FCState::SubWrite(EmuFile *os, const vector<SFORMAT> &sf) {
  uint32 acc = 0;

  TRACE_SCOPED_STAY_ENABLED_IF(false);

  for (const SFORMAT &f : sf) {
    // Port note: This used to be a nested subtree.
    assert(f.s != ~(uint32)0);

    const size_t size = f.s & ~FCEUSTATE_FLAGS;

    // 8 bytes for tag + size
    acc += 8;
    acc += size;

    // Are we writing or calculating the size of this block?
    if (os != nullptr) {
      os->fwrite(f.desc.data(), 4);
      write32le(size, os);

      // TRACE_SCOPED_ENABLE_IF(f.desc[2] == 'P' && f.desc[3] == 'C');
      // TRACE("{} for {}", f.desc, f.s & ~FCEUSTATE_FLAGS);

      TRACEA((uint8 *)f.v, size);

      static_assert(std::endian::native == std::endian::big ||
                    std::endian::native == std::endian::little,
                    "This code needs to know the native byte order.");

      if (std::endian::native == std::endian::big &&
          !!(f.s & FCEUSTATE_RLSB)) {
        // On a big endian system, we need to reverse the byte order
        // for values flagged as such (e.g. uint32 and uint16).
        std::vector<uint8_t> bytes(size, 0);
        memcpy(bytes.data(), (uint8_t*)f.v, size);
        FlipByteOrder(bytes.data(), size);
        os->fwrite((char *)bytes.data(), size);
      } else {
        // Little-endian systems, or it's just plain bytes.
        os->fwrite((char *)f.v, size);
      }
    }
  }

  return acc;
}

// Write all the sformats to the output file.
int FCState::WriteStateChunk(EmuFile *os, int type,
                           const vector<SFORMAT> &sf) {
  os->fputc(type);
  const int bsize = SubWrite(nullptr, sf);
  write32le(bsize, os);
  // TRACE("Write {} etc. sized {}", sf->desc, bsize);

  if (!SubWrite(os, sf)) {
    return 5;
  }
  return bsize + 5;
}

// Find the SFORMAT structure with the name 'desc', if any.
const SFORMAT *FCState::CheckS(const vector<SFORMAT> &sf,
                             uint32 tsize, SKEY desc) {
  for (const SFORMAT &f : sf) {
    assert(f.s != ~(uint32)0);
    if (f.desc == desc) {
      if (tsize != (f.s & (~FCEUSTATE_FLAGS))) return nullptr;
      return &f;
    }
  }
  return nullptr;
}

bool FCState::ReadStateChunk(EmuFile *is,
                             const vector<SFORMAT> &sf, int size) {
  int temp = is->ftell();

  while (is->ftell() < temp + size) {
    uint32 tsize;
    SKEY toa;
    if (is->fread(toa.data(), 4) < 4) return false;

    read32le(&tsize, is);

    if (const SFORMAT *tmp = CheckS(sf, tsize, toa)) {
      is->fread((char *)tmp->v, tmp->s & (~FCEUSTATE_FLAGS));

      if (std::endian::native == std::endian::big &&
          !!(tmp->s & FCEUSTATE_RLSB)) {
        FlipByteOrder((uint8 *)tmp->v, tmp->s & (~FCEUSTATE_FLAGS));
      }
    } else {
      is->fseek(tsize, SEEK_CUR);
    }
  }
  return true;
}

bool FCState::ReadStateChunks(EmuFile *is, int32 totalsize) {
  uint32 size;
  bool ret = true;

  while (totalsize > 0) {
    int t = is->fgetc();
    if (t == EOF) break;
    if (!read32le(&size, is)) break;
    totalsize -= size + 5;

    switch (t) {
      case 1:
        if (!ReadStateChunk(is, sfcpu, size)) ret = false;
        break;
      case 3:
        if (!ReadStateChunk(is, fc->ppu->FCEUPPU_STATEINFO(), size))
          ret = false;
        break;
      case 4:
        if (!ReadStateChunk(is, fc->input->FCEUINPUT_STATEINFO(), size))
          ret = false;
        break;
      case 7:
        fprintf(stderr, "This used to be mid-movie recording. -tom7.\n");
        abort();
        break;
      case 0x10:
        if (!ReadStateChunk(is, sfmdata, size)) ret = false;
        break;
      case 5:
        if (!ReadStateChunk(is, fc->sound->FCEUSND_STATEINFO(), size))
          ret = false;
        break;
      case 6: is->fseek(size, SEEK_CUR); break;
      case 8:
        // load back buffer
        {
          if (is->fread((char *)fc->fceu->XBackBuf, size) != size) ret = false;
        }
        break;
      case 2:
        if (!ReadStateChunk(is, sfcpuc, size)) ret = false;
        break;
      default:
        // for somebody's sanity's sake, at least warn about it:
        // XXX should probably just abort here since we don't try to provide
        // save-state compatibility. -tom7
        fprintf(stderr, "Invalid save state with unknown chunk type %d.\n"
                "Note that fceulib does not provide save-state "
                "compatibility across versions.\n", t);
        abort();
        // is->fseek(size, SEEK_CUR);
        break;
    }
  }

  return ret;
}

// Simplified save that does not compress.
bool FCState::SaveRAW(std::vector<uint8> *out) const {
  EmuFile_MEMORY os(out);

  uint32 totalsize = 0;

  fc->ppu->SaveState();
  fc->sound->SaveState();
  totalsize = WriteStateChunk(&os, 1, sfcpu);
  totalsize += WriteStateChunk(&os, 2, sfcpuc);
  //  TRACE("PPU:");
  totalsize += WriteStateChunk(&os, 3, fc->ppu->FCEUPPU_STATEINFO());
  // TRACEV(*out);
  totalsize += WriteStateChunk(&os, 4, fc->input->FCEUINPUT_STATEINFO());
  totalsize += WriteStateChunk(&os, 5, fc->sound->FCEUSND_STATEINFO());

  if (SPreSave) SPreSave(fc);
  // This allows other parts of the system to hook into things to be
  // saved. It is indeed used for "WRAM", "LATC", "BUSC". -tom7
  // M probably stands for Mapper, but I also use it in input, at least.
  //
  // TRACE("SFMDATA:");
  totalsize += WriteStateChunk(&os, 0x10, sfmdata);
  // TRACEV(*out);
  // Was just spre, but that seems wrong -tom7
  if (SPreSave && SPostSave) SPostSave(fc);

  // save the length of the file
  const uint32 len = os.size();

  // PERF shrink to fit?

  // sanity check: len and totalsize should be the same
  if (len != totalsize) {
    FCEUD_PrintError("sanity violation: len != totalsize");
    return false;
  }

  return true;
}

bool FCState::LoadRAW(const std::vector<uint8> &in) {
  EmuFile_MEMORY_READONLY is{in};

  int totalsize = is.size();
  // Assume current version; memory only.
  int stateversion = FCEU_VERSION_NUMERIC;

  bool success = (ReadStateChunks(&is, totalsize) != 0);

  if (fc->fceu->GameStateRestore != nullptr) {
    fc->fceu->GameStateRestore(fc, stateversion);
  }

  if (success) {
    fc->ppu->LoadState(stateversion);
    fc->sound->LoadState(stateversion);
    return true;
  } else {
    return false;
  }
}

void FCState::ResetExState(void (*PreSave)(FC *), void (*PostSave)(FC *)) {

  // If this needs to happen, it's a bug in the way the savestate
  // system is being used. Fix it! It's not even really possible to
  // do this hack in the reimplementation.
  #if 0
  // adelikat, 3/14/09: had to add this to clear out the size
  // parameter. NROM(mapper 0) games were having savestate
  // crashes if loaded after a non NROM game because the size
  // variable was carrying over and causing savestates to save
  // too much data
  SFMDATA[0].s = 0;
  #endif

  SPreSave = PreSave;
  SPostSave = PostSave;
  sfmdata.clear();
}

void FCState::AddExVec(const vector<SFORMAT> &vec) {
  for (const SFORMAT &sf : vec) {
    int flags = sf.s & FCEUSTATE_FLAGS;
    AddExStateReal(sf.v, sf.s & ~FCEUSTATE_FLAGS, flags, sf.desc,
                   "via AddExVec");
  }
}

void FCState::AddExStateReal(void *v, uint32 s, int type, SKEY desc,
                             const char *src) {
  // PERF: n^2. Use a map/set.
  for (const SFORMAT &sf : sfmdata) {
    if (sf.desc == desc) {
      fprintf(stderr,
              "SFORMAT with duplicate key: %c%c%c%c\n"
              "Second called from %s\n",
              desc[0], desc[1], desc[2], desc[3], src);
      abort();
    }
  }

  assert(s != ~(uint32)0);

  SFORMAT sf{v, s, desc};
  if (type) sf.s |= FCEUSTATE_RLSB;
  sfmdata.push_back(sf);
}

FCState::FCState(FC *fc) :
  fc(fc),
  sfcpu({
      {&fc->X->reg_PC, 2 | FCEUSTATE_RLSB, "rgPC"},
      {&fc->X->reg_A, 1, "regA"},
      {&fc->X->reg_P, 1, "regP"},
      {&fc->X->reg_X, 1, "regX"},
      {&fc->X->reg_Y, 1, "regY"},
      {&fc->X->reg_S, 1, "regS"},
      {fc->fceu->RAM, 0x800, "RAMM"},
    }),
  sfcpuc({
      {&fc->X->jammed, 1, "JAMM"},
      {&fc->X->IRQlow, 4 | FCEUSTATE_RLSB, "IQLB"},
      {&fc->X->tcount, 4 | FCEUSTATE_RLSB, "ICoa"},
      {&fc->X->count, 4 | FCEUSTATE_RLSB, "ICou"},
      {&fc->fceu->timestampbase,
       sizeof(fc->fceu->timestampbase) | FCEUSTATE_RLSB, "TSBS"},
      // alternative to the "quick and dirty hack"
      {&fc->X->reg_PI, 1, "MooP"},
      // This was not included in FCEUltra, but I can't see any
      // reason why it shouldn't be (it's updated with each memory
      // read and used by some boards), and execution diverges if
      // it's not saved/restored. (See "Skull & Crossbones" around
      // FCEUlib revision 2379.)
      {&fc->X->DB, 1, "DBDB"},
    })
{
  /* empty */
}
