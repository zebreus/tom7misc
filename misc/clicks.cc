
#include "mp3.h"

#include <vector>
#include <memory>

#include "mp3.h"
#include "ansi.h"
#include "auto-histo.h"
#include "image.h"

static void Histo(const std::vector<float> &samples) {
  AutoHisto histo(1000000);
  for (float s : samples) histo.Observe(s * 100.0f);
  printf("Histo:\n%s\n",
         histo.SimpleANSI(40).c_str());
}


static void Generate(const MP3 &mp3) {
  // Discretize.
  // Use 1/400th of a second (at 48khz).
  const int bucket_size = 240;
  const float threshold = 9.0f;

  // Consider the coin flip "heads" if any sample in the
  // range exceeds the threshold.
  std::vector<bool> buckets;
  buckets.reserve((mp3.samples.size() / bucket_size) + 1);
  int num_ones = 0;
  for (int bucket_start = 0;
       bucket_start < (int)mp3.samples.size() - bucket_size;
       bucket_start += bucket_size) {
    bool exceeds = false;
    for (int j = 0; j < bucket_size; j++) {
      if (fabs(mp3.samples[bucket_start + j] * 100.0f) > threshold) {
        exceeds = true;
      }
    }
    if (exceeds) num_ones++;
    buckets.push_back(exceeds);
  }
  printf("%d buckets. p=%.3f%%\n", (int)buckets.size(),
         (100.0 * num_ones) / buckets.size());

  // Make image.
  const int ppb = 10;
  ImageRGBA waveform(ppb * buckets.size(), 256);
  const int WAVE_OFFSET = 100;
  float HALF_WAVE_HEIGHT = 100.0;
  waveform.Clear32(0x000000FF);

  float px = 0.0, py = WAVE_OFFSET;
  const size_t used_samples =
    mp3.samples.size() - (mp3.samples.size() % bucket_size);

  for (int idx = 0; idx < used_samples; idx++) {
    float sample = std::clamp(mp3.samples[idx] * 3.0f, -1.0f, 1.0f);
    float nx = (idx / (double)used_samples) * (double)waveform.Width();
    float ny = sample * HALF_WAVE_HEIGHT + WAVE_OFFSET;
    waveform.BlendLineAA32(px, py, nx, ny, 0x00FF0077);
    px = nx;
    py = ny;
  }

  for (int b = 0; b < (int)buckets.size(); b++) {
    uint32_t c = ((b & 1) ? 0x111111FF : 0x000000FF) |
      (buckets[b] ? 0x000099FF : 0x000000FF);
    const int ystart = WAVE_OFFSET * 2 + 1;
    waveform.BlendRect32(b * ppb, ystart,
                         ppb, waveform.Height() - ystart, c);
  }

  waveform.Save("waveform.png");

}


int main(int argc, char **argv) {
  ANSI::Init();

  std::unique_ptr<MP3> mp3 = MP3::Load("banana.mp3");
  printf("%d samples.\n", (int)mp3->samples.size());
  Histo(mp3->samples);

  Generate(*mp3);

  return 0;
}
