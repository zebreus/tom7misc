
#include <vector>
#include <string>
#include <string_view>
#include <memory>

#include "wavesave.h"
#include "util.h"
#include "base/logging.h"
#include "base/stringprintf.h"

using namespace std;

static constexpr int SECONDS = 4 * 60;
static constexpr int SAMPLES_PER_SEC = 48000;

int main(int argc, char **argv) {

  std::vector<float> zero;
  for (int i = 0; i < SECONDS * SAMPLES_PER_SEC; i++)
    zero.push_back(0.0f);

  // Mono, saved as 16-bit.
  CHECK(WaveSave::SaveMono("silence.wav",
                           zero,
                           SAMPLES_PER_SEC));

  printf("Wrote silence.wav\n");
  return 0;
}
