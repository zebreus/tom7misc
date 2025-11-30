
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "image.h"
#include "opencl/clutil.h"
#include "timer.h"

#include "network-gpu.h"
#include "network.h"

using namespace std;

static CL *cl = nullptr;

struct RunMNIST {
  static constexpr int DIGIT_WIDTH = 28;
  static constexpr int DIGIT_HEIGHT = 28;

  static constexpr int DOWNSCALE = 2;
  static constexpr int SCALE = 2;

  // Inference sizes
  static constexpr int INPUT_SIZE = DIGIT_WIDTH * DIGIT_HEIGHT;
  static constexpr int OUTPUT_SIZE = 10;

  std::unique_ptr<ImageRGBA> input_image, output_image;

  RunMNIST(CL *cl, std::string_view infile) : cl(cl) {
    input_image.reset(ImageRGBA::Load(infile));
    CHECK(input_image.get() != nullptr);
    if (DOWNSCALE > 1) {
      *input_image = input_image->ScaleDownBy(DOWNSCALE);
    }
  }

  struct Result {
    double fwd_time = 0.0;
    std::string digits;
    ImageRGBA image;
  };

  // Needs non-const network, but doesn't modify it.
  Result Run(Network *net) {
    ImageRGBA out = input_image->Lightness().GreyscaleRGBA().ScaleBy(SCALE);
    Result result;

    const int rows = input_image->Height() / DIGIT_HEIGHT;
    const int cols = input_image->Width() / DIGIT_WIDTH;

    auto GetBlock = [this](int r, int c) -> ImageA {
        ImageRGBA block = input_image->Crop32(c * DIGIT_WIDTH,
                                              r * DIGIT_HEIGHT,
                                              DIGIT_WIDTH,
                                              DIGIT_HEIGHT);
        return block.Lightness();
      };


    auto net_gpu = std::make_unique<NetworkGPU>(cl, net);

    std::unique_ptr<ForwardLayerCL> forward_cl =
      std::make_unique<ForwardLayerCL>(cl, net_gpu.get());

    const int NUM_PER_BATCH = cols;

    // Uninitialized training examples on GPU.
    std::unique_ptr<TrainingRoundGPU> training(
        new TrainingRoundGPU(NUM_PER_BATCH, cl, *net));

    for (int row = 0; row < rows; row++) {
      std::vector<float> inputs;
      inputs.reserve(NUM_PER_BATCH * DIGIT_HEIGHT * DIGIT_WIDTH);
      for (int col = 0; col < cols; col++) {
        const ImageA img = GetBlock(row, col);
        for (int y = 0; y < DIGIT_HEIGHT; y++) {
          for (int x = 0; x < DIGIT_WIDTH; x++) {
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

      std::vector<uint8_t> digits;
      std::vector<float> max_score;

      for (int idx = 0; idx < NUM_PER_BATCH; idx++) {
        int besti = 0;
        float bestv = -1.0 / 0.0;
        for (int i = 0; i < OUTPUT_SIZE; i++) {
          float f = outputs[idx * OUTPUT_SIZE + i];
          if (f > bestv) {
            bestv = f;
            besti = i;
          }
        }

        digits.push_back(besti);
        max_score.push_back(bestv);
        // Print(stderr, "{} ({:.4f})\n", besti, bestv);
      }

      // Write into image
      for (int col = 0; col < cols; col++) {
        int xx = (col * DIGIT_WIDTH) * SCALE;
        int yy = (row * DIGIT_HEIGHT) * SCALE;
        out.BlendBox32(xx, yy,
                       DIGIT_WIDTH * SCALE, DIGIT_HEIGHT * SCALE,
                       0x00FF0055, std::nullopt);
        uint32_t alpha =
          (uint8_t)std::clamp(255.0f * max_score[col],
                              0.0f, 255.0f);
        out.BlendText32(xx + 4, yy + 4, 0xFFFF3300 + alpha,
                        std::format("{}", digits[col]));
      }

      for (int d : digits) {
        AppendFormat(&result.digits, "{}", d);
      }
    }

    result.image = std::move(out);
    return result;
  }

 private:
  CL *cl = nullptr;
};


int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc >= 3) << "./evaluate-mnist.exe modelfile input.png [output.png]\n";
  cl = new CL;

  const string model_name = argv[1];
  const string input_file = argv[2];
  const string output_file = argc > 3 ? argv[3] : "";

  std::unique_ptr<Network> net(
      Network::ReadFromFile(model_name));
  CHECK(net.get() != nullptr) << model_name;
  net->StructuralCheck();
  net->NaNCheck(model_name);

  RunMNIST run_mnist(cl, input_file);

  RunMNIST::Result res = run_mnist.Run(net.get());
  if (!output_file.empty()) {
    res.image.Save(output_file);
    Print(stderr, "Wrote {}\n", output_file);
  }
  Print("{}\n", res.digits);

  Print(stderr, "OK\n");
  return 0;
}
