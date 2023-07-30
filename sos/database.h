
#ifndef _SOS_DATABASE_H
#define _SOS_DATABASE_H

#include <array>
#include <string>
#include <cstdint>
#include <functional>

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
  std::optional<std::pair<uint64_t, uint64_t>>
  NextGapBefore(uint64_t start,
                uint64_t max_size) const;

  std::string Epochs() const;

  std::vector<std::pair<uint64_t, Square>> LastN(int n) const;

  // True if we have exhaustively searched the inner sum range
  // [a, b).
  bool CompleteBetween(uint64_t a, uint64_t b) const;
  bool IsComplete(uint64_t a) const;

  // Note that these two include vectors with the wrong slope
  // (wrap around).
  // For herror.
  // f(x0, y0, x1, y1, iceptx);
  void ForEveryHVec(uint64_t pt,
                    const std::function<void(int64_t, int64_t,
                                             int64_t, int64_t,
                                             int64_t)> &f);
  // And aerror.
  void ForEveryAVec(uint64_t pt,
                    const std::function<void(int64_t, int64_t,
                                             int64_t, int64_t,
                                             int64_t)> &f);


  // For both aerror and herror.
  // f_a or f_h(x0, y0, x1, y1, iceptx), but only when this is a zero
  // crossing with the correct sign.
  void ForEveryZeroVec(
      const std::function<void(int64_t, int64_t,
                               int64_t, int64_t,
                               int64_t)> &f_a,
      const std::function<void(int64_t, int64_t,
                               int64_t, int64_t,
                               int64_t)> &f_h) const;

  enum class IslandZero {
    NONE,
    HAS_ZERO,
    NO_POINTS,
    GO_LEFT,
    GO_RIGHT,
  };

  // Intended for sparse "islands"; not that useful for dense
  // regions.
  // For the span containing pt,
  //  return NONE if we haven't done anything in that span yet,
  //  return HAS_ZERO if the island is complete (has a result
  //    above and below the axis)
  //  return NO_POINTS if we have tried the span but don't have
  //    enough information to predict the direction; can go
  //    left or right to make progress
  //  return GO_LEFT if the zero will be to the left (smaller pt)
  //  return GO_RIGHT if the zero will be to the right (larger pt)
  IslandZero IslandHZero(int64_t pt);
  IslandZero IslandAZero(int64_t pt);

  // For all the places where we have a pair of aligned (mod the
  // appropriate radix) points straddling the x-axis within a known
  // region, get the (linear) intercept between them.
  // Returns {azeroes, hzeroes}.
  std::pair<std::vector<int64_t>,
            std::vector<int64_t>> GetZeroes();

  static int64_t GetHerr(const Square &square);
  static int64_t GetAerr(const Square &square);

  // Key is the inner sum.
  const std::map<uint64_t, Square> Almost2() const { return almost2; }
  const IntervalCover<bool> Done() const { return done; }

  // void ForEveryIntercept(std::function<void> );

private:
  // Set to true if done.
  IntervalCover<bool> done;
  // Key is the inner sum.
  std::map<uint64_t, Square> almost2;
};

#endif
