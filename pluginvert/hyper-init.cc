
// Optimize initialization of a network.


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

constexpr int EXAMPLES_PER_ROUND = 10000;

// Here we're talking about variance across examples.
struct VarianceExperiment {
  VarianceExperiment(int examples_per_round, NetworkGPU *net_gpu) :
    rc("variance-experiment"),
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

  ArcFour rc;
  const int examples_per_round = 0;
  NetworkGPU *net_gpu = nullptr;
  std::unique_ptr<ForwardLayerCL> forward_cl;
  std::unique_ptr<SummaryStatisticsCL> summary_cl;
  std::unique_ptr<TrainingRoundGPU> training;
};

// If non-null, fill the errors structures with mean, variance for each node.
void VarianceExperiment::GetStatistics(Errors *mean, Errors *variance) {
  const Network &net = *net_gpu->net;
  RandomGaussian gauss(&rc);

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
    uint32 v = MapV(f - 1.0f);
    return (v << 24) | 0xFF;
  }
}


static void VarianceImage(Network *net) {
  auto net_gpu = std::make_unique<NetworkGPU>(cl, net);
  VarianceExperiment experiment(EXAMPLES_PER_ROUND, net_gpu.get());

  // XXX debugging
  auto Fill = [](Errors *err) {
      bool bit = false;
      for (auto &e : err->error) {
        for (float &f : e) {
          f = bit ? -27.272727 : 42.424242;
          bit = !bit;
        }
      }
    };

  Errors mean(*net_gpu->net), variance(*net_gpu->net);
  Fill(&mean);
  Fill(&variance);

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


#if 0
using OutputType = Result;
using NetOptimizer = Optimizer<3, 3, OutputType>;


static NetOptimizer::return_type
OptimizeMe(const NetOptimizer::arg_type arg) {
  const auto &[width, ipn, depth] = arg.first;
  const auto &[lr, damp, ep] = arg.second;
  printf("Trying with model %d %d %d\n"
         "Rates 0.1^%.3f %.6f 0.1^%.3f\n",
         width, ipn, depth, lr, damp, ep);

  UpdateConfig update_config;
  update_config.base_learning_rate = pow(0.1, lr);
  update_config.learning_rate_dampening = damp;
  update_config.adam_epsilon = pow(0.1, ep);

  std::optional<Result> worst;
  int sample = 0;
  int64 num_params = 0;
  for (; sample < SAMPLES; sample++) {
    TrainNet tn = NetworkTestUtil::DodgeballAdam(
        width,
        ipn,
        depth);
    num_params = tn.net.TotalParameters();
    Result r = Train(tn, sample,
                     MAX_ITERATIONS,
                     EXAMPLES_PER_ROUND,
                     CONVERGED_THRESHOLD,
                     DIVERGED_THRESHOLD,
                     update_config);


    // TODO: Better if this merged them, so that we had for
    // example the min/median/max average_loss.
    if (!worst.has_value() || WorseResult(worst.value(), r)) {
      worst = make_optional(r);
    }

    CHECK(worst.has_value());
    if (worst.value().type == ResultType::DIVERGED) {
      // If we ever diverge, don't even try again, and don't consider
      // this a solution.
      break;
    }

  }

  CHECK(worst.has_value());
  switch (worst.value().type) {
  default:
  case ResultType::DIVERGED:
    return make_pair(DIVERGED_PENALTY -
                     worst.value().iters -
                     MAX_ITERATIONS * sample +
                     (num_params / 10.0),
                     nullopt);
  case ResultType::CONVERGED:
    printf("Always converged: %d %d %d, %.6f %.6f %.6f\n",
           width, ipn, depth,
           lr, damp, ep);
    // Always converged! Great!
    return make_pair(CONVERGED_PENALTY -
                     worst.value().iters +
                     (num_params / 10.0),
                     worst);
  case ResultType::TIMEOUT:
    // Prioritize average loss. Assume iters are the same.
    // How, if at all, to include the training time and
    // number of parameters?
    return make_pair(worst.value().average_loss,
                     worst);
  }
}

int main(int argc, char **argv) {
  cl = new CL;

  NetOptimizer optimizer(OptimizeMe);
  optimizer.SetSaveAll(true);

  // TODO: optimizer callback

  // Now searching close to the above with 10x depth
  // Params are width, ipn, depth
  optimizer.Run(
      // int args are width, ipn, depth
      {make_pair(24, 256), make_pair(4, 64), make_pair(2, 8)},
      // double args are learning rate exponent, damping, epsilon exponent
      {make_pair(0.0, 3.0), make_pair(0.1, 10.0), make_pair(0.0, 6.0)},
      // no max calls
      nullopt,
      // no max feasible calls
      nullopt,
      // eight hours
      {3600 * 8},
      // no target score
      nullopt);

  auto bo = optimizer.GetBest();
  CHECK(bo.has_value()) << "Always diverged?";
  auto [arg, score, out] = bo.value();
  auto [width, ipn, depth] = arg.first;
  auto [lr, damp, ep] = arg.second;
  printf("Best arg: %d %d %d | %.6f, %.6f, %.6f\n",
         width, ipn, depth, lr, damp, ep);
  printf("Score: %.6f\n", score);
  printf("Result:\n"
         "type: %s\n"
         "iters: %lld\n"
         "seconds: %.3f\n"
         "average loss: %.6f\n",
         ResultTypeString(out.type),
         out.iters,
         out.seconds,
         out.average_loss);

  string all_results;
  for (const auto &[arg, score, out] : optimizer.GetAll()) {
    const auto &[lr, damp, ep] = arg.second;
    StringAppendF(&all_results,
                  "%d,%d,%d,"
                  "%.6f,%.6f,%.6f,"
                  "%.6f,",
                  width, ipn, depth,
                  lr, damp, ep,
                  score);
    if (out.has_value()) {
      StringAppendF(&all_results,
                    "%s,%lld,%.3f,%.6f",
                    ResultTypeString(out.value().type),
                    out.value().iters,
                    out.value().seconds,
                    out.value().average_loss);
    } else {
      StringAppendF(&all_results, "NONE");
    }
    StringAppendF(&all_results, "\n");
  }
  Util::WriteFile("hyper.csv", all_results);
  printf("Wrote hyper.csv\n");

  delete cl;
  return 0;
}
#endif

int main(int argc, char **argv) {
  CHECK(argc == 2) << "./hyper-init.exe model.val";
  const string modelfile = argv[1];
  cl = new CL;

  std::unique_ptr<Network> net(
      Network::ReadFromFile(modelfile));
  CHECK(net.get() != nullptr);

  ArcFour rc(StringPrintf("variance-image.%lld", time(nullptr)));
  // Initialize with random weights.
  RandomizeNetwork(&rc, net.get(), 2);

  VarianceImage(net.get());

  delete cl;
  printf("OK\n");
  return 0;
}
