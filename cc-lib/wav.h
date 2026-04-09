
#ifndef _CC_LIB_WAV_H
#define _CC_LIB_WAV_H

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

struct WAV {

  struct Audio {
    int samples_per_sec = 0;

    int num_channels = 0;

    // Channels always interleaved, so this will have
    // frames * num_channels samples.
    // Audio nominally in [-1, +1].
    std::vector<float> samples;

    size_t NumFrames() const;
    double SecondsLong() const;
  };

  static std::optional<Audio> LoadFromFile(std::string_view filename);
  // Parse in-memory wave file.
  static std::optional<Audio> Parse(std::span<const uint8_t> bytes);
};

#endif
