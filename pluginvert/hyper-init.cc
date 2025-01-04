
// Optimize the parameters for random network initialization, such
// that the expected variance (over examples) for each node is close
// to 1.

#include <cstdint>
#include <optional>
#include <string>
#include <cmath>
#include <memory>

#include "clutil.h"
#include "network.h"
#include "network-gpu.h"
#include "network-test-util.h"

#include "bounds.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "opt/optimizer.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "util.h"
#include "image.h"
#include "timer.h"

using int64 = int64_t;
using TrainNet = NetworkTestUtil::TrainNet;
using namespace std;

static CL *cl = nullptr;

constexpr int EXAMPLES_PER_ROUND = 4000;

static_assert(EXAMPLES_PER_ROUND >= 2, "Variance would be degenerate "
              "without at least two examples, plus we use two error examples "
              "as scratch space!");

// Here we're talking about variance across examples.
struct VarianceExperiment {
  VarianceExperiment(ArcFour *rc, vector<NetworkGPU *> net_gpus) :
    rc(rc), net_gpus(net_gpus) {

    RandomGaussian gauss(rc);

    for (NetworkGPU *net_gpu : net_gpus) {
      forward_cls.emplace_back(
          std::make_unique<ForwardLayerCL>(cl, net_gpu));
      summary_cls.emplace_back(
          std::make_unique<SummaryStatisticsCL>(cl, net_gpu));
      // printf("Make training...\n");

      TrainingRoundGPU *training = new TrainingRoundGPU(
          EXAMPLES_PER_ROUND,
          cl, *net_gpu->net);

      // Generating a batch of random training examples, once at
      // start (we keep reusing it to save time).
      std::vector<float> flat_inputs;
      flat_inputs.reserve(training->input_size *
                          training->num_examples);
      for (int i = 0; i < training->num_examples; i++) {
        // TODO: The problem should supply input examples,
        // although this is a reasonable default.
        for (int j = 0; j < training->input_size; j++) {
          flat_inputs.push_back(gauss.Next());
        }
      }

      training->LoadInputs(flat_inputs);
      trainings.emplace_back(training);
    }
  }

  double ScoreVariance(double scale, double exponent, bool gaussian_init);

private:
  // not owned
  ArcFour *rc = nullptr;
  // not owned
  vector<NetworkGPU *> net_gpus;
  std::vector<std::unique_ptr<ForwardLayerCL>> forward_cls;
  std::vector<std::unique_ptr<SummaryStatisticsCL>> summary_cls;
  std::vector<std::unique_ptr<TrainingRoundGPU>> trainings;
};

// Randomize the weights, using the scale/exponent/gaussian_init
void HyperRandomizeChunk(
    double scale,
    double exponent,
    bool gaussian_init,
    ArcFour *rc,
    Chunk *chunk) {
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
      const double off = hole_frac * mag;
      for (int i = 0; i < vec->size(); i++) {
        // Uniform in [0,1]
        double d = (double)Rand32(rc) / (double)0xFFFFFFFF;
        bool s = (rc->Byte() & 1) != 0;
        float f = s ? d * -w - off : d * w + off;
        (*vec)[i] = f;
      }
    };

  if (chunk->fixed)
    return;

  // Standard advice is to leave biases at 0 to start.
  for (float &f : chunk->biases) f = 0.0f;

  const float mag = scale / pow(chunk->indices_per_node, exponent);

  if (gaussian_init) {
    RandomizeFloatsGaussian(mag, &chunk->weights);
  } else {
    RandomizeFloatsUniform(mag, &chunk->weights);
  }
}

static void HyperRandomizeNetwork(
    bool gaussian_init,
    ArcFour *rc,
    Network *net) {
  for (int layer_idx = 0; layer_idx < net->layers.size(); layer_idx++) {
    Layer &layer = net->layers[layer_idx];

    for (Chunk &chunk : layer.chunks) {
      const double magnitude = [&]() {
          if (gaussian_init) {
            // Gaussian variates
            switch (chunk.transfer_function) {
              // close to sqrt(3)
            case LEAKY_RELU: return 1.70103025;
            case RELU: return 1.71783215;
            case SIGMOID: return 2.0;
            default:
            case IDENTITY: return 1.0;
            }
          } else {
            // Uniform variates
            switch (chunk.transfer_function) {
            case LEAKY_RELU: return 2.95;
            case RELU: return 2.97;
            case SIGMOID: return 2.0;
            default:
              // close to sqrt(3)
            case IDENTITY: return 1.73;
            }
          }
        }();
      HyperRandomizeChunk(magnitude, 0.5, gaussian_init, rc, &chunk);
    }
  }
}

double VarianceExperiment::ScoreVariance(double scale, double exponent,
                                         bool gaussian_init) {
  // PERF autoparallel
  constexpr int MAX_PARALLELISM = 4;
  int64_t seed = Rand64(rc);
  vector<double> vars =
    ParallelMapi(
        net_gpus,
        [this, seed, scale, exponent, gaussian_init](
            int64_t idx, NetworkGPU *net_gpu) {
          Network *net = net_gpu->net;
          ArcFour rc(StringPrintf("%d.%lld", idx, seed));

          // Reinitialize network randomly; copy to GPU.
          Layer &layer = net->layers.back();
          CHECK(layer.chunks.size() == 1);
          Chunk &chunk = layer.chunks.back();
          HyperRandomizeChunk(scale, exponent, gaussian_init, &rc, &chunk);
          net_gpu->WriteToGPU();

          for (int src_layer = 0;
               src_layer < net->layers.size() - 1;
               src_layer++) {
            forward_cls[idx]->RunForward(trainings[idx].get(), src_layer);
          }

          // Only the last layer, to save time
          summary_cls[idx]->Compute(trainings[idx].get(),
                                    net->layers.size() - 1);

          // Now the first and second example's errors contain the output;
          // we just use the second here.
          // XXX we only need to export the last layer (the rest aren't
          // even initialized)
          Errors variance(*net);
          trainings[idx]->ExportErrors(1, &variance);

          // Just trying to target variance=1 for the last layer.
          const vector<float> &elast = variance.error.back();
          double var = 0.0;
          for (float f : elast) {
            float d = f - 1.0;
            var += d * d;
          }
          return var / elast.size();
        }, MAX_PARALLELISM);
  double total = 0.0;
  for (double v : vars) total += v;
  // total penalty across all networks
  return total;
}


static const char *ChunkTypeNameShort(ChunkType lt) {
  switch (lt) {
  case CHUNK_INPUT: return "INPUT";
  case CHUNK_DENSE: return "DENSE";
  case CHUNK_SPARSE: return "SPARSE";
  case CHUNK_CONVOLUTION_ARRAY: return "CONV";
  default: return "INVALID";
  }
}


using OutputType = double;
using InitOptimizer = Optimizer<0, 1, OutputType>;

// returns best scale, best penalty achieved
static std::pair<double, double>
Optimize(ArcFour *rc,
         TransferFunction tf1,
         TransferFunction tf2,
         bool gaussian_init1,
         bool gaussian_init2,
         int minutes) {

  // uniform weight scale:
  // Leaky relu dense: 2.94
  // Leaky relu sparse: 2.96 (same, as expected)
  // leaky relu conv: 2.955
  // RELU sparse: 2.99
  // SIGMOID sparse: 3.999
  //  (this is the maximum of the range... run again
  //   with a larger one. surprising this would be
  //   big though. variance is always very high, 25--30)
  // IDENTITY sparse: 1.75.
  //  (here there's definitely a minimum in this region,
  //   but it's surprising that it's not 1.0?. same result
  //   with another seed.)

  // OK so I think one issue here is that a uniform distribution
  // has variance like 1/12 * width^2, so if from [-1,1] then
  // (2^2)/12 = 1/3.

  Timer init_timer;

  constexpr ChunkType chunk_type = CHUNK_SPARSE;

  constexpr int NUM_NETWORKS = 32;
  // Generate a variety of networks. Pointers owned.
  vector<Network *> nets;
  vector<NetworkGPU *> net_gpus;

  auto R = [rc](int n) -> int {
      CHECK(n > 0);
      if (n == 1) return 0;
      return RandTo(rc, n);
    };

  for (int i = 0; i < NUM_NETWORKS; i++) {
    std::vector<Layer> layers;
    const int INPUT_SIZE = 4 + R(100);
    Chunk input_chunk;
    input_chunk.type = CHUNK_INPUT;
    input_chunk.num_nodes = INPUT_SIZE;
    input_chunk.width = INPUT_SIZE;
    input_chunk.height = 1;
    input_chunk.channels = 1;
    layers.push_back(Network::LayerFromChunks(input_chunk));

    CHECK(INPUT_SIZE > 2);

    // optional...
    const int HIDDEN_SIZE = 4 + R(100);
    const int HIDDEN_IPN =
      std::clamp(R(INPUT_SIZE * 0.75), 2, INPUT_SIZE);
    const Chunk hidden_chunk =
      Network::MakeRandomSparseChunk(rc, HIDDEN_SIZE,
                                     {Network::SparseSpan{
                                         .span_start = 0,
                                         .span_size = INPUT_SIZE,
                                         .ipn = HIDDEN_IPN,
                                       }},
                                     tf1, SGD);

    const int prev_size = layers.back().num_nodes;

    [[maybe_unused]]
    const int OUTPUT_SIZE = 1 + R(24);

    const Chunk output_chunk = [&]() -> Chunk {
        switch (chunk_type) {
        case CHUNK_DENSE:
          return Network::MakeDenseChunk(OUTPUT_SIZE,
                                         0, prev_size,
                                         tf2,
                                         // Not learning, but specify
                                         // SGD so that we don't need
                                         // to allocate the _aux
                                         // arrays.
                                         SGD);
        case CHUNK_SPARSE: {
          const int IPN =
            std::clamp(R(prev_size * 0.75), 2, prev_size);
          return
            Network::MakeRandomSparseChunk(rc,
                                           OUTPUT_SIZE,
                                           {Network::SparseSpan{
                                               .span_start = 0,
                                               .span_size = prev_size,
                                               .ipn = IPN,
                                             }},
                                           tf2, SGD);
        }
        case CHUNK_CONVOLUTION_ARRAY: {
          const int FEATURES = 1 + R(32);
          const int WIDTH = 1 + R(prev_size - 2);
          const int STRIDE = 1 + R(WIDTH - 1);
          return
            Network::Make1DConvolutionChunk(0, prev_size,
                                            FEATURES, WIDTH, STRIDE,
                                            tf2, SGD);
        }
        default:
          CHECK(false) << "unknown chunk type??";
          Chunk empty;
          return empty;
        }
      }();

    layers.push_back(Network::LayerFromChunks(output_chunk));
    // printf("With %d,%d\n", INPUT_SIZE, OUTPUT_SIZE);
    Network *net = new Network(layers);
    nets.push_back(net);

    // Initial randomization of the whole thing using learned
    // scales. This accomplishes the initialization of the first
    // layer using gaussian_init1.
    // const bool first_gaussian_init = !!(rc->Byte() & 1);
    HyperRandomizeNetwork(gaussian_init1, rc, net);

    NetworkGPU *net_gpu = new NetworkGPU(cl, net);
    net_gpu->SetVerbose(false);
    net_gpus.push_back(net_gpu);
  }


  // Avoid repeatedly constructing this stuff, as they
  // are expensive (compiling kernels, etc.)
  VarianceExperiment experiment(rc, net_gpus);

  int64 calls = 0;
  auto OptimizeMe = [&experiment, &calls, gaussian_init2](
      const InitOptimizer::arg_type arg) ->
    InitOptimizer::return_type {
      const auto [scale] = arg.second;

      calls++;
      if (calls % 1000 == 0) {
        printf("%lld calls\n", calls);
      }

      const double penalty = experiment.ScoreVariance(
          scale, 0.5, gaussian_init2);

      if (std::isnan(penalty)) {
        return InitOptimizer::INFEASIBLE;
      } else {
        // anything else is feasible; just reuse the penalty as the output
        return make_pair(penalty, make_optional(penalty));
      }
    };

  InitOptimizer optimizer(OptimizeMe);
  optimizer.SetSaveAll(true);

  string expt = StringPrintf("%s-%s-%s-%s",
                             TransferFunctionName(tf1),
                             gaussian_init1 ? "gauss" : "unif",
                             TransferFunctionName(tf2),
                             gaussian_init2 ? "gauss" : "unif");

  const double max_scale = tf2 == SIGMOID ? 32.0 : 4.0;

  const double init_ms = init_timer.MS();
  printf("[init %.3fs] Run %s...\n",
         init_ms / 1000.0, expt.c_str());

  Timer optimize_timer;
  optimizer.Run(
      // no int args
      {},
      {
        // scale
        make_pair(0.01, max_scale),
      },
      // no max calls
      nullopt,
      // no max feasible calls
      nullopt,
      { minutes * 60 },
      // no target score
      nullopt);

  double optimize_ms = optimize_timer.MS();
  printf("Made %lld calls in %.3fs\n", calls, optimize_ms / 1000.0);

  auto bo = optimizer.GetBest();
  CHECK(bo.has_value()) << "Always NaN?";
  auto [arg, score, out] = bo.value();
  // auto [width, ipn, depth] = arg.first;
  auto [scale] = arg.second;
  printf("Best arg:\n");
  printf("  %.9f\n", scale);
  printf("Score: %.6f\n", score);

  {
    string csv = expt + "\n";
    for (const auto &[arg, score, out_] : optimizer.GetAll()) {
      auto [scale] = arg.second;
      StringAppendF(&csv, "%.6f,%.6f\n", scale, score);
    }
    string filename = StringPrintf("hyper-%s.csv", expt.c_str());
    Util::WriteFile(filename, csv);
  }

  constexpr int WIDTH = 1024, HEIGHT = 1024;
  Bounds bounds;
  // make sure x-axis is visible
  bounds.Bound(0.0, 0.0);
  bounds.Bound(max_scale, 0.0);
  for (const auto &[arg, score, out_] : optimizer.GetAll()) {
    auto [scale] = arg.second;
    bounds.Bound(scale, score);
  }
  // bounds.AddMarginFrac(0.10);
  printf("Bounds %.3f %.3f to %.3f %.3f\n",
         bounds.MinX(), bounds.MinY(),
         bounds.MaxX(), bounds.MaxY());
  Bounds::Scaler scaler = bounds.Stretch(WIDTH, HEIGHT).FlipY();

  {
    ImageRGBA img(WIDTH, HEIGHT);
    img.Clear32(0x000000FF);

    const int yaxis = scaler.ScaleY(0);
    img.BlendLine32(0, yaxis, WIDTH - 1, yaxis, 0xFFFFFF3F);

    for (int x = 0; x <= max_scale; x++) {
      int xx = scaler.ScaleX(x);
      img.BlendLine32(xx, 0, xx, HEIGHT - 1, 0xFFFFFF3F);
      img.BlendText32(xx + 3, yaxis - 12, 0xFFFFFF7F,
                      StringPrintf("%d", x));
    }

    for (const auto &[arg, score, out_] : optimizer.GetAll()) {
      auto [scale] = arg.second;
      int x = round(scaler.ScaleX(scale));
      int y = round(scaler.ScaleY(score));

      img.BlendBox32(x - 1, y - 1, 3, 3, 0x7FFF7F7F, {0x7FFF7F3F});
      img.BlendPixel32(x, y, 0x7FFF7F9F);
    }
    string filename = StringPrintf("hyper-%s.png", expt.c_str());
    img.Save(filename);
  }

  // Clean up
  for (NetworkGPU *n : net_gpus) delete n;
  net_gpus.clear();
  for (Network *n : nets) delete n;
  nets.clear();

  return make_pair(scale, score);
}

int main(int argc, char **argv) {
  cl = new CL;

  ArcFour rc(StringPrintf("XXXhyper-init.%lld", time(nullptr)));

  auto RunAndLog = [&](TransferFunction tf1,
                       TransferFunction tf2,
                       bool gaussian_init1,
                       bool gaussian_init2,
                       int minutes) {
      printf("Start %s-%s-%s-%s:\n",
             TransferFunctionName(tf1),
             gaussian_init1 ? "gauss" : "unif",
             TransferFunctionName(tf2),
             gaussian_init2 ? "gauss" : "unif");
      const auto [best_scale, best_score] =
        Optimize(&rc, tf1, tf2, gaussian_init1, gaussian_init2, 15);
      FILE *log = fopen("hyper-init-all.csv", "ab");
      CHECK(log) << best_scale;
      fprintf(log, "%s,%s,%s,%s,%.8f,%.8f\n",
              TransferFunctionName(tf1),
              gaussian_init1 ? "gauss" : "unif",
              TransferFunctionName(tf2),
              gaussian_init2 ? "gauss" : "unif",
              best_scale, best_score);
      fclose(log);
    };

  #if 1

  // tf1 = LEAKY_RELU done

  for (TransferFunction tf1 : {IDENTITY}) {
    for (TransferFunction tf2 : {IDENTITY}) {
      for (bool g1 : {false, true}) {
        for (bool g2 : {false, true}) {
          RunAndLog(tf1, tf2, g1, g2, 2);
        }
      }
    }
  }

  #else
  string all;
  // run 'em all
  for (TransferFunction tf : {LEAKY_RELU, RELU, SIGMOID, IDENTITY}) {
    for (ChunkType ct : {CHUNK_SPARSE, CHUNK_DENSE, CHUNK_CONVOLUTION_ARRAY}) {
      for (bool gaussian_init : {false, true}) {
        RunAndLog(tf, ct, gaussian_init, 15);
      }
    }
  }
  #endif


  delete cl;
  return 0;
}

