
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "base/print.h"

using namespace std;

// Dead simple program that extracts the PRG ROM from .NES files
// written here. Assumes a lot about the size/format of the .NES
// files, so it's not going to work in the general case...

int main(int argc, char **argv) {
  std::vector<string> args;

  bool got_type = false;
  // If supplied, mirrors this many copies of the ROM to the
  // output file.
  int mirror = 1;
  bool dump_prg = false;
  for (int i = 1; i < argc; i++) {
    string arg = argv[i];
    if (arg == "-prg") {
      if (got_type) {
        Print(stderr, "-prg or -chr only once.\n");
        return -1;
      }
      dump_prg = true;
      got_type = true;
    } else if (arg == "-chr") {
      if (got_type) {
        Print(stderr, "-prg or -chr only once.\n");
        return -1;
      }

      dump_prg = false;
      got_type = true;
    } else if (arg == "-mirror") {
      if (i == argc - 1) {
        Print(stderr, "Need argument to -mirror.\n");
        return -1;
      }
      mirror = atoi(argv[i + 1]);
      if (mirror == 0) {
        Print(stderr, "Need numeric argument to -mirror; got: {}\n",
              argv[i + 1]);
        return -1;
      }
      i++;

    } else if (arg[0] == '-') {
      Print(stderr, "Unknown flag {}\n", arg);
      return -1;
    } else {
      args.push_back(arg);
    }
  }

  if (!got_type || args.size() != 2) {
    Print(stderr,
          "usage: makeimage.exe [-mirror n] -prg|-chr "
          "cart.nes cart.rom\n");
    return -1;
  }

  const string infile = args[0];
  const string outfile = args[1];

  FILE *inf = fopen(infile.c_str(), "rb");
  if (inf == 0) {
    Print(stderr, "Can't read {}\n", infile);
    return -1;
  }

  FILE *outf = fopen(outfile.c_str(), "wb");
  if (outf == 0) {
    fclose(inf);
    Print(stderr, "Can't open {} for writing\n", outfile);
    return -1;
  }

  // Read the header.
  uint8_t header[16];
  if (16 != fread(&header, 1, 16, inf)) {
    Print(stderr, "Can't read header\n");
    return -1;
  }

  if (0 != memcmp("NES\x1a", header, 4)) {
    Print(stderr, "Not a NES file\n");
    return -1;
  }

  int prg_bytes = header[4] * 16384;
  int chr_bytes = header[5] * 8192;
  Print(stderr,
        "{} prg banks ({} bytes) x {} mirrors = {}\n"
        "{} chr banks ({} bytes)\n",
        header[4], prg_bytes, mirror, mirror * prg_bytes,
        header[5], chr_bytes);
  uint8_t *prg = (uint8_t *)malloc(prg_bytes);
  if (prg_bytes != fread(prg, 1, prg_bytes, inf)) {
    Print(stderr, "Couldn't read {} PRG bytes?\n", prg_bytes);
    return -1;
  }

  uint8_t *chr = (uint8_t *)malloc(chr_bytes);
  if (chr_bytes != fread(chr, 1, chr_bytes, inf)) {
    Print(stderr, "Couldn't read {} CHR bytes?\n", chr_bytes);
    return -1;
  }

  if (dump_prg) {
    for (int i = 0; i < mirror; i++) {
      if (prg_bytes != fwrite(prg, 1, prg_bytes, outf)) {
        Print(stderr, "Couldn't write {} rom bytes?\n", prg_bytes);
        return -1;
      }
    }
    Print(stderr, "Successfully wrote {} PRG Bytes to {}.\n",
          mirror * prg_bytes, outfile);
  } else {
    for (int i = 0; i < mirror; i++) {
      if (chr_bytes != fwrite(chr, 1, chr_bytes, outf)) {
        Print(stderr, "Couldn't write {} rom bytes?\n", chr_bytes);
        return -1;
      }
    }
    Print(stderr, "Successfully wrote {} CHR Bytes to {}.\n",
          mirror * chr_bytes, outfile);
  }

  free(chr);
  free(prg);
  fclose(inf);
  fclose(outf);
  return 0;
}
