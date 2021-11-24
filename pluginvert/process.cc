
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

static constexpr int WINDOW_SIZE = 1024;
using Plugin = SimpleHighpass<WINDOW_SIZE>;

int main(int argc, char **argv) {
  CHECK(argc == 2) << "./process.exe input.mp3";

  std::vector<float> raw = ReadMp3Mono(argv[1]);
  CHECK(!raw.empty());
  printf("%s: %d samples\n", argv[1], (int)raw.size());

  const std::array<float, Plugin::NUM_PARAMETERS> params = {
    100.0f,
    1.5f,
  };

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
  WaveSave::SaveMono("processed.wav", processed, 44100);

  printf("OK wrote raw.wav and processed.wav\n");
  return 0;
}
