#include "wav.h"

#include <optional>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestParseTrivial() {
  constexpr uint8_t bytes[] = {
    'R', 'I', 'F', 'F',
    // Chunk size (44 bytes)
    0x2C, 0x00, 0x00, 0x00,
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    // Subchunk1Size (16 bytes)
    0x10, 0x00, 0x00, 0x00,
    // AudioFormat (PCM)
    0x01, 0x00,
    // NumChannels (2)
    0x02, 0x00,
    // SampleRate (44100)
    0x44, 0xAC, 0x00, 0x00,
    // ByteRate (176400)
    0x10, 0xB1, 0x02, 0x00,
    // BlockAlign (4)
    0x04, 0x00,
    // BitsPerSample (16)
    0x10, 0x00,
    'd', 'a', 't', 'a',
    // Subchunk2Size (8 bytes)
    0x08, 0x00, 0x00, 0x00,
    // Frame 0: 0, 32767
    0x00, 0x00, 0xFF, 0x7F,
    // Frame 1: -32768, 0
    0x00, 0x80, 0x00, 0x00,
  };

  std::optional<WAV::Audio> owav = WAV::Parse(bytes);
  CHECK(owav.has_value());
  const WAV::Audio &wav = owav.value();

  CHECK(wav.num_channels == 2);
  CHECK(wav.samples_per_sec == 44100);
  CHECK(wav.NumFrames() == 2);
  CHECK(wav.samples.size() == 4);

  CHECK(wav.samples[0] == 0.0f);
  // (This fraction can be represented exactly with float.)
  CHECK(wav.samples[1] == 32767.0f / 32768.0f);
  CHECK(wav.samples[2] == -1.0f);
  CHECK(wav.samples[3] == 0.0f);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestParseTrivial();

  Print("OK\n");
  return 0;
}
