
#ifndef _SOS_DATABASE_H
#define _SOS_DATABASE_H

#include <array>
#include <string>
#include <cstdint>

#include "interval-cover.h"

struct Database {
  using Square = std::array<uint64_t, 9>;
  Database() : done(false) {}

  // Mark an epoch as completed.
  void AddEpoch(uint64_t start, uint64_t size);
  void AddAlmost2(const Square &square);

  static Database FromInterestingFile(const std::string &filename);

  // returns start, size
  std::pair<uint64_t, uint64_t> NextToDo(uint64_t max_size) const;
  std::pair<uint64_t, uint64_t> NextGapAfter(uint64_t start,
                                             uint64_t max_size) const;

  std::string Epochs() const;

  std::vector<std::pair<uint64_t, Square>> LastN(int n) const;

  // True if we have exhaustively searched the inner sum range
  // [a, b).
  bool CompleteBetween(uint64_t a, uint64_t b) const;
  bool IsComplete(uint64_t a) const;

  static int64_t GetHerr(const Square &square);

private:
  // Set to true if done.
  IntervalCover<bool> done;
  // Key is the inner sum.
  std::map<uint64_t, Square> almost2;
};

#endif
