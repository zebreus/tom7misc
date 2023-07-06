#include <vector>
#include <string>
#include <set>
#include <memory>

#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "emulator.h"
#include "simplefm2.h"
#include "simplefm7.h"
#include "stringprintf.h"
#include "../cc-lib/wavesave.h"

#include "../cc-lib/image.h"

using namespace std;

#if DISABLE_SOUND
#error "If sound is not compiled in, can't write wave!"
#endif

// Frames to skip at the beginning
static constexpr int SKIP = 30 + 145;
// Slow-mo: Renders this many frames per frame.
static constexpr int FRAMES_PER_FRAME = 516;
static constexpr int MAX_FRAMES = 8.6 * 10;

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: fm2wav romfile.nes moviefile.fm7\n"
      "\nWrites to moviefile.wav and moviefile*.png\n");
    return -1;
  }
  const string romfilename = argv[1];
  const string moviefilename = argv[2];

  size_t dot = moviefilename.rfind(".");
  if (dot == string::npos) {
    fprintf(stderr, "Moviefilename [%s] should end with .fm7.\n",
      moviefilename.c_str());
    return -1;
  }
  const string wavefilename = moviefilename.substr(0, dot) + (string)".wav";
  const string pngbase = moviefilename.substr(0, dot);

  std::unique_ptr<Emulator> emu{Emulator::Create(romfilename)};
  vector<pair<uint8, uint8>> movie =
    SimpleFM7::ReadInputs2P(moviefilename);
  if ((int)movie.size() > MAX_FRAMES + SKIP)
    movie.resize(MAX_FRAMES + SKIP);

  vector<uint16> samples;


  for (int f = 0; f < (int)movie.size() && f < SKIP; f++) {
    const auto &[a, b] = movie[f];
    emu->StepFull(a, b);
  }

  int frame_num = 0;
  for (int f = SKIP; f < (int)movie.size(); f++) {
    const pair<uint8, uint8> input = movie[f];
    emu->StepFull(input.first, input.second);

    // Sound.
    vector<int16> sound;
    emu->GetSound(&sound);

    #define SKIDDER 1
    #if SKIDDER
    for (int16 s : sound) samples.push_back(s);
    // here frames
    for (int i = 0; i < (FRAMES_PER_FRAME - 1) * (int)sound.size(); i++) {
      samples.push_back(0);
    }
    #else
    for (int16 s : sound)
      for (int i = 0; i < FRAMES_PER_FRAME; i++)
        samples.push_back(s);
    #endif

    vector<uint8> rgba = emu->GetImage();
    ImageRGBA img(rgba, 256, 256);
    img.Save(FCEU_StringPrintf("%s_%05d.png", pngbase.c_str(), frame_num));
    frame_num++;
  }

  if (!WaveSave::SaveMono16(wavefilename, samples, Emulator::AUDIO_SAMPLE_RATE)) {
    fprintf(stderr, "Couldn't write to %s...\n", wavefilename.c_str());
    return -1;
  }

  fprintf(stderr, "Wrote %d frames of sound to %s.\n",
    (int)movie.size(), wavefilename.c_str());
  return 0;
}
