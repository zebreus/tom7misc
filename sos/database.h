
#ifndef _SOS_DATABASE_H
#define _SOS_DATABASE_H

#include <array>
#include <string>
#include <cstdint>

#include "interval-cover.h"

struct Database {
  Database() : done(false) {}

  // Mark an epoch as completed.
  void AddEpoch(uint64_t start, uint64_t size);
  void AddAlmost2(const std::array<uint64_t, 9> &square);

  static Database FromInterestingFile(const std::string &filename);

  std::string Epochs() const;

private:
  // Set to true if done.
  IntervalCover<bool> done;
};

#endif
