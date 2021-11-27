
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <cmath>

#include "mp3.h"
#include "plugins.h"
#include "util.h"
#include "wavesave.h"

#include "base/logging.h"

using namespace std;

static std::vector<float> ReadMp3Mono(const string &filename) {
  std::unique_ptr<MP3> mp3 = MP3::Load(filename);
  CHECK(mp3.get() != nullptr) << filename;

  if (mp3->channels == 1) return std::move(mp3->samples);
  CHECK(mp3->channels > 1);

  // Otherwise, de-interleave samples, taking the first channel.
  const int OUT_SIZE = mp3->samples.size() / mp3->channels;
  std::vector<float> out;
  out.reserve(OUT_SIZE);
  for (int i = 0; i < OUT_SIZE; i++) {
    out.push_back(mp3->samples[i * mp3->channels]);
  }
  return out;
}

// static constexpr int WINDOW_SIZE = 1024;
static constexpr int WINDOW_SIZE = 44100;
using Plugin = Convolve4<WINDOW_SIZE>;

int main(int argc, char **argv) {
  CHECK(argc == 2 + Plugin::NUM_PARAMETERS) <<
    "./process.exe input.mp3 param1 param2 ...\n" <<
    Plugin::NUM_PARAMETERS << " params for current plugin.";

  std::vector<float> raw = ReadMp3Mono(argv[1]);
  CHECK(!raw.empty());
  printf("%s: %d samples\n", argv[1], (int)raw.size());

  std::array<float, Plugin::NUM_PARAMETERS> params;
  for (int i = 0; i < Plugin::NUM_PARAMETERS; i++) {
    params[i] = Util::ParseDouble(argv[2 + i], 0.0);
    if (params[i] < Plugin::PARAMS[i].lb ||
        params[i] > Plugin::PARAMS[i].ub) {
      printf("Warning: Param %d out of bounds [%.2f, %.2f]: %.2f\n",
             i, Plugin::PARAMS[i].lb, Plugin::PARAMS[i].ub, params[i]);
    }
  }

  // TODO: Windowed. This also drops the last frame if not complete.
  vector<float> processed;
  processed.reserve(raw.size());
  for (int i = 0; i < raw.size() - WINDOW_SIZE; i += WINDOW_SIZE) {
    vector<float> iw(WINDOW_SIZE, 0.0f);
    for (int s = 0; s < WINDOW_SIZE; s++)
      iw[s] = raw[i + s];

    vector<float> ow = Plugin::Process(iw, params);
    for (float f : ow) processed.push_back(f);
  }

  WaveSave::SaveMono("raw.wav", raw, 44100);
  WaveSave::HardClipMono(&processed);
  WaveSave::SaveMono("processed.wav", processed, 44100);

  printf("OK wrote raw.wav and processed.wav\n");
  return 0;
}
