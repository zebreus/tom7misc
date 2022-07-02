
#include <memory>

#include "network-gpu.h"
#include "network.h"
#include "mnist.h"
#include "clutil.h"

#include "timer.h"
#include "image.h"

using namespace std;

static constexpr const char *MODEL_NAME = "grad.val";

// Known dimensions for mnist training data.
static constexpr int IMG_WIDTH = 28;
static constexpr int IMG_HEIGHT = 28;

constexpr int INPUT_SIZE = IMG_WIDTH * IMG_HEIGHT;
constexpr int OUTPUT_SIZE = 10;

static CL *cl = nullptr;

int main(int argc, char **argv) {
  cl = new CL;

  std::unique_ptr<Network> net(
      Network::ReadFromFile(MODEL_NAME));

  CHECK(net.get() != nullptr);

  net->StructuralCheck();
  net->NaNCheck(MODEL_NAME);

  auto net_gpu = std::make_unique<NetworkGPU>(cl, net.get());

  std::unique_ptr<ForwardLayerCL> forward_cl =
    std::make_unique<ForwardLayerCL>(cl, net_gpu.get());

  MNIST mnist_test("mnist/t10k");
  CHECK(mnist_test.width == IMG_WIDTH);
  CHECK(mnist_test.height == IMG_HEIGHT);


  const int NUM_TEST = mnist_test.Num();

  std::vector<float> inputs;

  for (int i = 0; i < mnist_test.Num(); i++) {
    const ImageA &img = mnist_test.images[i];
    for (int y = 0; y < IMG_HEIGHT; y++) {
      for (int x = 0; x < IMG_WIDTH; x++) {
        float f = (float)img.GetPixel(x, y) / 255.0f;
        inputs.push_back(f);
      }
    }
  }

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(NUM_TEST, cl, *net));

  training->LoadInputs(inputs);

  Timer fwd_timer;
  for (int src_layer = 0;
       src_layer < net->layers.size() - 1;
       src_layer++) {
    forward_cl->RunForward(training.get(), src_layer);
  }
  printf("Ran forward in %.3fs\n", fwd_timer.Seconds());

  std::vector<float> outputs;
  outputs.resize(NUM_TEST * OUTPUT_SIZE);
  training->ExportOutputs(&outputs);

  int correct = 0;
  for (int idx = 0; idx < NUM_TEST; idx++) {
    int besti = 0;
    float bestv = -1.0/0.0;
    for (int i = 0; i < OUTPUT_SIZE; i++) {
      float f = outputs[idx * OUTPUT_SIZE + i];
      if (f > bestv) {
        bestv = f;
        besti = i;
      }
    }

    if (besti == mnist_test.labels[idx]) {
      correct++;
    }
  }

  printf("%d/%d correct = %.3f%%\n",
         correct, NUM_TEST, (double)(correct * 100.0) / NUM_TEST);

  return 0;
}
