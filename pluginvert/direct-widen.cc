
#include "direct-word-problem.h"

#include <string>
#include <vector>
#include <cstdio>
#include <memory>
#include <cstdint>

#include "arcfour.h"
#include "randutil.h"
#include "network.h"
#include "base/logging.h"
#include "base/stringprintf.h"

using namespace std;

using int64 = int64_t;

constexpr int64 WORDLIST_SIZE_BEFORE = 1024;
constexpr int64 WORDLIST_SIZE_AFTER = 1025; // 2048;

static_assert(WORDLIST_SIZE == WORDLIST_SIZE_BEFORE);


static void AddWordsToModel() {
  ArcFour rc(StringPrintf("widen.%lld", time(nullptr)));

  std::unique_ptr<Network> net(
      Network::ReadFromFile(MODEL_NAME));

  printf("Read network.\n");

  // See direct-words.cc
  CHECK(net->layers.size() == 8);

  constexpr int INPUT_WORDS = WORDS_BEFORE + WORDS_AFTER;

  // Input layer just needs to note the increase in WORDLIST_SIZE,
  // but otherwise it's a fake layer.
  CHECK(net->layers.front().num_nodes ==
        WORDLIST_SIZE_BEFORE * INPUT_WORDS);
  net->layers.front().num_nodes = WORDLIST_SIZE_AFTER * INPUT_WORDS;
  Chunk &ichunk = net->layers.front().chunks.front();
  CHECK(ichunk.num_nodes == WORDLIST_SIZE_BEFORE * INPUT_WORDS);
  ichunk.num_nodes = WORDLIST_SIZE_AFTER * INPUT_WORDS;
  ichunk.width = ichunk.num_nodes;

  // The next layer is the forward embedding. We're increasing
  // the size of the convolution (and so the indices and weights),
  // but not the number of output features.
  Layer &layer1 = net->layers[1];
  CHECK(layer1.chunks.size() == 1);
  Chunk &chunk1 = layer1.chunks[0];

  chunk1.occurrence_x_stride = WORDLIST_SIZE_AFTER;
  chunk1.pattern_width = WORDLIST_SIZE_AFTER;
  chunk1.src_width = WORDLIST_SIZE_AFTER * INPUT_WORDS;
  chunk1.span_size = WORDLIST_SIZE_AFTER * INPUT_WORDS;
  chunk1.indices_per_node = WORDLIST_SIZE_AFTER;

  // Don't want to recompute the indices manually, but all we're
  // doing is adding (WORDLIST_SIZE_AFTER - WORDLIST_SIZE_BEFORE) consecutive
  // indices right after the existing ones, plus shifting everything
  // over to accommodate the new longer word length.
  {
    const auto [indices, this_num_nodes,
                num_occurrences_across, num_occurrences_down] =
      Network::MakeConvolutionArrayIndices(chunk1.span_start,
                                           chunk1.span_size,
                                           chunk1.num_features,
                                           chunk1.pattern_width,
                                           chunk1.pattern_height,
                                           chunk1.src_width,
                                           chunk1.src_height,
                                           chunk1.occurrence_x_stride,
                                           chunk1.occurrence_y_stride);
    CHECK(this_num_nodes == chunk1.num_nodes);
    CHECK(num_occurrences_across == chunk1.num_occurrences_across);
    CHECK(num_occurrences_down == chunk1.num_occurrences_down);
    chunk1.indices = indices;
  }

  // Uniform from -mag to mag.
  auto RandomFloatUniform = [&rc](float mag) {
      const float width = 2.0f * mag;
      // Uniform in [0,1]
      const double d = (double)Rand32(&rc) / (double)0xFFFFFFFF;
      return (width * d) - mag;
    };


  // Weights get expanded: Each feature gets EXT new weights, randomly
  // assigned.
  constexpr int EXT = WORDLIST_SIZE_AFTER - WORDLIST_SIZE_BEFORE;
  {
    const float mag = 1.0f / sqrtf(chunk1.indices_per_node);
    std::vector<float> new_weights, new_weights_aux;
    new_weights.reserve(WORDLIST_SIZE_AFTER * chunk1.num_features);
    new_weights_aux.reserve(WORDLIST_SIZE_AFTER * chunk1.num_features * 2);
    // Weights are feature-major.
    for (int f = 0; f < chunk1.num_features; f++) {
      // First, existing weights.
      for (int i = 0; i < WORDLIST_SIZE_BEFORE; i++) {
        const int idx = f * WORDLIST_SIZE_BEFORE + i;
        new_weights.push_back(chunk1.weights[idx]);
        new_weights_aux.push_back(chunk1.weights_aux[idx * 2 + 0]);
        new_weights_aux.push_back(chunk1.weights_aux[idx * 2 + 1]);
      }
      // Then random weights.
      for (int i = 0; i < EXT; i++) {
        new_weights.push_back(RandomFloatUniform(mag));
        // Bogus, but not much we could do about it.
        new_weights_aux.push_back(0.0f);
        new_weights_aux.push_back(0.0f);
      }
    }

    chunk1.weights = std::move(new_weights);
    chunk1.weights_aux = std::move(new_weights_aux);
    CHECK(chunk1.weights.size() ==
          chunk1.indices_per_node * chunk1.num_features);
    CHECK(chunk1.weights_aux.size() == chunk1.weights.size() * 2);
  }

  // Biases are the same because the number of features hasn't changed.

  // Output layer. This decodes 256 encoded floats to WORDLIST_SIZE_BEFORE.
  // Here we are adding EXT number of features. The indices don't change.
  constexpr int OUTPUT_WORDS = INPUT_WORDS + 1;

  Layer &olayer = net->layers.back();
  CHECK(olayer.chunks.size() == 1);
  Chunk &ochunk = olayer.chunks.front();

  ochunk.num_features = WORDLIST_SIZE_AFTER;
  ochunk.num_nodes = OUTPUT_WORDS * WORDLIST_SIZE_AFTER;
  ochunk.width = ochunk.num_nodes;

  // EXT new biases at the end, all zero.
  for (int i = 0; i < EXT; i++) {
    ochunk.biases.push_back(0.0f);
    // Two adam moments.
    ochunk.biases_aux.push_back(0.0f);
    ochunk.biases_aux.push_back(0.0f);
  }

  // Weights are feature-major, so we also add all the new ones at
  // the end.
  {
    const float mag = 1.0f / sqrtf(ochunk.indices_per_node);
    for (int i = 0; i < EXT; i++) {
      for (int j = 0; j < ochunk.indices_per_node; j++) {
        ochunk.weights.push_back(RandomFloatUniform(mag));
        // Two Adam moments per weight. These are bogus but there's
        // not much we could do here except reset to round 0?
        ochunk.weights_aux.push_back(0.0f);
        ochunk.weights_aux.push_back(0.0f);
      }
    }
  }

  olayer.num_nodes = ochunk.num_nodes;

  net->StructuralCheck();

  net->SaveToFile(MODEL_NAME);

  printf("\nOK\n");
}

int main(int argc, char **argv) {
  AddWordsToModel();
  return 0;
}
