
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

static void Lowpass(std::vector<float> *samples) {
  int window_size = 48;
  std::vector<float> new_samples;
  new_samples.reserve(samples->size());
  for (int i = 0; i < (int)samples->size(); i++) {
    double sum = 0.0;
    for (int j = 0; j < window_size; j++) {
      if (i - j >= 0) {
        sum += abs((*samples)[i - j]);
      }
    }
    sum /= window_size;
    new_samples.push_back(sum);
  }
  *samples = std::move(new_samples);
}


static void Generate(const MP3 &mp3) {
  // Discretize.
  // Use 1/400th of a second (at 48khz).
  const int bucket_size = 240;
  const float threshold = 9.5f;

  std::vector<float> lowpass_samples = mp3.samples;
  Lowpass(&lowpass_samples);

  // Consider the coin flip "heads" if any sample in the
  // range exceeds the threshold.
  std::vector<bool> buckets;
  buckets.reserve((lowpass_samples.size() / bucket_size) + 1);
  int num_ones = 0;
  for (int bucket_start = 0;
       bucket_start < (int)lowpass_samples.size() - bucket_size;
       bucket_start += bucket_size) {
    bool exceeds = false;
    for (int j = 0; j < bucket_size; j++) {
      if (fabs(lowpass_samples[bucket_start + j] * 100.0f) > threshold) {
        exceeds = true;
      }
    }
    if (exceeds) num_ones++;
    buckets.push_back(exceeds);
  }

  double biased_p = num_ones / (double)buckets.size();
  printf("%d buckets. p=%.3f%%\n", (int)buckets.size(), 100.0 * biased_p);

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

  // Now use it as a series of independent coin tosses with
  // probability p. We want to generate two 3-digit numbers.
  // We'll generate 10 uniform bits and reject if >999, which
  // will be rare.

  int b = 0;
  auto NextBiased = [&b, &buckets]() {
      CHECK(b < buckets.size()) << "Out of samples. :(";
      return buckets[b++];
    };

  for (int triples = 5; triples--;) {
    int triple = 0;
    for (int i = 0; i < 10; i++) {
      float target_p = 0.5f;
      for (;;) {
        bool x = NextBiased();
        if (target_p >= biased_p) {
          if (x) {
            // Accept with heads.
            triple <<= 1;
            triple |= 1;
            goto next_bit;
          } else {
            // Use remaining probability mass on the next
            // attempt.
            target_p = (target_p - biased_p) / (1.0 - biased_p);
          }

        } else {
          // target_p < biased_p
          if (!x) {
            // Accept with tails.
            triple <<= 1;
            goto next_bit;
          } else {
            target_p = target_p / biased_p;
          }

        }
      }

      next_bit:;
    }

    printf("Resulting number: %d\n", triple);
  }

}


int main(int argc, char **argv) {
  ANSI::Init();

  std::unique_ptr<MP3> mp3 = MP3::Load("banana.mp3");
  printf("%d samples.\n", (int)mp3->samples.size());
  Histo(mp3->samples);

  Generate(*mp3);

  return 0;
}
