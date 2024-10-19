#ifndef _FCEULIB_FILE_H_
#define _FCEULIB_FILE_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "emufile.h"

struct FceuFile {
  FceuFile();
  // Automatically closes file.
  ~FceuFile();

  // Mode is "rb" or "wb".
  static FceuFile *FOpen(const std::string &path, const char *mode);

  void Close();
  uint64_t Size() const;
  int FGetc();
  bool Read32LE(uint32_t *val);
  uint64_t FSeek(long offset, int whence);
  uint64_t FRead(void *ptr, size_t size, size_t nmemb);

  enum {
    READ, WRITE, READWRITE
  } mode;

  // the stream you can use to access the data
  EmuFile *stream = nullptr;

 private:
  uint64_t FTell();

  // the name of the file, or the logical name of the file within the archive
  std::string filename;

  // the size of the file
  int size = 0;
};

// XXX This whole thing can probably be deleted. -tom7
// Broke MakeFName into separate functions where it remained. -tom7
std::string FCEU_MakeSaveFilename();
std::string FCEU_MakeFDSFilename();
std::string FCEU_MakeFDSROMFilename();

#endif
