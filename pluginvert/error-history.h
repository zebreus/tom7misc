#ifndef _PLUGINVERT_ERROR_HISTORY_H
#define _PLUGINVERT_ERROR_HISTORY_H

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <map>

#include "image.h"

// Stores error history (train, test) over time. Multiple models are
// supported. Since we typically train a model over O(100k) rounds, we
// just keep all of the error history in memory.
//
// Not thread-safe.
struct ErrorHistory {
  // Standardized columns, but use of these is optional.
  static constexpr int ERROR_TRAIN = 0;
  static constexpr int ERROR_TEST = 1;

  explicit ErrorHistory(const std::string &filename,
                        int num_models = 1);

  // OK to add rounds sparsely, or even out-of-order.
  // Arbitrary (non-negative) column index can record different series
  //   (e.g. 0 = train error, 1 = eval error; or a model with just two
  //    outputs might graph error for both rather than combining).
  // 0 <= model_idx < num_models.
  void Add(int64_t round_number,
           double error_per_example,
           int column_idx = 0,
           int model_idx = 0);

  void Save();

  // Merge all the models into a single TSV, sub-sampling the data
  // if max_points is given. Only the selected column is output.
  void WriteMergedTSV(const std::string &outfile,
                      int column = 0,
                      std::optional<int> max_points = {}) const;

  ImageRGBA MakeImage(int width, int height,
                      std::map<int, uint32_t> column_colors,
                      int model_idx) const;

private:
  void Load();

  const std::string filename;
  const int num_models = 0;

  struct Record {
    int64_t round_number = 0;
    double error_per_example = 0.0;
    int column_index = 0;
    int model_index = 0;
  };

  std::vector<Record> records;
};

#endif
