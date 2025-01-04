
// As a starter problem, tune the network structure for a test
// problem. This can help us find network parameters and/or learning
// rate configuration that works for a given problem.


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

using int64 = int64_t;
using TrainNet = NetworkTestUtil::TrainNet;
using namespace std;

static CL *cl = nullptr;

enum class ResultType {
  CONVERGED,
  DIVERGED,
  TIMEOUT,
};

struct Result {
  ResultType type = ResultType::TIMEOUT;
  int64 iters = 0;
  double seconds = 0.0f;
  double average_loss = 0.0;
};

const char *ResultTypeString(ResultType t) {
  switch (t) {
  case ResultType::CONVERGED: return "CONVERGED";
  case ResultType::DIVERGED: return "DIVERGED";
  case ResultType::TIMEOUT: return "TIMEOUT";
  default: return "??";
  }
}

static bool WorseResult(const Result &l, const Result &r) {
  if (l.type == r.type) {
    switch (l.type) {
    case ResultType::DIVERGED:
      // Earlier divergence is worse
      return l.iters < r.iters;
    case ResultType::CONVERGED:
      // Later convergence is worse
      return r.iters < l.iters;
    default:
    case ResultType::TIMEOUT:
      // Higher loss is worse. Could also take
      // into account the amount of time...
      return l.average_loss > r.average_loss;
    }
  } else {
    if (l.type == ResultType::DIVERGED) return true;
    if (r.type == ResultType::DIVERGED) return false;

    if (l.type == ResultType::TIMEOUT) return true;
    if (r.type == ResultType::TIMEOUT) return false;

    // (both must be converged)
    return false;
  }
}


static Result
Train(TrainNet train_net,
      int64 seed,
      int max_iterations,
      int examples_per_round,
      float converged_threshold,
      float diverged_threshold,
      UpdateWeightsCL::UpdateConfig update_config =
      UpdateWeightsCL::UpdateConfig()) {
  static constexpr bool VERBOSE = true;

  ArcFour rc(StringPrintf("%s.%lld", train_net.name.c_str(), seed));
  RandomGaussian gauss(&rc);

  Network &net = train_net.net;
  // Initialize with random weights.
  RandomizeNetwork(&rc, &net, 2);

  auto net_gpu = make_unique<NetworkGPU>(cl, &net);
  net_gpu->SetVerbose(false);
  
  std::unique_ptr<ForwardLayerCL> forward_cl =
    std::make_unique<ForwardLayerCL>(cl, net_gpu.get());
  std::unique_ptr<SetOutputErrorCL> error_cl =
    std::make_unique<SetOutputErrorCL>(cl, net_gpu.get());
  std::unique_ptr<BackwardLayerCL> backward_cl =
    std::make_unique<BackwardLayerCL>(cl, net_gpu.get());
  [[maybe_unused]]
  std::unique_ptr<DecayWeightsCL> decay_cl =
    std::make_unique<DecayWeightsCL>(cl, net_gpu.get(), 0.999999999f);
  std::unique_ptr<UpdateWeightsCL> update_cl =
    std::make_unique<UpdateWeightsCL>(
        cl, net_gpu.get(), examples_per_round, update_config);

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(examples_per_round, cl, net));

  // Used to compute loss.
  std::vector<std::vector<float>> expected;
  expected.resize(training->num_examples);


  Timer train_timer;
  int64 total_examples = 0LL;
  // Outside the loop so that we can return it if we time out.
  float average_loss = 0.0f;
  for (int iter = 0; iter < max_iterations; iter++) {

    // Initialize training examples as a batch.
    std::vector<float> flat_inputs, flat_outputs;
    flat_inputs.reserve(train_net.NumInputs() * examples_per_round);
    flat_outputs.reserve(train_net.NumOutputs() * examples_per_round);
    for (int i = 0; i < training->num_examples; i++) {
      std::vector<float> inputs;
      inputs.reserve(train_net.NumInputs());

      if (train_net.boolean_input) {
        // For boolean problems, a combination of random
        // bit-strings and sparse ones.
        switch (rc.Byte() & 1) {
        default:
        case 0:
          for (int j = 0; j < train_net.NumInputs(); j++) {
            inputs.push_back(rc.Byte() < 128 ? 1.0f : 0.0f);
          }
          break;
        case 1: {
          // Choose a threshold, which yields strings biased
          // towards 0 or 1.
          const uint8 threshold = rc.Byte();
          for (int j = 0; j < train_net.NumInputs(); j++) {
            inputs.push_back(rc.Byte() < threshold ? 1.0f : 0.0f);
          }
          break;
        }
        }
      } else {
        // Could perhaps consider other distributions? Or the
        // problem could specify it?
        for (int j = 0; j < train_net.NumInputs(); j++) {
          inputs.push_back(gauss.Next());
        }
      }

      // Much faster to load these in a batch.
      for (float f : inputs) flat_inputs.push_back(f);
      std::vector<float> outputs = train_net.f(inputs);
      CHECK(outputs.size() == train_net.NumOutputs());
      for (float f : outputs) flat_outputs.push_back(f);
      // PERF could save the flat outputs and base on that
      expected[i] = std::move(outputs);
    }

    training->LoadInputs(flat_inputs);
    training->LoadExpecteds(flat_outputs);

    for (int src_layer = 0;
         src_layer < net.layers.size() - 1;
         src_layer++) {
      forward_cl->RunForward(training.get(), src_layer);
    }

    error_cl->SetOutputError(training.get());

    for (int dst_layer = net.layers.size() - 1;
         // Don't propagate to input.
         dst_layer > 1;
         dst_layer--) {
      backward_cl->BackwardLayer(training.get(), dst_layer);
    }

    for (int layer_idx = 0; layer_idx < net.layers.size(); layer_idx++) {
      decay_cl->Decay(layer_idx);
    }

    UnParallelComp(net.layers.size() - 1,
                   [&](int layer_minus_1) {
                     const int layer_idx = layer_minus_1 + 1;
                     update_cl->Update(training.get(), layer_idx);
                   },
                   1);

    total_examples += examples_per_round;
    const double total_sec = train_timer.MS() / 1000.0;
    const double eps = total_examples / total_sec;

    net.examples += examples_per_round;
    net.rounds++;

    if (iter % 1000 == 0) {
      // Get loss as abs distance, plus number of incorrect (as booleans).
      // Size of examples = Number of training instances.
      std::vector<std::pair<float, int>> losses =
        UnParallelMapi(expected,
                       [&](int idx, const std::vector<float> exp) {
                         std::vector<float> got;
                         got.resize(exp.size());
                         training->ExportOutput(idx, &got);

                         int incorrect = 0;
                         float loss = 0.0f;
                         for (int i = 0; i < exp.size(); i++) {
                           loss += fabsf(exp[i] - got[i]);
                           // for boolean problems only, but we compute
                           // it either way
                           bool want = exp[i] > 0.5f;
                           bool made = got[i] > 0.5f;
                           if (want != made) incorrect++;
                         }
                         return std::make_pair(loss, incorrect);
                       }, 1);

      average_loss = 0.0f;
      float min_loss = 1.0f / 0.0f, max_loss = 0.0f;
      int min_inc = net.layers.back().num_nodes + 1, max_inc = 0;
      float average_inc = 0.0f;
      for (auto [loss_dist, loss_incorrect] : losses) {
        min_loss = std::min(loss_dist, min_loss);
        max_loss = std::max(loss_dist, max_loss);
        average_loss += loss_dist;

        min_inc = std::min(loss_incorrect, min_inc);
        max_inc = std::max(loss_incorrect, max_inc);
        average_inc += loss_incorrect;
      }
      average_loss /= losses.size();
      average_inc /= losses.size();

      if (VERBOSE) {
        printf("%d: %.3f < %.3f < %.3f", iter,
               min_loss, average_loss, max_loss);
        if (train_net.boolean_output) {
          printf("  |  %d < %.3f < %d",
                 min_inc, average_inc, max_inc);
        }
        printf(" (%.2f eps)\n", eps);
      }

      // Did it diverge?
      if (std::isnan(average_loss) ||
          (average_loss > diverged_threshold && iter >= 500)) {
        Result diverged;
        diverged.type = ResultType::DIVERGED;
        diverged.iters = max_iterations;
        diverged.seconds = train_timer.MS() / 1000.0;
        diverged.average_loss = average_loss;
        return diverged;
      }

      // Met convergence threshold?
      if (train_net.boolean_output ?
          max_inc == 0 : average_loss < converged_threshold) {
        Result converged;
        converged.type = ResultType::CONVERGED;
        converged.iters = max_iterations;
        converged.seconds = train_timer.MS() / 1000.0;
        converged.average_loss = average_loss;
        return converged;
      }
    }
  }

  Result timeout;
  timeout.type = ResultType::TIMEOUT;
  timeout.iters = max_iterations;
  timeout.seconds = train_timer.MS() / 1000.0;
  timeout.average_loss = average_loss;
  return timeout;
}

using OutputType = Result;
using NetOptimizer = Optimizer<3, 3, OutputType>;

static constexpr int SAMPLES = 2;
static constexpr int MAX_ITERATIONS = 50000;
static constexpr int EXAMPLES_PER_ROUND = 1000;
static constexpr int CONVERGED_THRESHOLD = 0.01f;
static constexpr int DIVERGED_THRESHOLD = 4000.0f;

static constexpr double DIVERGED_PENALTY = 10'000'000'000.0;
static constexpr double CONVERGED_PENALTY = -10'000'000'000.0;

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
  Util::WriteFile("hyper-one.csv", all_results);
  printf("Wrote hyper-one.csv\n");
  
  delete cl;
  return 0;
}
