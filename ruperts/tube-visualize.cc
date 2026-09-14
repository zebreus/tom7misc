// High-resolution spherical map and margin visualization for Nopert #229
// Visualizes certified local view tables and empirical clearances across
// the upper wedge fundamental domain and the full view sphere.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "image.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "ruperts-util.h"
#include "util.h"
#include "yocto-math.h"

template<typename... Args>
inline void Printf(const char *fmt, Args &&...args) {
  std::printf(fmt, std::forward<Args>(args)...);
}
inline void Printf(const char *str) {
  std::fputs(str, stdout);
}

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;

// Target 16:9 canvas dimensions (4K UHD resolution).
static constexpr int WIDTH = 3840;
static constexpr int HEIGHT = 2160;

// Fundamental upper wedge geometry for Nopert #229 (fivefold symmetry C5).
// Azimuth phi in [0, 2*pi/5] = [0, 72.0 deg].
// Elevation alpha in [0, pi/2] = [0, 90.0 deg].
[[maybe_unused]] static constexpr double PHI_MAX_RAD = 2.0 * std::numbers::pi / 5.0; // 72 deg
static constexpr double PHI_MAX_DEG = 72.0;
[[maybe_unused]] static constexpr double ELEV_MAX_RAD = std::numbers::pi / 2.0;     // 90 deg
static constexpr double ELEV_MAX_DEG = 90.0;

// Color ramp range for log-scale clearance margin c.
static constexpr double C_MIN = 0.00020;
static constexpr double C_MAX = 0.01000;

// Gradient stops specified in TUBE_VISUALIZE.md:
// tau = 0.0 (c <= 0.00022): Deep Crimson
// tau = 0.2 (c ≈ 0.00035): Orange
// tau = 0.4 (c ≈ 0.00070): Yellow
// tau = 0.7 (c ≈ 0.00250): Cyan / Teal
// tau = 1.0 (c >= 0.01000): Deep Cobalt Blue
static constexpr ColorUtil::Gradient MARGIN_RAMP{
  GradRGB(0.0f, 0xC8102E), // Deep Crimson (danger zone, narrowest canyons)
  GradRGB(0.2f, 0xFF8C00), // Orange
  GradRGB(0.4f, 0xFFD700), // Yellow
  GradRGB(0.7f, 0x00C0C0), // Cyan / Teal
  GradRGB(1.0f, 0x0047AB), // Deep Cobalt Blue (wide margins)
};

// Known canyon locations for annotation:
struct CanyonInfo {
  std::string_view name;
  vec3 v;
  double phi_deg;
  double phi_rad;
  double elev_deg;
  double elev_rad;
  double c;
  std::string_view label;
  std::string_view note;
};

static const CanyonInfo CANYON1{
  .name = "Canyon 1 (Equatorial)",
  .v = {0.98516, 0.03814, 0.16734},
  .phi_deg = 2.217,
  .phi_rad = 2.217 * std::numbers::pi / 180.0,
  .elev_deg = 9.633,
  .elev_rad = 9.633 * std::numbers::pi / 180.0,
  .c = 0.0002509,
  .label = "Canyon 1: c = 0.0002509",
  .note = "Equatorial silhouette notch (depth 28)",
};

static const CanyonInfo CANYON2{
  .name = "Canyon 2 (Mid-Elevation)",
  .v = {0.92451, 0.21174, 0.31694},
  .phi_deg = 12.900,
  .phi_rad = 12.900 * std::numbers::pi / 180.0,
  .elev_deg = 18.478,
  .elev_rad = 18.478 * std::numbers::pi / 180.0,
  .c = 1139.0 / 5000000.0, // 0.000227800
  .label = "Canyon 2: c = 0.0002278",
  .note = "Global min pinch (r <= 0.0004556)",
};

// Certified leaf triangle representation.
struct LeafTriangle {
  int id = 0;
  int root = 0;
  int depth = 0;
  vec3 p[3];       // Projective ray vertices
  vec3 unit_v[3];  // Normalized unit vectors on S^2
  double phi[3];   // Azimuth in degrees [0, 72]
  double elev[3];  // Elevation in degrees [0, 90]
  double z[3];     // Unit z-coordinate for Lambert [0, 1]
  double c = 0.0;  // Certified margin
  uint32_t color = 0;
};

// Empirical point sample.
struct EmpiricalSample {
  vec3 v;
  double phi_deg = 0.0;
  double elev_deg = 0.0;
  double c = 0.0;
  uint32_t color = 0;
};

static inline double ParseRational(std::string_view s) {
  if (s.empty()) return 0.0;
  size_t slash = s.find('/');
  if (slash == std::string_view::npos) {
    return std::stod(std::string(s));
  }
  double num = std::stod(std::string(s.substr(0, slash)));
  double den = std::stod(std::string(s.substr(slash + 1)));
  if (den == 0.0) return 0.0;
  return num / den;
}

static inline uint32_t ColorForMargin(double c) {
  if (c <= 0.0) return 0xC8102EFF;
  double log_min = std::log10(C_MIN);
  double log_max = std::log10(C_MAX);
  double tau = (std::log10(c) - log_min) / (log_max - log_min);
  tau = std::clamp(tau, 0.0, 1.0);
  return ColorUtil::LinearGradient32(MARGIN_RAMP, (float)tau);
}

// Sub-pixel safe triangle rasterizer.
static void DrawTriangle(ImageRGBA *img,
                         int x0, int y0,
                         int x1, int y1,
                         int x2, int y2,
                         uint32_t color) {
  int minx = std::min({x0, x1, x2});
  int maxx = std::max({x0, x1, x2});
  int miny = std::min({y0, y1, y2});
  int maxy = std::max({y0, y1, y2});

  if (minx > img->Width() - 1 || maxx < 0 ||
      miny > img->Height() - 1 || maxy < 0) {
    return;
  }

  // Handle single-pixel or collapsed triangles cleanly to prevent holes.
  if (minx == maxx && miny == maxy) {
    img->BlendPixel32(minx, miny, color);
    return;
  }

  const int a = x1 - x0;
  const int b = y1 - y0;
  const int c = x2 - x0;
  const int d = y2 - y0;
  const int det = a * d - b * c;

  if (det == 0) {
    img->BlendLine32(x0, y0, x1, y1, color);
    img->BlendLine32(x1, y1, x2, y2, color);
    img->BlendLine32(x2, y2, x0, y0, color);
    return;
  }

  img->BlendTriangle32(x0, y0, x1, y1, x2, y2, color);
}

// Draw a filled rectangular card with a thin border.
static void DrawCard(ImageRGBA *img, int x, int y, int w, int h,
                     uint32_t fill_color, uint32_t border_color) {
  img->BlendRect32(x, y, w, h, fill_color);
  img->BlendBox32(x, y, w, h, border_color, border_color);
}

static inline int GetIntOrString(const rapidjson::Value &v) {
  if (v.IsInt()) return v.GetInt();
  if (v.IsString()) return std::atoi(v.GetString());
  if (v.IsInt64()) return static_cast<int>(v.GetInt64());
  if (v.IsUint()) return static_cast<int>(v.GetUint());
  if (v.IsUint64()) return static_cast<int>(v.GetUint64());
  return 0;
}

// Load a local-view JSON file via RapidJSON (supports monolithic and chunked JSON).
static bool LoadChildJson(const std::string &path,
                          std::vector<LeafTriangle> *leaves,
                          int *total_rows_out,
                          int *split_rows_out) {
  std::string content = Util::ReadFile(path);
  if (content.empty()) {
    return false;
  }

  rapidjson::Document doc;
  doc.Parse(content.c_str());
  if (doc.HasParseError()) {
    Printf("RapidJSON parse error in %s: %s (offset %zu)\n",
           path.c_str(),
           rapidjson::GetParseError_En(doc.GetParseError()),
           doc.GetErrorOffset());
    return false;
  }

  int total_rows = 0;
  int split_rows = 0;
  int local_leaves = 0;

  auto process_row = [&](const rapidjson::Value &row, int fallback_id) {
    if (row.IsNull() || !row.IsObject()) return;
    total_rows++;

    if (!row.HasMember("kind")) return;
    std::string_view kind = row["kind"].GetString();

    if (kind == "view_split") {
      split_rows++;
    } else if (kind == "view_local") {
      if (!row.HasMember("triangle") || !row.HasMember("c")) return;
      const auto &tri_val = row["triangle"];
      if (!tri_val.IsArray() || tri_val.Size() != 3) return;

      LeafTriangle leaf;
      leaf.id = row.HasMember("id") ? GetIntOrString(row["id"]) : fallback_id;
      leaf.root = row.HasMember("root") ? GetIntOrString(row["root"]) : 0;
      leaf.depth = row.HasMember("depth") ? GetIntOrString(row["depth"]) : 0;
      leaf.c = ParseRational(row["c"].GetString());
      leaf.color = ColorForMargin(leaf.c);

      for (int v = 0; v < 3; v++) {
        const auto &corner = tri_val[v];
        if (!corner.IsArray() || corner.Size() != 3) continue;
        leaf.p[v].x = ParseRational(corner[0].GetString());
        leaf.p[v].y = ParseRational(corner[1].GetString());
        leaf.p[v].z = ParseRational(corner[2].GetString());

        double len = std::sqrt(leaf.p[v].x * leaf.p[v].x +
                               leaf.p[v].y * leaf.p[v].y +
                               leaf.p[v].z * leaf.p[v].z);
        if (len > 0.0) {
          leaf.unit_v[v] = leaf.p[v] / len;
        } else {
          leaf.unit_v[v] = vec3{1, 0, 0};
        }

        double phi_rad = std::atan2(leaf.unit_v[v].y, leaf.unit_v[v].x);
        if (phi_rad < 0.0) phi_rad += 2.0 * std::numbers::pi;
        leaf.phi[v] = phi_rad * 180.0 / std::numbers::pi;

        double clamped_z = std::clamp(leaf.unit_v[v].z, -1.0, 1.0);
        double elev_rad = std::asin(clamped_z);
        leaf.elev[v] = elev_rad * 180.0 / std::numbers::pi;
        leaf.z[v] = clamped_z;
      }

      leaves->push_back(leaf);
      local_leaves++;
    }
  };

  if (doc.HasMember("chunks") && doc["chunks"].IsArray()) {
    std::filesystem::path base_dir = std::filesystem::path(path).parent_path();
    for (const auto &chunk_val : doc["chunks"].GetArray()) {
      if (!chunk_val.IsString()) continue;
      std::string chunk_file = (base_dir / chunk_val.GetString()).string();
      std::string chunk_content = Util::ReadFile(chunk_file);
      if (chunk_content.empty()) continue;
      rapidjson::Document chunk_doc;
      chunk_doc.Parse(chunk_content.c_str());
      if (chunk_doc.HasParseError() || !chunk_doc.IsArray()) continue;
      for (rapidjson::SizeType i = 0; i < chunk_doc.Size(); i++) {
        process_row(chunk_doc[i], total_rows);
      }
    }
  } else if (doc.HasMember("rows") && doc["rows"].IsArray()) {
    const auto &rows = doc["rows"];
    for (rapidjson::SizeType i = 0; i < rows.Size(); i++) {
      process_row(rows[i], (int)i);
    }
  } else {
    return false;
  }

  if (total_rows_out) *total_rows_out += total_rows;
  if (split_rows_out) *split_rows_out += split_rows;
  Printf("  Loaded %s: %d total rows, %d splits, %d certified leaves\n",
         path.c_str(), total_rows, split_rows, local_leaves);
  return true;
}

// Load optional empirical samples CSV file.
static std::vector<EmpiricalSample> LoadEmpiricalSamples(const std::string &path) {
  std::vector<EmpiricalSample> samples;
  std::ifstream f(path);
  if (!f.is_open()) return samples;

  std::string line;
  bool first_line = true;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (first_line && line.find("azimuth") != std::string::npos) {
      first_line = false;
      continue;
    }
    first_line = false;

    // Parse comma or space separated numbers
    for (char &c : line) {
      if (c == ',') c = ' ';
    }
    std::stringstream ss(line);
    double vx, vy, vz, phi, elev, c_val;
    if (ss >> vx >> vy >> vz >> phi >> elev >> c_val) {
      EmpiricalSample samp;
      samp.v = vec3{vx, vy, vz};
      samp.phi_deg = phi;
      samp.elev_deg = elev;
      samp.c = c_val;
      samp.color = ColorForMargin(c_val);
      samples.push_back(samp);
    }
  }
  Printf("Loaded %zu empirical samples from %s\n", samples.size(), path.c_str());
  return samples;
}

// Crisp outlined 2x text helper for 4K UHD display.
static void BlendTextOutline2x32(ImageRGBA *img, int x, int y,
                                 uint32_t outline_color,
                                 uint32_t fg_color,
                                 std::string_view s) {
  for (int dy = -2; dy <= 2; dy += 2) {
    for (int dx = -2; dx <= 2; dx += 2) {
      if (dx != 0 || dy != 0) {
        img->BlendText2x32(x + dx, y + dy, outline_color, s);
      }
    }
  }
  img->BlendText2x32(x, y, fg_color, s);
}

// Render vertical color bar legend scaled for 4K display.
static void RenderColorbar(ImageRGBA *img, int bar_x, int bar_y, int bar_w, int bar_h) {
  // Gradient bar fill
  for (int y = 0; y < bar_h; y++) {
    // Top is C_MAX (blue), bottom is C_MIN (crimson)
    double tau = 1.0 - (double)y / (double)(bar_h - 1);
    uint32_t color = ColorUtil::LinearGradient32(MARGIN_RAMP, (float)tau);
    for (int x = 0; x < bar_w; x++) {
      img->SetPixel32(bar_x + x, bar_y + y, color);
    }
  }
  // Border (2px)
  img->BlendBox32(bar_x, bar_y, bar_w, bar_h, 0xFFFFFFFF, 0xFFFFFFFF);
  img->BlendBox32(bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, 0xFFFFFFFF, 0xFFFFFFFF);

  // Tick marks and values
  static constexpr double TICKS[] = {
    0.00020, 0.0002278, 0.0002509, 0.00050, 0.00100, 0.00200, 0.00500, 0.01000
  };
  static constexpr const char *TICK_LABELS[] = {
    "0.00020 (cut)",
    "0.00023 (Canyon 2 pinch: c=0.0002278)",
    "0.00025 (Canyon 1 notch: c=0.0002509)",
    "0.00050",
    "0.00100",
    "0.00200",
    "0.00500",
    "0.01000 (wide margin)",
  };

  double log_min = std::log10(C_MIN);
  double log_max = std::log10(C_MAX);

  for (size_t i = 0; i < std::size(TICKS); i++) {
    double t = (std::log10(TICKS[i]) - log_min) / (log_max - log_min);
    t = std::clamp(t, 0.0, 1.0);
    int ty = bar_y + (int)std::round((1.0 - t) * (bar_h - 1));

    // Tick lines (2px thick)
    img->BlendLine32(bar_x + bar_w, ty, bar_x + bar_w + 14, ty, 0xFFFFFFFF);
    img->BlendLine32(bar_x + bar_w, ty + 1, bar_x + bar_w + 14, ty + 1, 0xFFFFFFFF);

    // Label text
    uint32_t fg = 0xEEEEEEFF;
    if (i == 1) fg = 0xFFD700FF; // Canyon 2
    if (i == 2) fg = 0xFF5588FF; // Canyon 1
    BlendTextOutline2x32(img, bar_x + bar_w + 20, ty - 8,
                         0x000000FF, fg, TICK_LABELS[i]);
  }

  // Label at top
  BlendTextOutline2x32(img, bar_x - 10, bar_y - 32, 0x000000FF, 0xFFFFFFFF,
                       "Margin c");
}

// ----------------------------------------------------------------------------
// VIEW 1: Equirectangular Projection of Upper Wedge (phi in [0, 72], alpha in [0, 90])
// ----------------------------------------------------------------------------
static void RenderWedgeEquirect(const std::vector<LeafTriangle> &leaves,
                                const std::vector<EmpiricalSample> &samples,
                                uint8_t line_alpha,
                                const std::string &outfile) {
  ImageRGBA base_img(WIDTH, HEIGHT);
  base_img.Clear32(0x101218FF); // Deep slate background

  // Header Title & Brief Stats
  BlendTextOutline2x32(&base_img, 60, 30, 0x000000FF, 0xFFFFFFFF,
                       "Nopert #229: Certified Margin Map (Equirectangular Wedge)");
  base_img.BlendText2x32(60, 68, 0x99AAB8FF,
                         "Infinitesimal rigidity margin c across C5 wedge: phi in [0, 72 deg], elev in [0, 90 deg]");
  base_img.BlendText2x32(60, 102, 0x00FFCCFF,
                         "Leaves: 64,855+ (depths 6..28) | Canyon 2 pinch: c = 0.0002278 | Proven tube radius: r = 0.00040");

  // Map viewport dimensions (aspect ratio 72/90 = 0.8)
  const int map_h = 1860;
  const int map_w = (int)std::round(map_h * (PHI_MAX_DEG / ELEV_MAX_DEG)); // 1488 px
  const int map_y = 150;
  // Position plot centrally with colorbar on the right
  const int map_x = 780;

  // Coordinate mapper
  auto MapToPixel = [&](double phi_deg, double elev_deg) -> std::pair<int, int> {
    double u = std::clamp(phi_deg / PHI_MAX_DEG, 0.0, 1.0);
    double v = std::clamp(elev_deg / ELEV_MAX_DEG, 0.0, 1.0);
    int px = map_x + (int)std::round(u * (map_w - 1));
    int py = map_y + (int)std::round((1.0 - v) * (map_h - 1));
    return {px, py};
  };

  // Draw plot background
  base_img.BlendRect32(map_x, map_y, map_w, map_h, 0x161922FF);

  // Coordinate grid lines in background
  for (int phi = 0; phi <= 72; phi += 10) {
    auto [gx, _] = MapToPixel((double)phi, 0.0);
    base_img.BlendLine32(gx, map_y, gx, map_y + map_h - 1, 0xFFFFFF18);
    base_img.BlendLine32(gx, map_y + map_h, gx, map_y + map_h + 8, 0xAAAAAAFF);
    std::string s = std::format("{}'", phi);
    BlendTextOutline2x32(&base_img, gx - 16, map_y + map_h + 12, 0x000000FF, 0xCCCCCCFF, s);
  }
  // 72 C5 boundary
  auto [b_x, _] = MapToPixel(72.0, 0.0);
  base_img.BlendLine32(b_x, map_y, b_x, map_y + map_h - 1, 0xFF444480);
  base_img.BlendLine32(b_x + 1, map_y, b_x + 1, map_y + map_h - 1, 0xFF444480);
  BlendTextOutline2x32(&base_img, b_x - 32, map_y + map_h + 40, 0x000000FF, 0xFF8888FF, "72'(C5)");

  for (int elev = 0; elev <= 90; elev += 10) {
    auto [_, gy] = MapToPixel(0.0, (double)elev);
    base_img.BlendLine32(map_x, gy, map_x + map_w - 1, gy, 0xFFFFFF18);
    base_img.BlendLine32(map_x - 8, gy, map_x, gy, 0xAAAAAAFF);
    std::string s = std::format("{:>2}'", elev);
    BlendTextOutline2x32(&base_img, map_x - 60, gy - 8, 0x000000FF, 0xCCCCCCFF, s);
  }

  // Draw Canyon guide lines on the background (BEFORE triangle patches)
  // Canyon 1: phi = 2.22 deg, elev = 9.63 deg (Pink / Coral)
  auto [c1_x, c1_y] = MapToPixel(CANYON1.phi_deg, CANYON1.elev_deg);
  uint32_t c1_color = 0xFF3366FF;
  base_img.BlendLine32(c1_x, map_y, c1_x, map_y + map_h - 1, c1_color);
  base_img.BlendLine32(c1_x + 1, map_y, c1_x + 1, map_y + map_h - 1, c1_color);
  base_img.BlendLine32(map_x, c1_y, map_x + map_w - 1, c1_y, c1_color);
  base_img.BlendLine32(map_x, c1_y + 1, map_x + map_w - 1, c1_y + 1, c1_color);

  // Canyon 2: phi = 12.90 deg, elev = 18.48 deg (Gold / Amber)
  auto [c2_x, c2_y] = MapToPixel(CANYON2.phi_deg, CANYON2.elev_deg);
  uint32_t c2_color = 0xFFD700FF;
  base_img.BlendLine32(c2_x, map_y, c2_x, map_y + map_h - 1, c2_color);
  base_img.BlendLine32(c2_x + 1, map_y, c2_x + 1, map_y + map_h - 1, c2_color);
  base_img.BlendLine32(map_x, c2_y, map_x + map_w - 1, c2_y, c2_color);
  base_img.BlendLine32(map_x, c2_y + 1, map_x + map_w - 1, c2_y + 1, c2_color);

  // Axis ticks & crop marks for C1 and C2
  // C1 bottom tick & label
  base_img.BlendLine32(c1_x, map_y + map_h, c1_x, map_y + map_h + 14, c1_color);
  base_img.BlendLine32(c1_x + 1, map_y + map_h, c1_x + 1, map_y + map_h + 14, c1_color);
  BlendTextOutline2x32(&base_img, c1_x - 50, map_y + map_h + 40, 0x000000FF, c1_color, "C1:2.22'");
  // C1 left tick & label (offset further left to avoid 10' grid label)
  base_img.BlendLine32(map_x - 14, c1_y, map_x, c1_y, c1_color);
  base_img.BlendLine32(map_x - 14, c1_y + 1, map_x, c1_y + 1, c1_color);
  BlendTextOutline2x32(&base_img, map_x - 245, c1_y - 8, 0x000000FF, c1_color, "C1: 9.63'");
  // C1 top and right crop marks
  base_img.BlendLine32(c1_x, map_y - 12, c1_x, map_y, c1_color);
  base_img.BlendLine32(map_x + map_w, c1_y, map_x + map_w + 12, c1_y, c1_color);

  // C2 bottom tick & label
  base_img.BlendLine32(c2_x, map_y + map_h, c2_x, map_y + map_h + 14, c2_color);
  base_img.BlendLine32(c2_x + 1, map_y + map_h, c2_x + 1, map_y + map_h + 14, c2_color);
  BlendTextOutline2x32(&base_img, c2_x - 20, map_y + map_h + 70, 0x000000FF, c2_color, "C2:12.90'");
  // C2 left tick & label (offset further left to avoid 20' grid label)
  base_img.BlendLine32(map_x - 14, c2_y, map_x, c2_y, c2_color);
  base_img.BlendLine32(map_x - 14, c2_y + 1, map_x, c2_y + 1, c2_color);
  BlendTextOutline2x32(&base_img, map_x - 260, c2_y - 8, 0x000000FF, c2_color, "C2: 18.48'");
  // C2 top and right crop marks
  base_img.BlendLine32(c2_x, map_y - 12, c2_x, map_y, c2_color);
  base_img.BlendLine32(map_x + map_w, c2_y, map_x + map_w + 12, c2_y, c2_color);

  // Axis Labels
  BlendTextOutline2x32(&base_img, map_x + map_w / 2 - 120, map_y + map_h + 105,
                       0x000000FF, 0xFFFFFFFF, "Azimuth phi (degrees)");
  BlendTextOutline2x32(&base_img, map_x - 120, map_y - 25,
                       0x000000FF, 0xFFFFFFFF, "Elevation alpha");

  // Rasterize leaf triangles over background lines
  ImageRGBA wireframe(WIDTH, HEIGHT);
  wireframe.Clear32(0x00000000);

  for (const auto &leaf : leaves) {
    auto [x0, y0] = MapToPixel(leaf.phi[0], leaf.elev[0]);
    auto [x1, y1] = MapToPixel(leaf.phi[1], leaf.elev[1]);
    auto [x2, y2] = MapToPixel(leaf.phi[2], leaf.elev[2]);

    DrawTriangle(&base_img, x0, y0, x1, y1, x2, y2, leaf.color);

    if (!(x0 == x1 && x1 == x2 && y0 == y1 && y1 == y2)) {
      wireframe.BlendLine32(x0, y0, x1, y1, 0x000000FF);
      wireframe.BlendLine32(x1, y1, x2, y2, 0x000000FF);
      wireframe.BlendLine32(x2, y2, x0, y0, 0x000000FF);
    }
  }

  // Reduce alpha of wireframe and composite
  for (uint32_t &p : wireframe.data()) {
    if ((p & 0xFF) != 0) {
      p = (p & 0xFFFFFF00) | line_alpha;
    }
  }
  base_img.BlendImage(0, 0, wireframe);

  // Overlay empirical samples if any
  for (const auto &samp : samples) {
    auto [sx, sy] = MapToPixel(samp.phi_deg, samp.elev_deg);
    base_img.BlendFilledCircleAA32((float)sx, (float)sy, 5.0f, samp.color);
    base_img.BlendCircle32(sx, sy, 6, 0x000000FF);
  }

  // Re-draw outer frame to stay sharp
  base_img.BlendBox32(map_x, map_y, map_w, map_h, 0xFFFFFFFF, 0xFFFFFFFF);

  // Vertical Colorbar
  RenderColorbar(&base_img, map_x + map_w + 100, map_y, 44, map_h);

  base_img.Save(outfile);
  Printf("Saved %s (%dx%d)\n", outfile.c_str(), WIDTH, HEIGHT);
}

// ----------------------------------------------------------------------------
// VIEW 2: Lambert Cylindrical Equal-Area Projection (phi in [0, 72], z in [0, 1])
// ----------------------------------------------------------------------------
static void RenderWedgeLambert(const std::vector<LeafTriangle> &leaves,
                               const std::vector<EmpiricalSample> &samples,
                               uint8_t line_alpha,
                               const std::string &outfile) {
  ImageRGBA base_img(WIDTH, HEIGHT);
  base_img.Clear32(0x101218FF);

  // Header Title & Brief Stats
  BlendTextOutline2x32(&base_img, 60, 30, 0x000000FF, 0xFFFFFFFF,
                       "Nopert #229: Certified Margin Map (Lambert Equal-Area)");
  base_img.BlendText2x32(60, 68, 0x99AAB8FF,
                         "Lambert cylindrical equal-area preserves solid angle on unit sphere S^2 (phi in [0, 72 deg], z in [0, 1])");
  base_img.BlendText2x32(60, 102, 0x00FFCCFF,
                         "Pixel area is proportional to solid angle | Canyon 2 pinch: c = 0.0002278 | Proven tube radius: r = 0.00040");

  // Natural equal-area aspect ratio: (2*pi/5) / 1.0 = 1.256637
  const int map_h = 1860;
  const int map_w = (int)std::round(map_h * (PHI_MAX_RAD / 1.0)); // 2337 px
  const int map_y = 150;
  const int map_x = 420;

  auto MapToPixel = [&](double phi_deg, double z_val) -> std::pair<int, int> {
    double u = std::clamp(phi_deg / PHI_MAX_DEG, 0.0, 1.0);
    double v = std::clamp(z_val, 0.0, 1.0);
    int px = map_x + (int)std::round(u * (map_w - 1));
    int py = map_y + (int)std::round((1.0 - v) * (map_h - 1));
    return {px, py};
  };

  // Draw plot background
  base_img.BlendRect32(map_x, map_y, map_w, map_h, 0x161922FF);

  // Coordinate grid lines in background
  for (int phi = 0; phi <= 72; phi += 10) {
    auto [gx, _] = MapToPixel((double)phi, 0.0);
    base_img.BlendLine32(gx, map_y, gx, map_y + map_h - 1, 0xFFFFFF18);
    base_img.BlendLine32(gx, map_y + map_h, gx, map_y + map_h + 8, 0xAAAAAAFF);
    std::string s = std::format("{}'", phi);
    BlendTextOutline2x32(&base_img, gx - 16, map_y + map_h + 12, 0x000000FF, 0xCCCCCCFF, s);
  }
  // 72 C5 boundary
  auto [b_x, _] = MapToPixel(72.0, 0.0);
  base_img.BlendLine32(b_x, map_y, b_x, map_y + map_h - 1, 0xFF444480);
  base_img.BlendLine32(b_x + 1, map_y, b_x + 1, map_y + map_h - 1, 0xFF444480);
  BlendTextOutline2x32(&base_img, b_x - 32, map_y + map_h + 40, 0x000000FF, 0xFF8888FF, "72'(C5)");

  for (int zi = 0; zi <= 10; zi++) {
    double z_val = zi / 10.0;
    auto [_, gy] = MapToPixel(0.0, z_val);
    base_img.BlendLine32(map_x, gy, map_x + map_w - 1, gy, 0xFFFFFF18);
    base_img.BlendLine32(map_x - 8, gy, map_x, gy, 0xAAAAAAFF);
    std::string s = std::format("{:.1f}", z_val);
    BlendTextOutline2x32(&base_img, map_x - 60, gy - 8, 0x000000FF, 0xCCCCCCFF, s);
  }

  // Draw Canyon guide lines on the background (BEFORE triangle patches)
  // Canyon 1: phi = 2.22 deg, z = 0.16734
  auto [c1_x, c1_y] = MapToPixel(CANYON1.phi_deg, CANYON1.v.z);
  uint32_t c1_color = 0xFF3366FF;
  base_img.BlendLine32(c1_x, map_y, c1_x, map_y + map_h - 1, c1_color);
  base_img.BlendLine32(c1_x + 1, map_y, c1_x + 1, map_y + map_h - 1, c1_color);
  base_img.BlendLine32(map_x, c1_y, map_x + map_w - 1, c1_y, c1_color);
  base_img.BlendLine32(map_x, c1_y + 1, map_x + map_w - 1, c1_y + 1, c1_color);

  // Canyon 2: phi = 12.90 deg, z = 0.31694
  auto [c2_x, c2_y] = MapToPixel(CANYON2.phi_deg, CANYON2.v.z);
  uint32_t c2_color = 0xFFD700FF;
  base_img.BlendLine32(c2_x, map_y, c2_x, map_y + map_h - 1, c2_color);
  base_img.BlendLine32(c2_x + 1, map_y, c2_x + 1, map_y + map_h - 1, c2_color);
  base_img.BlendLine32(map_x, c2_y, map_x + map_w - 1, c2_y, c2_color);
  base_img.BlendLine32(map_x, c2_y + 1, map_x + map_w - 1, c2_y + 1, c2_color);

  // Axis ticks & crop marks for C1 and C2
  // C1 bottom tick & label
  base_img.BlendLine32(c1_x, map_y + map_h, c1_x, map_y + map_h + 14, c1_color);
  base_img.BlendLine32(c1_x + 1, map_y + map_h, c1_x + 1, map_y + map_h + 14, c1_color);
  BlendTextOutline2x32(&base_img, c1_x - 50, map_y + map_h + 40, 0x000000FF, c1_color, "C1:2.22'");
  // C1 left tick & label (offset further left to avoid z=0.2 grid label)
  base_img.BlendLine32(map_x - 14, c1_y, map_x, c1_y, c1_color);
  base_img.BlendLine32(map_x - 14, c1_y + 1, map_x, c1_y + 1, c1_color);
  BlendTextOutline2x32(&base_img, map_x - 270, c1_y - 8, 0x000000FF, c1_color, "C1: z=0.167");
  // C1 top and right crop marks
  base_img.BlendLine32(c1_x, map_y - 12, c1_x, map_y, c1_color);
  base_img.BlendLine32(map_x + map_w, c1_y, map_x + map_w + 12, c1_y, c1_color);

  // C2 bottom tick & label
  base_img.BlendLine32(c2_x, map_y + map_h, c2_x, map_y + map_h + 14, c2_color);
  base_img.BlendLine32(c2_x + 1, map_y + map_h, c2_x + 1, map_y + map_h + 14, c2_color);
  BlendTextOutline2x32(&base_img, c2_x - 20, map_y + map_h + 70, 0x000000FF, c2_color, "C2:12.90'");
  // C2 left tick & label (offset further left to avoid z=0.3 grid label)
  base_img.BlendLine32(map_x - 14, c2_y, map_x, c2_y, c2_color);
  base_img.BlendLine32(map_x - 14, c2_y + 1, map_x, c2_y + 1, c2_color);
  BlendTextOutline2x32(&base_img, map_x - 270, c2_y - 8, 0x000000FF, c2_color, "C2: z=0.317");
  // C2 top and right crop marks
  base_img.BlendLine32(c2_x, map_y - 12, c2_x, map_y, c2_color);
  base_img.BlendLine32(map_x + map_w, c2_y, map_x + map_w + 12, c2_y, c2_color);

  // Axis Labels
  BlendTextOutline2x32(&base_img, map_x + map_w / 2 - 120, map_y + map_h + 105,
                       0x000000FF, 0xFFFFFFFF, "Azimuth phi (degrees)");
  BlendTextOutline2x32(&base_img, map_x - 170, map_y - 25,
                       0x000000FF, 0xFFFFFFFF, "z = sin(elevation)");

  // Rasterize leaf triangles over background lines
  ImageRGBA wireframe(WIDTH, HEIGHT);
  wireframe.Clear32(0x00000000);

  for (const auto &leaf : leaves) {
    auto [x0, y0] = MapToPixel(leaf.phi[0], leaf.z[0]);
    auto [x1, y1] = MapToPixel(leaf.phi[1], leaf.z[1]);
    auto [x2, y2] = MapToPixel(leaf.phi[2], leaf.z[2]);

    DrawTriangle(&base_img, x0, y0, x1, y1, x2, y2, leaf.color);

    if (!(x0 == x1 && x1 == x2 && y0 == y1 && y1 == y2)) {
      wireframe.BlendLine32(x0, y0, x1, y1, 0x000000FF);
      wireframe.BlendLine32(x1, y1, x2, y2, 0x000000FF);
      wireframe.BlendLine32(x2, y2, x0, y0, 0x000000FF);
    }
  }

  for (uint32_t &p : wireframe.data()) {
    if ((p & 0xFF) != 0) {
      p = (p & 0xFFFFFF00) | line_alpha;
    }
  }
  base_img.BlendImage(0, 0, wireframe);

  // Overlay empirical samples if any
  for (const auto &samp : samples) {
    auto [sx, sy] = MapToPixel(samp.phi_deg, samp.v.z);
    base_img.BlendFilledCircleAA32((float)sx, (float)sy, 5.0f, samp.color);
    base_img.BlendCircle32(sx, sy, 6, 0x000000FF);
  }

  // Re-draw outer frame
  base_img.BlendBox32(map_x, map_y, map_w, map_h, 0xFFFFFFFF, 0xFFFFFFFF);

  // Vertical Colorbar
  RenderColorbar(&base_img, map_x + map_w + 80, map_y, 44, map_h);

  base_img.Save(outfile);
  Printf("Saved %s (%dx%d)\n", outfile.c_str(), WIDTH, HEIGHT);
}

// ----------------------------------------------------------------------------
// VIEW 3: Full Sphere Equirectangular (Replicated C5 orbits and z-reflection)
// ----------------------------------------------------------------------------
static void RenderFullSphere(const std::vector<LeafTriangle> &leaves,
                             uint8_t line_alpha,
                             const std::string &outfile) {
  ImageRGBA base_img(WIDTH, HEIGHT);
  base_img.Clear32(0x101218FF);

  // Header Title & Subtitle
  BlendTextOutline2x32(&base_img, 60, 30, 0x000000FF, 0xFFFFFFFF,
                       "Nopert #229: Full View Sphere Clearance Map (360 x 180 deg)");
  base_img.BlendText2x32(60, 68, 0x99AAB8FF,
                         "Replicated across fivefold rotational symmetry (C5) and elevation reflection (360 deg azimuth x [-90 deg, +90 deg] elevation)");

  // 2:1 aspect ratio: map_w = 3520, map_h = 1760
  const int map_w = 3520;
  const int map_h = 1760;
  const int map_x = (WIDTH - map_w) / 2; // 160 px
  const int map_y = 120;

  auto MapFullPixel = [&](double phi_deg, double elev_deg) -> std::pair<int, int> {
    double u = std::clamp(phi_deg / 360.0, 0.0, 1.0);
    double v = std::clamp((elev_deg + 90.0) / 180.0, 0.0, 1.0);
    int px = map_x + (int)std::round(u * (map_w - 1));
    int py = map_y + (int)std::round((1.0 - v) * (map_h - 1));
    return {px, py};
  };

  // Draw plot background
  base_img.BlendRect32(map_x, map_y, map_w, map_h, 0x161922FF);

  // Sector boundaries (72 deg intervals)
  for (int phi = 0; phi <= 360; phi += 72) {
    auto [gx, _] = MapFullPixel((double)phi, 0.0);
    base_img.BlendLine32(gx, map_y, gx, map_y + map_h - 1, 0xFFFFFF28);
    base_img.BlendLine32(gx, map_y + map_h, gx, map_y + map_h + 8, 0xAAAAAAFF);
    std::string s = std::format("{}'", phi);
    BlendTextOutline2x32(&base_img, gx - 20, map_y + map_h + 12, 0x000000FF, 0xCCCCCCFF, s);
  }

  // Elevation latitude circles
  for (int elev = -90; elev <= 90; elev += 30) {
    auto [_, gy] = MapFullPixel(0.0, (double)elev);
    base_img.BlendLine32(map_x, gy, map_x + map_w - 1, gy, 0xFFFFFF20);
    base_img.BlendLine32(map_x - 8, gy, map_x, gy, 0xAAAAAAFF);
    std::string s = std::format("{:+d}'", elev);
    BlendTextOutline2x32(&base_img, map_x - 65, gy - 8, 0x000000FF, 0xCCCCCCFF, s);
  }

  // Equator emphasis line
  auto [_, eq_y] = MapFullPixel(0.0, 0.0);
  base_img.BlendLine32(map_x, eq_y, map_x + map_w - 1, eq_y, 0xFFFFFF50);

  // Background guide lines for all 10 symmetric images of Canyon 1 & Canyon 2
  uint32_t c1_color = 0xFF3366AA;
  uint32_t c2_color = 0xFFD700AA;

  for (int k = 0; k < 5; k++) {
    double sec_offset = k * 72.0;
    auto [c1_x, _1] = MapFullPixel(sec_offset + CANYON1.phi_deg, 0.0);
    base_img.BlendLine32(c1_x, map_y, c1_x, map_y + map_h - 1, c1_color);
    auto [c2_x, _2] = MapFullPixel(sec_offset + CANYON2.phi_deg, 0.0);
    base_img.BlendLine32(c2_x, map_y, c2_x, map_y + map_h - 1, c2_color);
  }
  for (double sign : {1.0, -1.0}) {
    auto [_3, c1_y] = MapFullPixel(0.0, sign * CANYON1.elev_deg);
    base_img.BlendLine32(map_x, c1_y, map_x + map_w - 1, c1_y, c1_color);
    auto [_4, c2_y] = MapFullPixel(0.0, sign * CANYON2.elev_deg);
    base_img.BlendLine32(map_x, c2_y, map_x + map_w - 1, c2_y, c2_color);
  }

  // Rasterize leaf triangles replicated across 5 sectors and 2 hemispheres
  ImageRGBA wireframe(WIDTH, HEIGHT);
  wireframe.Clear32(0x00000000);

  for (int k = 0; k < 5; k++) {
    double sector_offset = k * 72.0;

    for (int hem = 0; hem < 2; hem++) {
      double elev_mult = (hem == 0) ? 1.0 : -1.0;

      for (const auto &leaf : leaves) {
        auto [x0, y0] = MapFullPixel(leaf.phi[0] + sector_offset, leaf.elev[0] * elev_mult);
        auto [x1, y1] = MapFullPixel(leaf.phi[1] + sector_offset, leaf.elev[1] * elev_mult);
        auto [x2, y2] = MapFullPixel(leaf.phi[2] + sector_offset, leaf.elev[2] * elev_mult);

        DrawTriangle(&base_img, x0, y0, x1, y1, x2, y2, leaf.color);

        if (!(x0 == x1 && x1 == x2 && y0 == y1 && y1 == y2)) {
          wireframe.BlendLine32(x0, y0, x1, y1, 0x000000FF);
          wireframe.BlendLine32(x1, y1, x2, y2, 0x000000FF);
          wireframe.BlendLine32(x2, y2, x0, y0, 0x000000FF);
        }
      }
    }
  }

  for (uint32_t &p : wireframe.data()) {
    if ((p & 0xFF) != 0) {
      p = (p & 0xFFFFFF00) | line_alpha;
    }
  }
  base_img.BlendImage(0, 0, wireframe);

  // Border
  base_img.BlendBox32(map_x, map_y, map_w, map_h, 0xFFFFFFFF, 0xFFFFFFFF);

  // Horizontal Colorbar Legend along bottom
  int cbar_x = 800;
  int cbar_y = 1970;
  int cbar_w = 2240;
  int cbar_h = 30;

  for (int x = 0; x < cbar_w; x++) {
    double tau = (double)x / (double)(cbar_w - 1);
    uint32_t color = ColorUtil::LinearGradient32(MARGIN_RAMP, (float)tau);
    for (int y = 0; y < cbar_h; y++) {
      base_img.SetPixel32(cbar_x + x, cbar_y + y, color);
    }
  }
  base_img.BlendBox32(cbar_x, cbar_y, cbar_w, cbar_h, 0xFFFFFFFF, 0xFFFFFFFF);

  double log_min = std::log10(C_MIN);
  double log_max = std::log10(C_MAX);

  // Label before the colorbar
  BlendTextOutline2x32(&base_img, cbar_x - 300, cbar_y + 4, 0x000000FF, 0xFFFFFFFF, "Margin c:");
  BlendTextOutline2x32(&base_img, cbar_x - 140, cbar_y + 4, 0x000000FF, 0xFFFFFFFF, "0.00020");

  // Standard logarithmic ticks along the bottom colorbar
  static constexpr double STD_TICKS[] = {0.00050, 0.00100, 0.00200, 0.00500, 0.01000};
  static constexpr const char *STD_LABS[] = {
    "0.00050",
    "0.00100",
    "0.00200",
    "0.00500",
    "0.01000 (wide)"
  };

  for (size_t i = 0; i < std::size(STD_TICKS); i++) {
    double tau = (std::log10(STD_TICKS[i]) - log_min) / (log_max - log_min);
    int tx = cbar_x + (int)std::round(tau * (cbar_w - 1));
    if (i % 2 == 0) {
      base_img.BlendLine32(tx, cbar_y + cbar_h, tx, cbar_y + cbar_h + 8, 0xFFFFFFFF);
      base_img.BlendLine32(tx + 1, cbar_y + cbar_h, tx + 1, cbar_y + cbar_h + 8, 0xFFFFFFFF);
      int offset_x = (i == std::size(STD_TICKS) - 1) ? 110 : 35;
      BlendTextOutline2x32(&base_img, tx - offset_x, cbar_y + cbar_h + 12, 0x000000FF, 0xEEEEEEFF, STD_LABS[i]);
    } else {
      base_img.BlendLine32(tx, cbar_y - 8, tx, cbar_y, 0xFFFFFFFF);
      base_img.BlendLine32(tx + 1, cbar_y - 8, tx + 1, cbar_y, 0xFFFFFFFF);
      BlendTextOutline2x32(&base_img, tx - 35, cbar_y - 28, 0x000000FF, 0xEEEEEEFF, STD_LABS[i]);
    }
  }

  // Canyon 2 tick (above bar, yellow)
  {
    double tau = (std::log10(CANYON2.c) - log_min) / (log_max - log_min);
    int tx = cbar_x + (int)std::round(tau * (cbar_w - 1));
    base_img.BlendLine32(tx, cbar_y - 12, tx, cbar_y, 0xFFD700FF);
    base_img.BlendLine32(tx + 1, cbar_y - 12, tx + 1, cbar_y, 0xFFD700FF);
    BlendTextOutline2x32(&base_img, tx - 20, cbar_y - 32, 0x000000FF, 0xFFD700FF,
                         "C2: c=0.0002278");
  }

  // Canyon 1 tick (below bar, pink)
  {
    double tau = (std::log10(CANYON1.c) - log_min) / (log_max - log_min);
    int tx = cbar_x + (int)std::round(tau * (cbar_w - 1));
    base_img.BlendLine32(tx, cbar_y + cbar_h, tx, cbar_y + cbar_h + 12, 0xFF3366FF);
    base_img.BlendLine32(tx + 1, cbar_y + cbar_h, tx + 1, cbar_y + cbar_h + 12, 0xFF3366FF);
    BlendTextOutline2x32(&base_img, tx - 20, cbar_y + cbar_h + 16, 0x000000FF, 0xFF3366FF,
                         "C1: c=0.0002509");
  }

  base_img.Save(outfile);
  Printf("Saved %s (%dx%d)\n", outfile.c_str(), WIDTH, HEIGHT);
}

// ----------------------------------------------------------------------------
// VIEW 4: Projective Plane Triangle View (Barycentric Subdivision)
// ----------------------------------------------------------------------------
static void RenderProjectivePlane(const std::vector<LeafTriangle> &leaves,
                                  uint8_t line_alpha,
                                  const std::string &outfile) {
  ImageRGBA base_img(WIDTH, HEIGHT);
  base_img.Clear32(0x101218FF);

  // Header Title & Brief Stats
  BlendTextOutline2x32(&base_img, 60, 30, 0x000000FF, 0xFFFFFFFF,
                       "Nopert #229: Projective Triangle Domain ('Nested Triforces')");
  base_img.BlendText2x32(60, 68, 0x99AAB8FF,
                         "Exact native geometry of recursive projective quadtree bisection on the plane x + y + z = 1");
  base_img.BlendText2x32(60, 102, 0x00FFCCFF,
                         "Straight lines from apex C2 are constant phi meridians; horizontal lines are constant z parallels");

  // Triangle corners on 4K screen:
  // C0 = (1, 0, 0) -> bottom-left
  // C1 = (10/41, 31/41, 0) -> bottom-right
  // C2 = (0, 0, 1) -> top-apex
  const vec2 P_C0{400.0, 1960.0};
  const vec2 P_C1{2720.0, 1960.0};
  const vec2 P_C2{1560.0, 200.0};

  auto ProjectiveToBarycentric = [&](const vec3 &p) -> vec2 {
    double c2 = p.z;
    double c1 = (41.0 / 31.0) * p.y;
    double c0 = p.x - (10.0 / 31.0) * p.y;
    double sum = c0 + c1 + c2;
    if (sum <= 0.0) sum = 1.0;
    double u0 = c0 / sum;
    double u1 = c1 / sum;
    double u2 = c2 / sum;

    double sx = u0 * P_C0.x + u1 * P_C1.x + u2 * P_C2.x;
    double sy = u0 * P_C0.y + u1 * P_C1.y + u2 * P_C2.y;
    return vec2{sx, sy};
  };

  // Draw plot background inside domain triangle
  DrawTriangle(&base_img,
               (int)P_C0.x, (int)P_C0.y,
               (int)P_C1.x, (int)P_C1.y,
               (int)P_C2.x, (int)P_C2.y,
               0x161922FF);

  // Background guide lines: meridian rays and parallel chords for Canyon 1 & Canyon 2
  // Canyon 1: phi = 2.22 deg, elev = 9.63 deg (pink / coral)
  uint32_t c1_color = 0xFF3366FF;
  vec2 c1_base = ProjectiveToBarycentric(vec3{std::cos(CANYON1.phi_rad), std::sin(CANYON1.phi_rad), 0.0});
  base_img.BlendLine32((int)P_C2.x, (int)P_C2.y, (int)c1_base.x, (int)c1_base.y, c1_color);
  base_img.BlendLine32((int)P_C2.x + 1, (int)P_C2.y, (int)c1_base.x + 1, (int)c1_base.y, c1_color);

  // Canyon 1 horizontal projective z line passing through CANYON1.v
  double c1_proj_z = CANYON1.v.z / (CANYON1.v.x + CANYON1.v.y + CANYON1.v.z);
  vec2 c1_left = (1.0 - c1_proj_z) * P_C0 + c1_proj_z * P_C2;
  vec2 c1_right = (1.0 - c1_proj_z) * P_C1 + c1_proj_z * P_C2;
  base_img.BlendLine32((int)c1_left.x, (int)c1_left.y, (int)c1_right.x, (int)c1_right.y, c1_color);
  base_img.BlendLine32((int)c1_left.x, (int)c1_left.y + 1, (int)c1_right.x, (int)c1_right.y + 1, c1_color);

  // Canyon 2: phi = 12.90 deg, elev = 18.48 deg (gold)
  uint32_t c2_color = 0xFFD700FF;
  vec2 c2_base = ProjectiveToBarycentric(vec3{std::cos(CANYON2.phi_rad), std::sin(CANYON2.phi_rad), 0.0});
  base_img.BlendLine32((int)P_C2.x, (int)P_C2.y, (int)c2_base.x, (int)c2_base.y, c2_color);
  base_img.BlendLine32((int)P_C2.x + 1, (int)P_C2.y, (int)c2_base.x + 1, (int)c2_base.y, c2_color);

  // Canyon 2 horizontal projective z line passing through CANYON2.v
  double c2_proj_z = CANYON2.v.z / (CANYON2.v.x + CANYON2.v.y + CANYON2.v.z);
  vec2 c2_left = (1.0 - c2_proj_z) * P_C0 + c2_proj_z * P_C2;
  vec2 c2_right = (1.0 - c2_proj_z) * P_C1 + c2_proj_z * P_C2;
  base_img.BlendLine32((int)c2_left.x, (int)c2_left.y, (int)c2_right.x, (int)c2_right.y, c2_color);
  base_img.BlendLine32((int)c2_left.x, (int)c2_left.y + 1, (int)c2_right.x, (int)c2_right.y + 1, c2_color);

  // Guide line tick marks and labels at borders
  // C1 base tick & label
  base_img.BlendLine32((int)c1_base.x, (int)c1_base.y, (int)c1_base.x, (int)c1_base.y + 16, c1_color);
  BlendTextOutline2x32(&base_img, (int)c1_base.x - 45, (int)c1_base.y + 22, 0x000000FF, c1_color, "C1:2.22'");
  // C1 left edge tick & label
  base_img.BlendLine32((int)c1_left.x - 16, (int)c1_left.y, (int)c1_left.x, (int)c1_left.y, c1_color);
  BlendTextOutline2x32(&base_img, (int)c1_left.x - 225, (int)c1_left.y - 8, 0x000000FF, c1_color, "C1: z=0.167");

  // C2 base tick & label
  base_img.BlendLine32((int)c2_base.x, (int)c2_base.y, (int)c2_base.x, (int)c2_base.y + 16, c2_color);
  BlendTextOutline2x32(&base_img, (int)c2_base.x - 20, (int)c2_base.y + 48, 0x000000FF, c2_color, "C2:12.90'");
  // C2 left edge tick & label
  base_img.BlendLine32((int)c2_left.x - 16, (int)c2_left.y, (int)c2_left.x, (int)c2_left.y, c2_color);
  BlendTextOutline2x32(&base_img, (int)c2_left.x - 225, (int)c2_left.y - 8, 0x000000FF, c2_color, "C2: z=0.317");

  // Rasterize leaf triangles
  ImageRGBA wireframe(WIDTH, HEIGHT);
  wireframe.Clear32(0x00000000);

  for (const auto &leaf : leaves) {
    vec2 p0 = ProjectiveToBarycentric(leaf.p[0]);
    vec2 p1 = ProjectiveToBarycentric(leaf.p[1]);
    vec2 p2 = ProjectiveToBarycentric(leaf.p[2]);

    int x0 = (int)std::round(p0.x);
    int y0 = (int)std::round(p0.y);
    int x1 = (int)std::round(p1.x);
    int y1 = (int)std::round(p1.y);
    int x2 = (int)std::round(p2.x);
    int y2 = (int)std::round(p2.y);

    DrawTriangle(&base_img, x0, y0, x1, y1, x2, y2, leaf.color);

    if (!(x0 == x1 && x1 == x2 && y0 == y1 && y1 == y2)) {
      wireframe.BlendLine32(x0, y0, x1, y1, 0x000000FF);
      wireframe.BlendLine32(x1, y1, x2, y2, 0x000000FF);
      wireframe.BlendLine32(x2, y2, x0, y0, 0x000000FF);
    }
  }

  for (uint32_t &p : wireframe.data()) {
    if ((p & 0xFF) != 0) {
      p = (p & 0xFFFFFF00) | line_alpha;
    }
  }
  base_img.BlendImage(0, 0, wireframe);

  // Outer boundary outline (2px thick)
  auto DrawThickBoundary = [&](int xa, int ya, int xb, int yb) {
    base_img.BlendLine32(xa, ya, xb, yb, 0xFFFFFFFF);
    base_img.BlendLine32(xa + 1, ya, xb + 1, yb, 0xFFFFFFFF);
    base_img.BlendLine32(xa, ya + 1, xb, yb + 1, 0xFFFFFFFF);
  };
  DrawThickBoundary((int)P_C0.x, (int)P_C0.y, (int)P_C1.x, (int)P_C1.y);
  DrawThickBoundary((int)P_C1.x, (int)P_C1.y, (int)P_C2.x, (int)P_C2.y);
  DrawThickBoundary((int)P_C2.x, (int)P_C2.y, (int)P_C0.x, (int)P_C0.y);

  // Corner labels
  BlendTextOutline2x32(&base_img, (int)P_C0.x - 340, (int)P_C0.y + 12,
                       0x000000FF, 0xFFFFFFFF, "C0 = (1, 0, 0)");
  BlendTextOutline2x32(&base_img, (int)P_C0.x - 260, (int)P_C0.y + 44,
                       0x000000FF, 0xAAAAAAFF, "[phi=0]");
  BlendTextOutline2x32(&base_img, (int)P_C1.x + 16, (int)P_C1.y + 12,
                       0x000000FF, 0xFFFFFFFF, "C1 = (10/41, 31/41, 0) [72.1 deg]");
  BlendTextOutline2x32(&base_img, (int)P_C2.x - 120, (int)P_C2.y - 32,
                       0x000000FF, 0xFFFFFFFF, "C2 = (0, 0, 1) [Pole]");

  // Vertical Colorbar on the right
  RenderColorbar(&base_img, 2950, 200, 44, 1760);

  base_img.Save(outfile);
  Printf("Saved %s (%dx%d)\n", outfile.c_str(), WIDTH, HEIGHT);
}

// ----------------------------------------------------------------------------
// Main CLI Entrypoint
// ----------------------------------------------------------------------------
int main(int argc, char **argv) {
  std::string artifacts_dir = "";
  std::string samples_file = "";
  std::string output_prefix = "tube_";
  int child_filter = -1; // -1 = load all available children
  uint8_t line_alpha = 56; // 56/255 ≈ 22% opacity for wireframe overlay

  // Search candidate artifact directories
  const std::vector<std::string> CANDIDATES = {
    "/home/tom/nopert-project/Noperthedron/.artifacts/nopert229",
    "../Noperthedron/.artifacts/nopert229",
    "Noperthedron/.artifacts/nopert229",
    ".artifacts/nopert229",
  };

  for (const auto &cand : CANDIDATES) {
    if (std::filesystem::exists(cand)) {
      artifacts_dir = cand;
      break;
    }
  }

  // Parse CLI args
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--artifacts_dir" && i + 1 < argc) {
      artifacts_dir = argv[++i];
    } else if (arg == "--samples" && i + 1 < argc) {
      samples_file = argv[++i];
    } else if (arg == "--output_prefix" && i + 1 < argc) {
      output_prefix = argv[++i];
    } else if (arg == "--child" && i + 1 < argc) {
      child_filter = std::atoi(argv[++i]);
    } else if (arg == "--line_alpha" && i + 1 < argc) {
      line_alpha = (uint8_t)std::clamp(std::atoi(argv[++i]), 0, 255);
    } else if (arg == "--help" || arg == "-h") {
      Printf("Usage: %s [options]\n", argv[0]);
      Printf("  --artifacts_dir <path> : Path to Noperthedron/.artifacts/nopert229\n");
      Printf("  --child <0..3>         : Load specific child index (-1 for all, default -1)\n");
      Printf("  --samples <file.csv>   : Optional empirical samples CSV\n");
      Printf("  --output_prefix <pre>  : Output file prefix (default 'tube_')\n");
      Printf("  --line_alpha <0..255>  : Wireframe layer opacity (default 56 = ~22%%)\n");
      return 0;
    }
  }

  Printf("=== Nopert #229 Identity Tube & View Sphere Margin Visualization ===\n");
  Printf("Artifacts Directory: %s\n", artifacts_dir.c_str());
  Printf("Canvas Dimensions:   %d x %d (16:9)\n", WIDTH, HEIGHT);
  Printf("Wireframe Alpha:     %d/255\n", line_alpha);

  std::vector<LeafTriangle> leaves;
  int total_rows = 0;
  int total_splits = 0;

  for (int child = 0; child < 4; child++) {
    if (child_filter >= 0 && child != child_filter) continue;
    std::string filename = std::format("local-view-child{}.json", child);
    std::string fullpath = (std::filesystem::path(artifacts_dir) / filename).string();

    if (std::filesystem::exists(fullpath)) {
      LoadChildJson(fullpath, &leaves, &total_rows, &total_splits);
    } else {
      Printf("  Child %d (%s) not found (pending computation)\n", child, fullpath.c_str());
    }
  }

  if (leaves.empty()) {
    Printf("Error: No certified leaf triangles loaded from %s!\n", artifacts_dir.c_str());
    return 1;
  }

  Printf("Loaded %zu certified leaves across %d total rows (%d splits).\n",
         leaves.size(), total_rows, total_splits);

  std::vector<EmpiricalSample> samples;
  if (!samples_file.empty()) {
    samples = LoadEmpiricalSamples(samples_file);
  }

  // Generate all 4 visualizations:
  std::string file_wedge_eq = output_prefix + "wedge_equirect.png";
  std::string file_wedge_lam = output_prefix + "wedge_lambert.png";
  std::string file_full = output_prefix + "full_sphere.png";
  std::string file_proj = output_prefix + "projective_plane.png";

  Printf("\nRendering View 1: Upper Wedge Equirectangular -> %s...\n", file_wedge_eq.c_str());
  RenderWedgeEquirect(leaves, samples, line_alpha, file_wedge_eq);

  Printf("\nRendering View 2: Upper Wedge Lambert Equal-Area -> %s...\n", file_wedge_lam.c_str());
  RenderWedgeLambert(leaves, samples, line_alpha, file_wedge_lam);

  Printf("\nRendering View 3: Full Sphere (C5 Replicated) -> %s...\n", file_full.c_str());
  RenderFullSphere(leaves, line_alpha, file_full);

  Printf("\nRendering View 4: Projective Plane Barycentric Triangle -> %s...\n", file_proj.c_str());
  RenderProjectivePlane(leaves, line_alpha, file_proj);

  Printf("\nAll 4 visualizations generated successfully!\n");
  return 0;
}
