
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

#include "font-problem.h"
#include "fonts/ttf.h"
#include "image.h"
#include "network.h"
#include "threadutil.h"
#include "util.h"

using namespace std;

static constexpr FontProblem::SDFConfig SDF_CONFIG = {};

using uint8 = uint8_t;
using uint32 = uint32_t;
using int64 = int64_t;

using Gen5Result = FontProblem::Gen5Result;
// Gen5Field is one of the 5 fields in Gen5Result with an SDF.
typedef ImageF Gen5Result::*Gen5Field;

struct Op {
  char input_char1, input_char2;
  char output_char;
  // Field to read for the output SDF.
  // Gen5Result::*field;
  Gen5Field field;
  Op(char a, char b, char o) :
    input_char1(a), input_char2(b), output_char(o) {}
};

struct Config {
  string input_font;

  string font_name = "Heavenica";
  string filename = "heavenica.sfd";
  string copyright;

  float extra_scale = 1.0f;
  float linegap = 0.0f;
  float max_gamma = 1.0f;
  float target_frac = 0.0f;
  float blank_width = 1.0f;

  vector<Op> letters;
};

Config Middlecase() {
  Config cfg;
  cfg.input_font = "helvetica.ttf";
  cfg.font_name = "Middlecase";
  cfg.filename = "middlecase.sfd";
  cfg.copyright = "http://tom7.org/lowercase";
  cfg.extra_scale = 1.2f;
  cfg.linegap = -0.1f;
  cfg.max_gamma = 0.95f;
  cfg.target_frac = 0.05f;
  cfg.blank_width = 0.75f;

  for (int i = 0; i < 26; i++) {
    cfg.letters.emplace_back('A' + i, 'a' + i, 'A' + i);
  }

  return cfg;
}

static void GenerateOne(const Network &make_lowercase,
                        const Network &make_uppercase,
                        Config cfg) {
  TTF ttf(cfg.input_font);

  std::mutex out_m;

  vector<pair<char, TTF::Char>> chars =
    ParallelMap(
        cfg.letters,
        [&](Op op) {
          std::optional<ImageA> sdfao =
            ttf.GetSDF(op.input_char1, SDF_CONFIG.sdf_size,
                       SDF_CONFIG.pad_top, SDF_CONFIG.pad_bot,
                       SDF_CONFIG.pad_left,
                       SDF_CONFIG.onedge_value, SDF_CONFIG.falloff_per_pixel);
          std::optional<ImageA> sdfbo =
            ttf.GetSDF(op.input_char2, SDF_CONFIG.sdf_size,
                       SDF_CONFIG.pad_top, SDF_CONFIG.pad_bot,
                       SDF_CONFIG.pad_left,
                       SDF_CONFIG.onedge_value, SDF_CONFIG.falloff_per_pixel);
          CHECK(sdfao.has_value() && sdfbo.has_value());

          ImageF sdfa(sdfao.value());
          ImageF sdfb(sdfbo.value());
          ImageF sdf(SDF_CONFIG.sdf_size, SDF_CONFIG.sdf_size);

          for (int y = 0; y < SDF_CONFIG.sdf_size; y++) {
            for (int x = 0; x < SDF_CONFIG.sdf_size; x++) {
              float fa = sdfa.GetPixel(x, y);
              float fb = sdfb.GetPixel(x, y);
              sdf.SetPixel(x, y, (fa + fb) * 0.5f);
            }
          }

          constexpr float onedge = SDF_CONFIG.onedge_value / 255.0f;

          // Reduce gamma until at least TARGET_FRAC of pixels are
          // above threshold.
          const int target_count =
            cfg.target_frac * (SDF_CONFIG.sdf_size * SDF_CONFIG.sdf_size);
          float gamma = cfg.max_gamma;
          bool adjusted = false;
          while (gamma > 0.01) {
            int count = 0;
            for (int y = 0; y < sdf.Height(); y++) {
              for (int x = 0; x < sdf.Width(); x++) {
                float f = sdf.GetPixel(x, y);
                if (powf(f, gamma) >= onedge) count++;
              }
            }

            if (count >= target_count) {
              break;
            }
            gamma -= 0.01f;
            adjusted = true;
          }

          if (gamma != 1.0f) {
            if (adjusted) {
              MutexLock ml(&out_m);
              printf("%s [%c] Auto reduced gamma to %.3f\n",
                     cfg.font_name.c_str(),
                     op.output_char, gamma);
            }
            for (int y = 0; y < sdf.Height(); y++) {
              for (int x = 0; x < sdf.Width(); x++) {
                const float f = sdf.GetPixel(x, y);
                sdf.SetPixel(x, y, powf(f, gamma));
              }
            }
          }

          const auto [unopt_contours, contours] =
            FontProblem::VectorizeSDF(SDF_CONFIG, sdf);
          const float right_edge =
            FontProblem::GuessRightEdge(SDF_CONFIG, sdf);
          TTF::Char ttf_char =
            FontProblem::ToChar(SDF_CONFIG, contours, right_edge);
          return make_pair(op.output_char, std::move(ttf_char));
        }, 16);

  TTF::Font font;
  font.baseline = FontProblem::TTFBaseline(SDF_CONFIG);
  font.linegap = cfg.linegap;
  font.extra_scale = cfg.extra_scale;
  font.copyright = cfg.copyright;
  for (const auto &[c, ch] : chars) font.chars[c] = ch;

  Util::WriteFile(cfg.filename, font.ToSFD(cfg.font_name));
  printf("Wrote %s\n", cfg.filename.c_str());

}

int main(int argc, char **argv) {

  std::unique_ptr<Network> make_lowercase, make_uppercase;
  make_lowercase.reset(Network::ReadNetworkBinary("net0.val"));
  make_uppercase.reset(Network::ReadNetworkBinary("net1.val"));

  CHECK(make_lowercase.get() != nullptr);
  CHECK(make_uppercase.get() != nullptr);


  // Generate config.
  GenerateOne(*make_lowercase, *make_uppercase, Middlecase());

  return 0;
}
