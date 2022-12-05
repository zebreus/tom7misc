
#ifndef _GRAD_EVAL_MNIST_H
#define _GRAD_EVAL_MNIST_H

#include <memory>
#include <vector>
#include <string>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "clutil.h"
#include "network.h"
#include "network-gpu.h"
#include "image.h"
#include "mnist.h"

struct EvalMNIST {
  static constexpr int IMG_WIDTH = 28;
  static constexpr int IMG_HEIGHT = 28;

  static constexpr int INPUT_SIZE = IMG_WIDTH * IMG_HEIGHT;
  static constexpr int OUTPUT_SIZE = 10;

  EvalMNIST(CL *cl) : cl(cl), mnist_test("mnist/t10k") {
    CHECK(mnist_test.width == IMG_WIDTH);
    CHECK(mnist_test.height == IMG_HEIGHT);
  }

  struct Result {
    int total = 0;
    int correct = 0;
    double fwd_time = 0.0;
    ImageRGBA wrong;
    // Confusion matrix. Rows are actual labels, columns are predicted.
    // Sum across all cells = correct.
    std::array<std::array<int, MNIST::RADIX>, MNIST::RADIX> conf;
  };

  // Needs non-const network, but doesn't modify it.
  Result Evaluate(Network *net) {
    Result result;

    for (int r = 0; r < MNIST::RADIX; r++) {
      for (int c = 0; c < MNIST::RADIX; c++) {
        result.conf[r][c] = 0;
      }
    }

    auto net_gpu = std::make_unique<NetworkGPU>(cl, net);

    std::unique_ptr<ForwardLayerCL> forward_cl =
      std::make_unique<ForwardLayerCL>(cl, net_gpu.get());


    const int PAD = 1;
    const int ACROSS = 68;
    const int DOWN = 38;
    const int SQUARE = 28 + PAD;

    result.wrong = ImageRGBA(SQUARE * ACROSS, SQUARE * DOWN);
    result.wrong.Clear32(0x000055FF);
    // Next position for wrong example.
    int xx = 0, yy = 0;

    const int NUM_TEST = mnist_test.Num();
    result.total = NUM_TEST;

    CHECK(NUM_TEST % 10 == 0);
    const int NUM_PER_BATCH = NUM_TEST / 10;
    // Uninitialized training examples on GPU.
    std::unique_ptr<TrainingRoundGPU> training(
        new TrainingRoundGPU(NUM_PER_BATCH, cl, *net));

    for (int batch = 0; batch < 10; batch++) {
      std::vector<float> inputs;
      inputs.reserve(NUM_PER_BATCH * IMG_HEIGHT * IMG_WIDTH);
      for (int i = 0; i < NUM_PER_BATCH; i++) {
        const int idx = batch * NUM_PER_BATCH + i;
        const ImageA &img = mnist_test.images[idx];
        for (int y = 0; y < IMG_HEIGHT; y++) {
          for (int x = 0; x < IMG_WIDTH; x++) {
            float f = (float)img.GetPixel(x, y) / 255.0f;
            inputs.push_back(f);
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
        const int correct_label = mnist_test.labels[example_idx];
        result.conf[correct_label][besti]++;
        if (besti == correct_label) {
          result.correct++;
        } else {
          result.wrong.CopyImage(
              xx * SQUARE, yy * SQUARE,
              mnist_test.images[example_idx].GreyscaleRGBA());
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
  MNIST mnist_test;
};

#endif
