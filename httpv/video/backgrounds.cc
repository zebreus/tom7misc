
#include <algorithm>
#include <memory>
#include <cmath>
#include <utility>
#include <vector>

#include "base/print.h"
#include "mov-recorder.h"
#include "ansi.h"
#include "image.h"
#include "arcfour.h"
#include "periodically.h"
#include "randutil.h"
#include "threadutil.h"
#include "color-util.h"
#include "status-bar.h"
#include "lines.h"
#include "geom/marching.h"

static constexpr int WIDTH = 3840;
static constexpr int HEIGHT = 2160;

struct ParametricAnim {
  // PRNG can be used to initialize state.
  ParametricAnim(ArcFour *rc) {}
  virtual ~ParametricAnim() {}

  // For a time in 0..1. The expectation is that
  // MakeFrame(0.0) = MakeFrame(1.0) so that the animation
  // loops!
  virtual ImageRGBA MakeFrame(double t) const = 0;
};

struct VaporwaveAnim : public ParametricAnim {
  int W = 120;
  int D = 80;
  std::vector<float> H;

  VaporwaveAnim(ArcFour *rc) : ParametricAnim(rc), H(W) {
    for (int x = 0; x < W; x++) {
      float mid_dist = std::abs(x - W / 2.0f);
      if (mid_dist < 8.0f) {
        H[x] = 0.0f;
      } else {
        float r = rc->Byte() / 255.0f;
        H[x] = (mid_dist - 8.0f) * (0.8f + 1.2f * r);
      }
    }
    // Smooth the heights to look more like natural mountains
    for (int iter = 0; iter < 5; iter++) {
      std::vector<float> next = H;
      for (int x = 1; x < W - 1; x++) {
        if (std::abs(x - W / 2.0f) < 8.0f) continue;
        next[x] = (H[x-1] + H[x] * 2 + H[x+1]) / 4.0f;
      }
      H = next;
    }
  }

  void DrawLine(ImageRGBA &img, float x1, float y1, float x2, float y2,
                float radius, uint32_t c) const {
    img.BlendThickLine32(x1, y1, x2, y2, radius, c);
  }

  ImageRGBA MakeFrame(double t) const override {
    ImageRGBA img(WIDTH, HEIGHT);

    int horizon_y = HEIGHT / 2 + 150;

    // Background sky gradient
    ColorUtil::Gradient sky_grad = {
      GradRGB(0.0f, 0x050015),
      GradRGB(0.6f, 0x300050),
      GradRGB(1.0f, 0xFF0080)
    };
    for (int y = 0; y < horizon_y; y++) {
      float f = y / (float)horizon_y;
      img.FillRect32(0, y, WIDTH, 1, ColorUtil::LinearGradient32(sky_grad, f));
    }

    // Ground
    img.FillRect32(0, horizon_y, WIDTH, HEIGHT - horizon_y, 0x050010FF);

    // Retro Sun
    int sun_r = 700;
    int sun_x = WIDTH / 2;
    int sun_y = horizon_y - 100;

    ColorUtil::Gradient sun_grad = {
      GradRGB(0.0f, 0xFFFF00),
      GradRGB(0.5f, 0xFF8000),
      GradRGB(1.0f, 0xFF0080)
    };

    ColorUtil::Gradient sun_alt_grad = {
      GradRGB(0.0f, 0xFFFF00),
      GradRGB(0.5f, 0xAA0080),
      GradRGB(1.0f, 0x8000FF)
    };

    // Height above the horizon where horizontal cutouts begin.
    const float cutout_height = 450.0f;
    // Total number of cutout stripes in that region.
    const float num_stripes = 12.0f;
    // Exponent for stripe phase. < 1.0 compresses stripes near the horizon.
    const float phase_exp = 0.6f;
    // Exponent for duty cycle. Controls how quickly the primary sun color
    // thins out near the horizon.
    const float duty_exp = 0.8f;

    for (int y = sun_y - sun_r; y <= sun_y + sun_r; y++) {
      if (y >= horizon_y) break;
      if (y < 0) continue;
      float dy = y - sun_y;
      if (dy * dy <= sun_r * sun_r) {
        float dx = std::sqrt(sun_r * sun_r - dy * dy);
        float prog = (dy + sun_r) / (2.0f * sun_r);

        bool use_alt = false;
        float h_dist = horizon_y - y;
        if (h_dist < cutout_height) {
          // f goes from 0.0 at the horizon to 1.0 at the top of the cutouts
          float f = h_dist / cutout_height;
          float phase = std::pow(f, phase_exp) * num_stripes;
          float stripe = std::fmod(phase, 1.0f);

          // main_thickness gets smaller as f approaches 0 (horizon),
          // thinning out the main color bands in a 1D stipple effect.
          float main_thickness = std::pow(f, duty_exp);
          if (stripe > main_thickness) {
            use_alt = true;
          }
        }

        uint32_t c = ColorUtil::LinearGradient32(
            use_alt ? sun_alt_grad : sun_grad, prog);
        img.FillRect32((int)(sun_x - dx), y, (int)(2 * dx), 1, c);
      }
    }

    // --- Sun Reflection Constants ---
    // Maximum distance the reflection extends below the horizon.
    const float refl_length = (HEIGHT - horizon_y) * 0.8f;
    // Initial vertical gap between reflection lines at the horizon.
    const int init_y_gap = 10;
    // How much the gap increases each step to simulate perspective.
    const int gap_increase = 4;

    int y_gap = init_y_gap;
    for (int y = horizon_y + y_gap; y < HEIGHT; y += y_gap) {
      y_gap += gap_increase;
      float dy = y - horizon_y;
      if (dy > refl_length) break;

      // prog_y goes from 0.0 at the horizon to 1.0 at the reflection tip
      float prog_y = dy / refl_length;

      // Squashed semicircle: width proportional to sqrt(1 - prog_y^2)
      float width = sun_r * std::sqrt(std::max(0.0f, 1.0f - prog_y * prog_y));

      uint32_t c = ColorUtil::LinearGradient32(sun_grad, 1.0f - prog_y * 0.5f);
      uint32_t alpha = (uint32_t)(200.0f * (1.0f - prog_y));
      c = (c & 0xFFFFFF00) | alpha;
      DrawLine(img, sun_x - width, y, sun_x + width, y, 6.0f, c);
    }

    // 3D Grid projection setup
    float cell_size = 80.0f;
    float cam_y = 40.0f;
    float fov = 2500.0f;

    auto Project = [&](float wx, float wy, float z) -> std::pair<float, float> {
      float sx = WIDTH / 2.0f + (wx / z) * fov;
      float sy = horizon_y - ((wy - cam_y) / z) * fov;
      return {sx, sy};
    };

    uint32_t neon = 0xFF00FF00; // Magenta base color, alpha gets added later

    // Draw horizontal lines (constant Z)
    for (int z_idx = 0; z_idx <= D + 1; z_idx++) {
      float z_dist = z_idx - t;
      if (z_dist < 0.2f) continue;

      float fade = 1.0f;
      if (z_dist > D * 0.4f) {
        fade = 1.0f - ((z_dist - D * 0.4f) / (D * 0.6f));
      }
      if (fade <= 0.0f) continue;

      uint32_t c = neon | (uint32_t)(fade * 255.0f);

      float prev_sx = 0, prev_sy = 0;
      for (int x = 0; x < W; x++) {
        float wx = (x - W / 2.0f) * cell_size;
        float wy = H[x] * cell_size;
        auto [sx, sy] = Project(wx, wy, z_dist * cell_size);
        if (x > 0) {
          DrawLine(img, prev_sx, prev_sy, sx, sy, 4.0f, c);
        }
        prev_sx = sx; prev_sy = sy;
      }
    }

    // Draw vertical lines (constant X)
    for (int x = 0; x < W; x++) {
      float wx = (x - W / 2.0f) * cell_size;
      float wy = H[x] * cell_size;

      float prev_sx = 0, prev_sy = 0;
      float prev_fade = 0;
      float prev_z_dist = 0;
      bool first = true;
      for (int z_idx = 0; z_idx <= D + 1; z_idx++) {
        float z_dist = z_idx - t;
        float fade = 1.0f;
        if (z_dist > D * 0.4f) {
          fade = 1.0f - ((z_dist - D * 0.4f) / (D * 0.6f));
        }
        if (fade < 0.0f) fade = 0.0f;

        float pz = std::max(0.2f, z_dist);
        auto [sx, sy] = Project(wx, wy, pz * cell_size);

        if (!first && (z_dist >= 0.2f || prev_z_dist >= 0.2f)) {
          if (fade > 0.0f || prev_fade > 0.0f) {
            float avg_fade = (fade + prev_fade) * 0.5f;
            uint32_t c = neon | (uint32_t)(avg_fade * 255.0f);
            DrawLine(img, prev_sx, prev_sy, sx, sy, 4.0f, c);
          }
        }
        prev_sx = sx; prev_sy = sy;
        prev_fade = fade;
        prev_z_dist = z_dist;
        first = false;
      }
    }

    return img;
  }

};

// This is meeeeeeeee!!!!

struct Vaporwave2Anim : public ParametricAnim {
  static constexpr int GRID_REPEAT = 7;
  int W = 120;
  int D = 80;
  std::vector<std::vector<float>> H;

  Vaporwave2Anim(ArcFour *rc) : ParametricAnim(rc),
                                H(GRID_REPEAT, std::vector<float>(W, 0.0f)) {

    for (int z = 0; z < GRID_REPEAT; z++) {
      for (int x = 0; x < W; x++) {
        float mid_dist = std::abs(x - W / 2.0f);
        float r = rc->Byte() / 255.0f;
        if (mid_dist < 3.0f) {
          H[z][x] = 0.0;
        } else if (mid_dist < 8.0f) {
          H[z][x] = r * 0.2f;
        } else {
          float dist = mid_dist - 8.0f;
          // A cosine envelope makes the terrain rise and fall rather than
          // growing infinitely at the edges.
          float env = 6.0f * (1.0f - std::cos(dist * 0.15f));
          H[z][x] = env * (0.8f + 1.2f * r);
        }
      }
    }

    // Smooth the heights to look more like natural mountains (in 2D)
    for (int iter = 0; iter < 5; iter++) {
      std::vector<std::vector<float>> next = H;
      for (int z = 0; z < GRID_REPEAT; z++) {
        int prev_z = (z + GRID_REPEAT - 1) % GRID_REPEAT;
        int next_z = (z + 1) % GRID_REPEAT;
        for (int x = 1; x < W - 1; x++) {
          if (std::abs(x - W / 2.0f) < 8.0f) continue;
          next[z][x] = (H[prev_z][x] + H[next_z][x] +
                        H[z][x-1] + H[z][x+1] +
                        H[z][x] * 4.0f) / 8.0f;
        }
      }
      H = next;
    }
  }

  float GetH(int z_idx, int x) const {
    int mod = z_idx % GRID_REPEAT;
    if (mod < 0) mod += GRID_REPEAT;
    return H[mod][x];
  }

  void DrawLine(ImageRGBA &img, float x1, float y1, float x2, float y2,
                float radius, uint32_t c) const {
    img.BlendThickLine32(x1, y1, x2, y2, radius, c);
  }

  ImageRGBA MakeFrame(double t) const override {
    ImageRGBA img(WIDTH, HEIGHT);

    int horizon_y = HEIGHT / 2 + 150;

    // Background sky gradient
    ColorUtil::Gradient sky_grad = {
      GradRGB(0.0f, 0x050015),
      GradRGB(0.6f, 0x300050),
      GradRGB(1.0f, 0xFF0080)
    };
    for (int y = 0; y < horizon_y; y++) {
      float f = y / (float)horizon_y;
      img.FillRect32(0, y, WIDTH, 1, ColorUtil::LinearGradient32(sky_grad, f));
    }

    // Ground
    img.FillRect32(0, horizon_y, WIDTH, HEIGHT - horizon_y, 0x050010FF);

    // Retro Sun
    int sun_r = 700;
    int sun_x = WIDTH / 2;
    int sun_y = horizon_y - 100;

    ColorUtil::Gradient sun_grad = {
      GradRGB(0.0f, 0xFFFF00),
      GradRGB(0.5f, 0xFF8000),
      GradRGB(1.0f, 0xFF0080)
    };

    ColorUtil::Gradient sun_alt_grad = {
      GradRGB(0.0f, 0xFFFF00),
      GradRGB(0.5f, 0xAA0080),
      GradRGB(1.0f, 0x8000FF)
    };

    // Height above the horizon where horizontal cutouts begin.
    const float cutout_height = 450.0f;
    // Total number of cutout stripes in that region.
    const float num_stripes = 12.0f;
    // Exponent for stripe phase. < 1.0 compresses stripes near the horizon.
    const float phase_exp = 0.6f;
    // Exponent for duty cycle. Controls how quickly the primary sun color
    // thins out near the horizon.
    const float duty_exp = 0.8f;

    for (int y = sun_y - sun_r; y <= sun_y + sun_r; y++) {
      if (y >= horizon_y) break;
      if (y < 0) continue;
      float dy = y - sun_y;
      if (dy * dy <= sun_r * sun_r) {
        float dx = std::sqrt(sun_r * sun_r - dy * dy);
        float prog = (dy + sun_r) / (2.0f * sun_r);

        bool use_alt = false;
        float h_dist = horizon_y - y;
        if (h_dist < cutout_height) {
          // f goes from 0.0 at the horizon to 1.0 at the top of the cutouts
          float f = h_dist / cutout_height;
          float phase = std::pow(f, phase_exp) * num_stripes;
          float stripe = std::fmod(phase, 1.0f);

          // main_thickness gets smaller as f approaches 0 (horizon),
          // thinning out the main color bands in a 1D stipple effect.
          float main_thickness = std::pow(f, duty_exp);
          if (stripe > main_thickness) {
            use_alt = true;
          }
        }

        uint32_t c = ColorUtil::LinearGradient32(
            use_alt ? sun_alt_grad : sun_grad, prog);
        img.FillRect32((int)(sun_x - dx), y, (int)(2 * dx), 1, c);
      }
    }

    // --- Sun Reflection Constants ---
    // Maximum distance the reflection extends below the horizon.
    const float refl_length = (HEIGHT - horizon_y) * 0.8f;
    // Initial vertical gap between reflection lines at the horizon.
    const int init_y_gap = 10;
    // How much the gap increases each step to simulate perspective.
    const int gap_increase = 4;

    int y_gap = init_y_gap;
    for (int y = horizon_y + y_gap; y < HEIGHT; y += y_gap) {
      y_gap += gap_increase;
      float dy = y - horizon_y;
      if (dy > refl_length) break;

      // prog_y goes from 0.0 at the horizon to 1.0 at the reflection tip
      float prog_y = dy / refl_length;

      // Squashed semicircle: width proportional to sqrt(1 - prog_y^2)
      float width = sun_r * std::sqrt(std::max(0.0f, 1.0f - prog_y * prog_y));

      uint32_t c = ColorUtil::LinearGradient32(sun_grad, 1.0f - prog_y * 0.5f);
      uint32_t alpha = (uint32_t)(200.0f * (1.0f - prog_y));
      c = (c & 0xFFFFFF00) | alpha;
      DrawLine(img, sun_x - width, y, sun_x + width, y, 6.0f, c);
    }

    // 3D Grid projection setup
    float cell_size = 80.0f;
    float cam_y = 40.0f;
    float fov = 2500.0f;

    auto Project = [&](float wx, float wy, float z) -> std::pair<float, float> {
      float sx = WIDTH / 2.0f + (wx / z) * fov;
      float sy = horizon_y - ((wy - cam_y) / z) * fov;
      return {sx, sy};
    };

    uint32_t neon = 0xFF00FF00; // Magenta base color, alpha gets added later

    // Draw horizontal lines (constant Z)
    for (int z_idx = 0; z_idx <= D + GRID_REPEAT + 1; z_idx++) {
      float z_dist = z_idx - t * GRID_REPEAT;
      if (z_dist < 0.2f) continue;

      float fade = 1.0f;
      if (z_dist > D * 0.4f) {
        fade = 1.0f - ((z_dist - D * 0.4f) / (D * 0.6f));
      }
      if (fade <= 0.0f) continue;

      uint32_t c = neon | (uint32_t)(fade * 255.0f);

      float prev_sx = 0, prev_sy = 0;
      for (int x = 0; x < W; x++) {
        float wx = (x - W / 2.0f) * cell_size;
        float wy = GetH(z_idx, x) * cell_size;
        auto [sx, sy] = Project(wx, wy, z_dist * cell_size);
        if (x > 0) {
          DrawLine(img, prev_sx, prev_sy, sx, sy, 4.0f, c);
        }
        prev_sx = sx; prev_sy = sy;
      }
    }

    // Draw vertical lines (constant X)
    for (int x = 0; x < W; x++) {
      float prev_sx = 0, prev_sy = 0;
      float prev_fade = 0;
      float prev_z_dist = 0;
      bool first = true;
      for (int z_idx = 0; z_idx <= D + GRID_REPEAT + 1; z_idx++) {
        float z_dist = z_idx - t * GRID_REPEAT;
        float fade = 1.0f;
        if (z_dist > D * 0.4f) {
          fade = 1.0f - ((z_dist - D * 0.4f) / (D * 0.6f));
        }
        if (fade < 0.0f) fade = 0.0f;

        float pz = std::max(0.2f, z_dist);
        float wx = (x - W / 2.0f) * cell_size;
        float wy = GetH(z_idx, x) * cell_size;
        auto [sx, sy] = Project(wx, wy, pz * cell_size);

        if (!first && (z_dist >= 0.2f || prev_z_dist >= 0.2f)) {
          if (fade > 0.0f || prev_fade > 0.0f) {
            float avg_fade = (fade + prev_fade) * 0.5f;
            uint32_t c = neon | (uint32_t)(avg_fade * 255.0f);
            DrawLine(img, prev_sx, prev_sy, sx, sy, 4.0f, c);
          }
        }
        prev_sx = sx; prev_sy = sy;
        prev_fade = fade;
        prev_z_dist = z_dist;
        first = false;
      }
    }

    return img;
  }

};

static void Render() {
  static constexpr int FRAMES = 300;
  ArcFour rc("fixed seed");
  std::unique_ptr<ParametricAnim> anim(new Vaporwave2Anim(&rc));

  MovRecorder recorder("anim.mov", WIDTH, HEIGHT);
  recorder.SetEncodingThreads(8);
  recorder.SetMaxQueueSize(16);
  StatusBar status(1);
  Periodically status_per(0.5);
  for (int i = 0; i < FRAMES; i++) {
    // Since frame(0) = frame(1), we do not want a duplicate
    // frame at the end. so i < FRAMES is correct here.
    double t = i / (double)FRAMES;
    recorder.AddFrame(anim->MakeFrame(t));
    status_per.RunIf([&]() {
        status.Progress(i, FRAMES, "Rendering");
      });
  }

  Print("Done generating frames. Finalizing...\n");
  // MovRecorder's destructor writes.
}

int main(int argc, char **argv) {
  ANSI::Init();

  Render();

  return 0;
}
