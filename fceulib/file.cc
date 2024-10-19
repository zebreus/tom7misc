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

#include <cstdint>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>

#include "emufile.h"
#include "fceu.h"
#include "file.h"
#include "state.h"
#include "types.h"

using namespace std;

FceuFile::FceuFile() {}

FceuFile::~FceuFile() {
  Close();
}

uint64_t FceuFile::FTell() {
  return stream->ftell();
}

// Opens a file to be read a byte at a time.
static EmuFile_FILE *FCEUD_UTF8_fstream(const char *fn, const char *m) {
  return new EmuFile_FILE(fn, m);
}

FceuFile *FceuFile::FOpen(const std::string &path, const char *mode) {
  // XXX simplify away; see below
  bool read = (string)mode == "rb";
  bool write = (string)mode == "wb";
  if (!read && !write) {
    FCEU_PrintError("invalid file open mode specified "
                    "(only wb and rb are supported)");
    return 0;
  }

  // This is weird -- the only supported mode is read, and write always fails?
  // (Probably because of archives? But that means we can probably just get
  // rid of mode at all call sites. But FDS tries "wb"...)
  // -tom7
  if (!read) {
    return nullptr;
  }

  // if the archive contained no files, try to open it the old fashioned way
  EmuFile_FILE* fp = FCEUD_UTF8_fstream(path.c_str(), mode);
  if (!fp || fp->get_fp() == nullptr) {
    return 0;
  }

  // Here we used to try zip files. -tom7

  // open a plain old file
  FceuFile *fceufp = new FceuFile();
  fceufp->filename = path;
  fceufp->stream = fp;
  fceufp->FSeek(0, SEEK_END);
  fceufp->size = fceufp->FTell();
  fceufp->FSeek(0, SEEK_SET);

  return fceufp;
}

void FceuFile::Close() {
  if (stream != nullptr) {
    delete stream;
  }
  stream = nullptr;
}

uint64 FceuFile::FRead(void *ptr, size_t size, size_t nmemb) {
  return stream->fread((char *)ptr, size * nmemb);
}

uint64_t FceuFile::FSeek(long offset, int whence) {
  stream->fseek(offset, whence);
  return FTell();
}

bool FceuFile::Read32LE(uint32 *val) {
  return stream->read32le(val);
}

int FceuFile::FGetc() {
  return stream->fgetc();
}

uint64_t FceuFile::Size() const {
  return size;
}

// TODO(tom7): We should probably not let this thing save battery backup
// files; they should be serialized as part of the state at most.
//
// These are currently producing dummy names like ".sav" always. Don't
// let fceulib save!
string FCEU_MakeSaveFilename() {
  return ".sav";
}

string FCEU_MakeFDSFilename() {
  return ".fds";
}

string FCEU_MakeFDSROMFilename() {
  return "disksys.rom";
}
