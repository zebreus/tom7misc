
#ifndef _PLUGINVERT_MODELINFO_H
#define _PLUGINVERT_MODELINFO_H

#include <optional>
#include <cstdint>
#include <map>

#include "image.h"
#include "network.h"

struct ModelInfo {
  // Generate an image with histograms of biases and weights for each
  // layer. Used in training UI or to try to diagnose stuff like runaway
  // gradients.
  //
  // If bounds are present, values are clipped. This can be helpful for
  // inspecting the many weights that are very close to zero.
  static ImageRGBA Histogram(
      const Network &net, int width, int height,
      std::optional<float> weight_bound_low = std::nullopt,
      std::optional<float> weight_bound_high = std::nullopt,
      std::optional<float> bias_bound_low = std::nullopt,
      std::optional<float> bias_bound_high = std::nullopt);

  // Generate an image of the individual weights on a single chunk.
  // The weights are plotted in 2D, as though dense, although large
  // empty regions can be elided. The number of indices on the chunk
  // and the previous layer determine the dimensions. layer_idx is an
  // index into net's Layers, and chunk_idx is an index into that
  // layer's chunks.
  static ImageRGBA ChunkWeights(
      const Network &net, int layer_idx, int chunk_idx,
      // In diagnostic mode, draw unreferenced cells, small nonzero
      // weights, and missing (sparse) inputs distinctly; it's easier
      // to see structural issues this way. Otherwise, draw the matrix
      // as though dense, and boost the visibility of small weights
      // (sqrt(sqrt(f))); it's easier to see patterns in weight values
      // this way and it's nicer on the eye.
      bool diagnostic_mode = false);

  // This is mostly just exposed for testing.
  //
  // Keeps track of what intervals are populated. This lets us draw
  // holes in a chunk's weights efficiently. We only try to do this
  // for indices into the previous layer (i.e. the y axis).
  struct Intervals {
    Intervals(int max_gap, int gap_draw_size);

    void Add(int idx);

    // Get the "pixel" position to draw the index, which should have
    // been previously added.
    int GetPos(int idx) const;

    bool IsInInterval(int idx) const;

    // Passing in the full size, returns the start "pixel" position
    // of each gap, and the number of skipped elements.
    std::vector<std::pair<int, int>> GetGaps(int size) const;

    int ImageSize(int size) const;

    int max_gap = 0;
    int gap_draw_size = 0;
    // Non-overlapping. {start, length}.
    std::map<int, int> ivals;
  };
};

#endif
