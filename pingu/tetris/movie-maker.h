
#ifndef _PINGU_TETRIS_MOVIE_MAKER_H
#define _PINGU_TETRIS_MOVIE_MAKER_H

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"

#include "encoding.h"

struct MovieMaker {

  // Callbacks 
  struct Callbacks {
    // num pieces in schedule
    std::function<void(const Emulator &, int)> game_start;

    // number of retries, desired shape
    std::function<void(const Emulator &, int, Piece)> retried;

    // pieces done, pieces total
    std::function<void(const Emulator &, int, int)> placed_piece;

    // XXX not implemented
    // bytes done
    std::function<void(const Emulator &, int)> finished_byte;
  };
  
  MovieMaker(const std::string &solution_file,
             const std::string &rom_file,
             int64_t seed);

  // Find moves in the emulator (and execute them)
  // that encode the bytes on the playfield. Calls callbacks
  // in the object periodically. Returns the accumulated
  // moves.
  std::vector<uint8_t> Play(const std::vector<uint8> &bytes,
                            Callbacks callbacks);

  // Emulator owned by MovieMaker.
  // Use this after Play has returned, but if you mess with it
  // while or before Play runs, who knows what will happen?
  Emulator *GetEmu() { return emu.get(); };

  // Just for benchmarking, etc.; the number of total emulator
  // steps we executed while finding the solution.
  int64 StepsExecuted() const { return steps_executed; }
  
private:
  ArcFour rc;
  std::unique_ptr<Emulator> emu;
  std::map<uint8_t, std::vector<Move>> all_sols;
  int64 steps_executed = 0;
};

#endif
