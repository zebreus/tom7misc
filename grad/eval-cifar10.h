
#ifndef _GRAD_EVAL_CIFAR10_H
#define _GRAD_EVAL_CIFAR10_H

#include <memory>
#include <vector>
#include <string>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "clutil.h"
#include "network.h"
#include "network-gpu.h"
#include "image.h"
#include "cifar10.h"

struct EvalCIFAR10 {
  static constexpr int IMG_WIDTH = CIFAR10::WIDTH;
  static constexpr int IMG_HEIGHT = CIFAR10::HEIGHT;

  static constexpr int INPUT_SIZE = IMG_WIDTH * IMG_HEIGHT;
  static constexpr int OUTPUT_SIZE = CIFAR10::RADIX;

  EvalCIFAR10(CL *cl) : cl(cl), cifar10_test("cifar10", true) {
    CHECK(cifar10_test.Num() > 0);
  }

  struct Result {
    int total = 0;
    int correct = 0;
    double fwd_time = 0.0;
    ImageRGBA wrong;
    // Confusion matrix. Rows are actual labels, columns are predicted.
    // Sum across all cells = correct.
    std::array<std::array<int, CIFAR10::RADIX>, CIFAR10::RADIX> conf;
  };

  static void TitleResult(Result *r) {
    if (r->wrong.Height() < 20) return;
    r->wrong.BlendRect32(0, r->wrong.Height() - 20,
                         r->wrong.Width(), 20, 0x000000AA);
    r->wrong.BlendText2x32(
        2, r->wrong.Height() - 19,
        0xCCCCCCFF,
        StringPrintf("%d total, %d correct (%.3f%%), %.1f ex/sec",
                     r->total, r->correct,
                     (r->correct * 100.0) / r->total,
                     r->total / r->fwd_time));
  }

  // Needs non-const network, but doesn't modify it.
  Result Evaluate(Network *net) {
    Result result;
    for (int r = 0; r < CIFAR10::RADIX; r++) {
      for (int c = 0; c < CIFAR10::RADIX; c++) {
        result.conf[r][c] = 0;
      }
    }

    auto net_gpu = std::make_unique<NetworkGPU>(cl, net);

    std::unique_ptr<ForwardLayerCL> forward_cl =
      std::make_unique<ForwardLayerCL>(cl, net_gpu.get());

    const int PAD = 1;
    // To fit in 1920x1080.
    const int ACROSS = 58;
    const int DOWN = 32;
    const int SQUARE = IMG_WIDTH + PAD;

    result.wrong = ImageRGBA(SQUARE * ACROSS, SQUARE * DOWN);
    result.wrong.Clear32(0x000055FF);
    // Next position for wrong example.
    int xx = 0, yy = 0;

    const int NUM_TEST = cifar10_test.Num();
    // Can be any divisor; just needs to fit on GPU.
    const int NUM_BATCHES = 100;
    result.total = NUM_TEST;

    CHECK(NUM_TEST % NUM_BATCHES == 0);
    const int NUM_PER_BATCH = NUM_TEST / NUM_BATCHES;
    // Uninitialized training examples on GPU.
    std::unique_ptr<TrainingRoundGPU> training(
        new TrainingRoundGPU(NUM_PER_BATCH, cl, *net));

    for (int batch = 0; batch < NUM_BATCHES; batch++) {
      std::vector<float> inputs;
      inputs.reserve(NUM_PER_BATCH * IMG_HEIGHT * IMG_WIDTH);
      for (int i = 0; i < NUM_PER_BATCH; i++) {
        const int idx = batch * NUM_PER_BATCH + i;
        const ImageRGBA &img = cifar10_test.images[idx];
        for (int y = 0; y < IMG_HEIGHT; y++) {
          for (int x = 0; x < IMG_WIDTH; x++) {
            auto [r, g, b, a_] = img.GetPixel(x, y);
            inputs.push_back(r * (1.0f / 255.0f));
            inputs.push_back(g * (1.0f / 255.0f));
            inputs.push_back(b * (1.0f / 255.0f));
          }
        }
      }

      training->LoadInputs(inputs);

      Timer fwd_timer;
      for (int src_layer = 0;
           src_layer < net->layers.size() - 1;
           src_layer++) {
        forward_cl->RunForward(training.get(), src_layer);
      }
      result.fwd_time += fwd_timer.Seconds();

      std::vector<float> outputs;
      outputs.resize(NUM_PER_BATCH * OUTPUT_SIZE);
      training->ExportOutputs(&outputs);

      for (int idx = 0; idx < NUM_PER_BATCH; idx++) {
        int besti = 0;
        float bestv = -1.0/0.0;
        for (int i = 0; i < OUTPUT_SIZE; i++) {
          float f = outputs[idx * OUTPUT_SIZE + i];
          if (f > bestv) {
            bestv = f;
            besti = i;
          }
        }

        // XXX
        const int example_idx = batch * NUM_PER_BATCH + idx;
        const int correct_label = cifar10_test.labels[example_idx];
        result.conf[correct_label][besti]++;
        if (besti == correct_label) {
          result.correct++;
        } else {
          result.wrong.CopyImage(
              xx * SQUARE, yy * SQUARE,
              cifar10_test.images[example_idx]);
          result.wrong.BlendText32(xx * SQUARE, yy * SQUARE,
                                   0x00FF00AA,
                                   StringPrintf("%c", correct_label + '0'));
          result.wrong.BlendText32(xx * SQUARE + (SQUARE - 10), yy * SQUARE,
                                   0xFF0000AA,
                                   StringPrintf("%c", besti + '0'));
          xx++;
          if (xx > ACROSS) {
            xx = 0;
            yy++;
          }
        }
      }
    }

    return result;
  }

private:
  CL *cl = nullptr;
  CIFAR10 cifar10_test;
};

#endif
