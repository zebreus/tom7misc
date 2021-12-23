
// Optimize initialization of a specific network.
// This may be obsolete, as hyper-init determines some good
// general-purpose parameters that should work for most networks.
// But this may be a good tool for exotic network structures
// that don't match the distribution we optimized over?

#include <cstdint>
#include <optional>
#include <string>
#include <cmath>
#include <memory>

#include "clutil.h"
#include "network.h"
#include "network-gpu.h"
#include "network-test-util.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "opt/optimizer.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "util.h"
#include "image.h"

using int64 = int64_t;
using TrainNet = NetworkTestUtil::TrainNet;
using namespace std;

static CL *cl = nullptr;

constexpr int EXAMPLES_PER_ROUND = 4000;

// Here we're talking about variance across examples.
struct VarianceExperiment {
  VarianceExperiment(ArcFour *rc, int examples_per_round, NetworkGPU *net_gpu) :
    rc(rc),
    examples_per_round(examples_per_round),
    net_gpu(net_gpu) {
    CHECK(examples_per_round >= 2) << "Variance would be degenerate "
      "without at least two examples, plus we use two error examples "
      "as scratch space!";
    forward_cl = std::make_unique<ForwardLayerCL>(cl, net_gpu);
    summary_cl = std::make_unique<SummaryStatisticsCL>(cl, net_gpu);

    training = std::make_unique<TrainingRoundGPU>(examples_per_round,
                                                  cl, *net_gpu->net);
  }

  // Get the average mean/variance for the nodes in each chunk.
  void GetStatistics(Errors *mean, Errors *variance);
  vector<vector<double>> GetVarianceLayers(const Errors &variance);

  // not owned
  ArcFour *rc = nullptr;
  const int examples_per_round = 0;
  // not owned
  NetworkGPU *net_gpu = nullptr;
  std::unique_ptr<ForwardLayerCL> forward_cl;
  std::unique_ptr<SummaryStatisticsCL> summary_cl;
  std::unique_ptr<TrainingRoundGPU> training;
};

// If non-null, fill the errors structures with mean, variance for each node.
void VarianceExperiment::GetStatistics(Errors *mean, Errors *variance) {
  const Network &net = *net_gpu->net;
  RandomGaussian gauss(rc);

  // Generating a batch of random training examples.
  std::vector<float> flat_inputs;
  flat_inputs.reserve(training->input_size * training->num_examples);
  for (int i = 0; i < training->num_examples; i++) {

    // TODO: The problem should supply input examples, although
    // this is a reasonable default.
    for (int j = 0; j < training->input_size; j++) {
      flat_inputs.push_back(gauss.Next());
    }
  }

  training->LoadInputs(flat_inputs);

  for (int src_layer = 0;
       src_layer < net.layers.size() - 1;
       src_layer++) {
    forward_cl->RunForward(training.get(), src_layer);
  }

  #if 0
  // XXX test on CPU, slow
  std::vector<Stimulation> stims;
  for (int e = 0; e < training->num_examples; e++) {
    Stimulation stim(net);
    training->ExportStimulation(e, &stim);
    stims.emplace_back(std::move(stim));
  }

  double m = 0.0;
  for (const Stimulation &stim : stims) {
    const vector<float> &out = stim.values.back();
    printf("%.3f %.3f %.3f\n", out[0], out[1], out[2]);
    m += out[0];
  }
  m /= stims.size();
  double v = 0.0;
  for (const Stimulation &stim : stims) {
    const vector<float> &out = stim.values.back();
    double d = out[0] - m;
    v += (d * d);
  }
  v /= stims.size();
  printf("Variance C++: %.3f\n", v);
  #endif

  summary_cl->Compute(training.get());

  // now the first and second example's errors contain the output

  if (mean != nullptr)
    training->ExportErrors(0, mean);
  if (variance != nullptr)
    training->ExportErrors(1, variance);
}

vector<vector<double>>
VarianceExperiment::GetVarianceLayers(const Errors &variance) {
  const Network &net = *net_gpu->net;
  vector<vector<double>> layer_variance;
  for (int layer_idx = 0; layer_idx < net.layers.size(); layer_idx++) {
    const Layer &layer = net.layers[layer_idx];
    vector<double> chunk_variance;
    int node_idx = 0;
    for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
      const Chunk &chunk = layer.chunks[chunk_idx];
      double var = 0.0;
      for (int i = 0; i < chunk.num_nodes; i++) {
        var += variance.error[layer_idx][node_idx];
        node_idx++;
      }
      chunk_variance.push_back(var / chunk.num_nodes);
    }
    layer_variance.push_back(std::move(chunk_variance));
  }
  return layer_variance;
}

static inline uint32 GetMeanColor(float f) {
  auto MapV = [](float f) -> uint8 {
      float ff = sqrt(f);
      int v = roundf(255.0f * ff);
      return std::clamp(v, 0, 255);
    };

  if (f > 0.0f) {
    uint32 v = MapV(f);
    return (v << 16) | 0xFF;
  } else {
    uint32 v = MapV(-f);
    return (v << 24) | 0xFF;
  }
}

// Variance is always positive. Targeting 1.
static inline uint32 GetVarianceColor(float f) {
  auto MapV = [](float f) -> uint8 {
      float ff = sqrt(f);
      int v = roundf(255.0f * ff);
      return std::clamp(v, 0, 255);
    };

  if (f > 1.0f) {
    uint32 v = MapV(f - 1.0f);
    return (v << 16) | 0xFF;
  } else {
    uint32 v = MapV(1.0f - f);
    return (v << 24) | 0xFF;
  }
}


static void VarianceImage(Network *net) {
  auto net_gpu = std::make_unique<NetworkGPU>(cl, net);
  ArcFour rc("variance-image");
  VarianceExperiment experiment(&rc, EXAMPLES_PER_ROUND, net_gpu.get());

  Errors mean(*net_gpu->net), variance(*net_gpu->net);

  experiment.GetStatistics(&mean, &variance);

  auto MakeImages = [net](const string basename, const Errors &errors,
                          std::function<uint32(float)> MapColor) {
      for (int layer_idx = 0; layer_idx < net->layers.size(); layer_idx++) {
        printf("%s %d:\n", basename.c_str(), layer_idx);
        const Layer &layer = net->layers[layer_idx];
        int num_nodes = layer.num_nodes;
        int height = std::clamp((int)round(sqrt(layer.num_nodes)),
                                1, layer.num_nodes);
        int width = (num_nodes / height) + ((num_nodes % height) ? 1 : 0);
        ImageRGBA img(width, height);
        img.Clear32(0x000000FF);
        for (int i = 0; i < num_nodes; i++) {
          const int y = i / width;
          const int x = i % width;
          const float f = errors.error[layer_idx][i];
          const uint32 c = MapColor(f);
          img.SetPixel32(x, y, c);
          printf("  %.3f", f);
        }
        printf("\n");

        int long_edge = std::max(img.Width(), img.Height());
        int scale = 1;
        while ((scale + 1) * long_edge <= 512) scale++;
        if (scale > 1) img = img.ScaleBy(scale);
        img.Save(StringPrintf("%s.%d.png", basename.c_str(), layer_idx));
      }
    };

  MakeImages("mean", mean, GetMeanColor);
  MakeImages("variance", variance, GetVarianceColor);
}


// Randomize the weights, using the per-chunk magnitudes
void HyperRandomizeNetwork(
    const vector<vector<double>> &mags,
    ArcFour *rc,
    Network *net) {
  [[maybe_unused]]
  auto RandomizeFloatsGaussian =
    [&rc](float mag, vector<float> *vec) {
      RandomGaussian gauss{rc};
      for (int i = 0; i < vec->size(); i++) {
        (*vec)[i] = mag * gauss.Next();
      }
    };

  [[maybe_unused]]
  auto RandomizeFloatsUniform =
    [&rc](float mag, vector<float> *vec) {
      // Uniform from -mag to mag.
      const float width = 2.0f * mag;
      for (int i = 0; i < vec->size(); i++) {
        // Uniform in [0,1]
        double d = (double)Rand32(rc) / (double)0xFFFFFFFF;
        float f = (width * d) - mag;
        (*vec)[i] = f;
      }
    };

  // Weights of exactly zero are just wasting indices, as they do
  // not affect the prediction nor propagated error.
  // Here the values are always within [hole_frac * mag, mag] or
  // [-mag, -hole_frag * mag].
  [[maybe_unused]]
  auto RandomizeFloatsDonut =
    [&rc](float hole_frac, float mag, vector<float> *vec) {
      CHECK(hole_frac >= 0.0 && hole_frac < 1.0) << hole_frac;
      // One side.
      const double w = (1.0 - hole_frac) * mag;
      for (int i = 0; i < vec->size(); i++) {
        // Uniform in [0,1]
        double d = (double)Rand32(rc) / (double)0xFFFFFFFF;
        bool s = (rc->Byte() & 1) != 0;
        float f = s ? d * -w : d * w;
        (*vec)[i] = f;
      }
    };

  for (int layer_idx = 0; layer_idx < net->layers.size(); layer_idx++) {
    Layer &layer = net->layers[layer_idx];
    for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
      Chunk *chunk = &layer.chunks[chunk_idx];
      if (chunk->fixed)
        continue;

      // Standard advice is to leave biases at 0 to start.
      for (float &f : chunk->biases) f = 0.0f;

      CHECK(layer_idx < mags.size());
      CHECK(chunk_idx < mags[layer_idx].size());
      const float mag = mags[layer_idx][chunk_idx];

      if (chunk->type == CHUNK_SPARSE) {
        // If sparse, don't output zero (or other tiny) weights.
        RandomizeFloatsDonut(0.01f, mag, &chunk->weights);
      } else {
        RandomizeFloatsUniform(mag, &chunk->weights);
      }
    }
  }

}


using OutputType = double;
using InitOptimizer = Optimizer<0, 8, OutputType>;

static void Optimize(ArcFour *rc, Network *net) {

  // Avoid repeatedly constructing this, stuff, as they
  // are expensive (compiling kernels, etc.)
  auto net_gpu = std::make_unique<NetworkGPU>(cl, net);
  net_gpu->SetVerbose(false);
  VarianceExperiment experiment(rc, EXAMPLES_PER_ROUND, net_gpu.get());

  int64 calls = 0;
  auto OptimizeMe = [rc, net, &net_gpu, &experiment, &calls](
      const InitOptimizer::arg_type arg) ->
    InitOptimizer::return_type {
      const auto &[d0, d1, d2, d3, d4, d5, d6, d7] = arg.second;
      vector<vector<double>> mags = {
        // input layer not interesting
        {1.0f},
        // sparse hidden layers
        {d0}, {d1}, {d2}, {d3}, {d4}, {d5},
        // two chunks on final layer
        {d6, d7},
      };

      calls++;

      // Initialize with random weights on CPU, and then copy
      // to GPU.
      HyperRandomizeNetwork(mags, rc, net_gpu->net);
      net_gpu->WriteToGPU();

      // Get variance
      Errors variance(*net_gpu->net);
      experiment.GetStatistics(nullptr, &variance);

      vector<vector<double>> vl = experiment.GetVarianceLayers(variance);

      // Penalty is squared distance from 1.0, ignoring the input
      // layer (that's just variance of the input, which is gaussian).
      // Each chunk is given the same weight.
      double penalty = 0.0;
      for (int lidx = 1; lidx < vl.size(); lidx++) {
        for (double v : vl[lidx]) {
          double d = v - 1.0;
          penalty += (d * d);
        }
      }

      if (std::isnan(penalty)) {
        return InitOptimizer::INFEASIBLE;
      } else {
        // anything else is feasible; just reuse the penalty as the output
        return make_pair(penalty, make_optional(penalty));
      }
    };

  InitOptimizer optimizer(OptimizeMe);

  std::array<std::pair<double, double>, InitOptimizer::num_doubles> bounds;
  for (int i = 0; i < InitOptimizer::num_doubles; i++)
    bounds[i] = make_pair(1.0e-8, 2.0);

  optimizer.Run(
      // no int args
      {},
      bounds,
      // no max calls
      {1000}, // nullopt,
      // no max feasible calls
      nullopt,
      // one hour
      // {3600 * 1},
      nullopt,
      // no target score
      nullopt);

  printf("Made %lld calls\n", calls);

  auto bo = optimizer.GetBest();
  CHECK(bo.has_value()) << "Always NaN?";
  auto [arg, score, out] = bo.value();
  // auto [width, ipn, depth] = arg.first;
  printf("Best arg:\n");
  for (double mag : arg.second) {
    printf("  %.9f\n", mag);
  }
  printf("Score: %.6f\n", score);
}


#if 0
int main(int argc, char **argv) {
  CHECK(argc == 2) << "./hyper-init.exe model.val";
  const string modelfile = argv[1];
  cl = new CL;

  std::unique_ptr<Network> net(
      Network::ReadFromFile(modelfile));
  CHECK(net.get() != nullptr);

  ArcFour rc(StringPrintf("variance-image.%lld", time(nullptr)));
  // Initialize with random weights.
  // RandomizeNetwork(&rc, net.get(), 2);

  vector<vector<double>> mags = {
    {1.0},
    {0.990320741},
    {0.506674891},
    {0.408527098},
    {0.443092824},
    {0.396415723},
    {0.389330556},
    {1.937256538, 0.119547717},
  };
  HyperRandomizeNetwork(mags, &rc, net.get());

  VarianceImage(net.get());

  delete cl;
  printf("OK\n");
  return 0;
}
#else

int main(int argc, char **argv) {
  CHECK(argc == 2) << "./hyper-init.exe model.val";
  const string modelfile = argv[1];

  cl = new CL;

  std::unique_ptr<Network> net(
      Network::ReadFromFile(modelfile));
  CHECK(net.get() != nullptr);

  ArcFour rc(StringPrintf("hyper.%lld", time(nullptr)));
  Optimize(&rc, net.get());

  delete cl;
  return 0;
}
#endif
