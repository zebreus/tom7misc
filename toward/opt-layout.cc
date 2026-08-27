
#include "ansi.h"

#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>

#include "base/print.h"
#include "cell-library.h"
#include "layout.h"
#include "opt/optimizer.h"
#include "periodically.h"
#include "prop.h"
#include "status-bar.h"
#include "timer.h"

static void DoOptimize() {
  Timer timer;

  // We don't care about the output; we're trying to find parameters
  // that improve Layout performance (circuit height).
  using LayoutOptimizer = Optimizer<3, 5, char>;
  const Prop prop = []{
      std::optional<Prop> oprop =
        ParseProp(
            // b2b4
            "((545&(643&441))|((545&(646|647))&((430|429)|(441|(432|(433|431))))))"
            // or b2b3
            "|((648&((534|533)|(545|(536|(537|535)))))|((643&545)|((647|646)&((534|533)|(545|(536|(537|535)))))))"
            // or b1b4
            "|((649&(545&(750|751)))&((430|429)|(441|(432|(433|431)))))"
            // or b1b3
            "|((649&(750|751))&((534|533)|(545|(536|(537|535)))))"
                  );

      CHECK(oprop.has_value());
      return std::move(oprop.value());
    }();

  Print("Opt with prop:\n{}\n", PropString(prop));

  int best_height = std::numeric_limits<int>::max();
  CellLibrary library;
  World world;
  Periodically status_per(1.0);
  StatusBar status(1);

  int infeasible = 0;
  int calls = 0;
  LayoutOptimizer::function_type loss_fn =
      [&](const LayoutOptimizer::arg_type &arg) ->
    LayoutOptimizer::return_type {
    const auto &[ints, doubles] = arg;
    calls++;

    std::unique_ptr<LayoutEngine> engine = LayoutEngine::Create(library, world);
    engine->SetVerbose(0);
    engine->SetWriteImages(false);
    engine->SetWriteDebugging(false);

    engine->SetAdditionalAdditionalClearance(ints[0]);
    engine->SetQuiesceDistance(ints[1]);
    engine->SetAdditionalMinQuiesceDistance(ints[2]);

    engine->SetExtWeight(doubles[0]);
    engine->SetClearanceCompressionWeight(doubles[1]);
    engine->SetCorrectSpringWeight(doubles[2]);
    engine->SetQuiesceCompress(doubles[3]);
    engine->SetQuiesceExpand(doubles[4]);

    std::optional<int> max_layers = std::nullopt;
    if (best_height < std::numeric_limits<int>::max()) {
      max_layers = best_height * 2;
    }

    std::span<const Prop> props(&prop, 1);
    std::variant<Layout, std::string> res =
        engine->DoLayoutExt(props, max_layers);

    status_per.RunIf([&]{
        status.Print("Ran {} samples. {} infeasible. Best height: {}",
                     calls, infeasible, best_height);
      });

    if (std::holds_alternative<Layout>(res)) {
      const Layout &layout = std::get<Layout>(res);
      int h = layout.circuit.layers.size();
      if (h < best_height) {
        best_height = h;
      }
      return std::make_pair((double)h, std::optional<char>('a'));
    }

    infeasible++;
    return LayoutOptimizer::INFEASIBLE;
  };

  LayoutOptimizer opt(loss_fn);

  std::array<std::pair<int32_t, int32_t>, 3> int_bounds = {{
    {0, 500},  // AdditionalAdditionalClearance
    {8, 500},  // QuiesceDistance
    {0, 500},  // AdditionalMinQuiesceDistance
  }};

  std::array<std::pair<double, double>, 5> double_bounds = {{
    {0.0, 50.0},     // ExtWeight
    {0.0, 10000.0},  // ClearanceCompressionWeight
    {0.0, 10000.0},  // CorrectSpringWeight
    {0.0, 1000.0},   // QuiesceCompress
    {0.0, 500.0},    // QuiesceExpand
  }};

  constexpr double OPT_SEC = 3600.0 * 2.0;

  opt.Run(int_bounds, double_bounds, std::nullopt, std::nullopt,
          OPT_SEC, std::nullopt);

  std::array<bool, 3> categorical = {false, false, false};
  auto local_results =
    opt.ExploreLocally(categorical, int_bounds, double_bounds, 0.05);

  if (auto best = opt.GetBest()) {
    const auto &[best_arg, best_score, best_out] = best.value();
    local_results.emplace_back(best_arg, best_score, best_out);

    Print(AWHITE("Best height") ": {}\n", best_score);

    const auto &[int_args, double_args] = best_arg;
    const auto &[aac, qd, amqd] = int_args;
    const auto &[ew, ccw, csw, qc, qe] = double_args;

    Print("AdditionalAdditionalClearance: {}\n"
          "QuiesceDistance: {}\n"
          "AdditionalMinQuiesceDistance: {}\n",
          aac, qd, amqd);

    Print("ExtWeight: {:.17g}\n"
          "ClearanceCompressionWeight: {:.17g}\n"
          "CorrectSpringWeight: {:.17g}\n"
          "QuiesceCompress: {:.17g}\n"
          "QuiesceExpand: {:.17g}\n",
          ew, ccw, csw, qc, qe);
  }

  std::array<LayoutOptimizer::IntFeature, 3> int_features = {{
    {"AdditionalAdditionalClearance", false},
    {"QuiesceDistance", false},
    {"AdditionalMinQuiesceDistance", false},
  }};
  std::array<LayoutOptimizer::DoubleFeature, 5> double_features = {{
    {"ExtWeight"},
    {"ClearanceCompressionWeight"},
    {"CorrectSpringWeight"},
    {"QuiesceCompress"},
    {"QuiesceExpand"},
  }};

  auto [features, loss] = opt.Explain(
      local_results, int_features, double_features, "Bias");

  Print("\n" AWHITE("Local Explanation")
        " (Loss: " AYELLOW("{:.4f}") "):\n", loss);
  for (const auto &f : features) {
    if (f.type == LayoutOptimizer::FeatureType::BIAS) {
      Print("  " APURPLE("{:<32}") " " AGREEN("{:+.4f}") "\n",
            f.name + ":", f.coefficient);
    } else if (f.type == LayoutOptimizer::FeatureType::CATEGORICAL_INT) {
      Print("  " ACYAN("{:<32}") " " AGREEN("{:+.4f}") "\n",
            f.name + "=" + std::to_string(f.categorical_value) + ":",
            f.coefficient);
    } else {
      Print("  " ACYAN("{:<32}") " " AGREEN("{:+.4f}") "\n",
            f.name + ":", f.coefficient);
    }
  }

  Print("\nFinished in {}\n", ANSI::Time(timer.Seconds()));
}

int main(int argc, char **argv) {
  ANSI::Init();

  DoOptimize();

  return 0;
}
