
#include "error-history.h"

#include <cstdio>
#include <optional>
#include <map>
#include <vector>
#include <string>
#include <cmath>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"
#include "image.h"
#include "bounds.h"

using namespace std;
using int64 = int64_t;
using uint32 = uint32_t;

template<class C, class K, class V>
static inline auto FindWithDefault(const C &cont, const K &key, V v) -> V {
  auto cit = cont.find(key);
  if (cit == cont.end()) return v;
  return cit->second;
}

static inline constexpr HalfAlpha(uint32_t c) {
  uint32 rgb = c & 0xFFFFFF00;
  uint32 a = (c & 0xFF) >> 1;
  return rgb | a;
}

ErrorHistory::ErrorHistory(const std::string &filename,
                           int num_models) : filename(filename),
                                             num_models(num_models) {
  Load();
}

void ErrorHistory::Add(int64_t round_number,
                       double error_per_example,
                       int column_idx,
                       int model_idx) {
  CHECK(round_number >= 0);
  CHECK(column_idx >= 0) << column_idx;
  CHECK(model_idx >= 0 && model_idx < num_models) << model_idx << " " << num_models;

  records.push_back(Record{.round_number = round_number,
                           .error_per_example = error_per_example,
                           .column_index = column_idx,
                           .model_index = model_idx});
}

void ErrorHistory::Save() {
  // XXX sort
  FILE *f = fopen(filename.c_str(), "wb");
  CHECK(f) << filename;

  for (const Record &r : records) {
    fprintf(f, "%lld\t%d\t%d\t%.12g\n",
            r.round_number, r.model_index,
            r.column_index,
            r.error_per_example);
  }

  fclose(f);
  printf("Wrote %lld error records to %s\n", (int64_t)records.size(),
         filename.c_str());
}

void ErrorHistory::Load() {
  vector<string> lines = Util::ReadFileToLines(filename);
  records.clear();
  records.reserve(lines.size());
  for (string &line : lines) {
    Record r;
    r.round_number = std::stoll(Util::chopto('\t', line));
    if (r.round_number == 0) continue;
    r.model_index = std::stoi(Util::chopto('\t', line));
    if (r.model_index < 0 || r.model_index >= num_models) continue;
    r.column_index = std::stoi(Util::chopto('\t', line));
    if (r.column_index < 0) continue;
    r.error_per_example = std::stod(Util::chopto('\t', line));
    records.push_back(r);
  }

  printf("Read %lld error records from %s\n",
         (int64_t)records.size(),
         filename.c_str());
}

void ErrorHistory::WriteMergedTSV(const string &outfile,
                                  int column,
                                  std::optional<int> max_points) const {
  std::map<int64, vector<Record>> collated_records;
  for (const auto &record : records)
    collated_records[record.round_number].push_back(record);

  vector<bool> had_error(num_models, false);
  auto AllHadError = [&had_error]() {
      for (bool b : had_error) if (!b) return false;
      return true;
    };
  vector<double> last_error(num_models, 0.0);

  // Collate so that we have one row for each round.
  vector<std::pair<int64, vector<double>>> rows;
  for (const auto &[round, recs] : collated_records) {
    for (const auto &rec : recs) {
      if (rec.column_index == column) {
        int idx = rec.model_index;
        had_error[idx] = true;
        last_error[idx] = rec.error_per_example;
      }
    }
    if (AllHadError()) {
      rows.emplace_back(round, last_error);
    }
  }

  // Now thin to the number of rows.
  if (max_points.has_value() && rows.size() > max_points.value()) {
    const int64 out_points = max_points.value();

    CHECK(rows.size() >= 2) << "need at least two rows";

    vector<std::pair<int64, vector<double>>> out;
    out.reserve(out_points);

    // First point is always the actual first point.
    int prev_srci = 0;
    out.push_back(rows[0]);
    // Now, for the rest, assume points are evenly distributed in
    // the input (not necessarily true; could improve by finding
    // the target round) to compute the index to sample.
    for (int i = 1; i < out_points; i++) {
      // How far are we through the output?
      double f = i / (double)out_points;
      // And how far is that through the original rows?
      const int srci = f * rows.size();
      if (srci < 0 || srci >= rows.size()) break;

      // This is not really right either, in the case that we
      // have more samples towards the end of the time period.
      vector<double> avgs(num_models, 0.0);
      double denom = 0.0;
      for (int j = prev_srci + 1; j <= srci; j++) {
        const auto &[row_round, row_vals] = rows[j];
        for (int m = 0; m < num_models; m++) {
          avgs[m] += row_vals[m];
        }
        denom += 1.0;
      }
      for (double &v : avgs) v /= denom;

      // Round could be the midpoint?
      out.emplace_back(rows[srci].first, avgs);
      prev_srci = srci;
    }

    rows = std::move(out);
  }

  FILE *f = fopen(outfile.c_str(), "wb");
  for (const auto &[round, vals] : rows) {
    fprintf(f, "%lld", round);
    for (double v : vals)
      fprintf(f, "\t%.4f", v);
    fprintf(f, "\n");
  }
  fclose(f);
  printf("Wrote merged to %s\n", outfile.c_str());
}

ImageRGBA ErrorHistory::MakeImage(int width, int height,
                                  std::map<int, uint32_t> column_colors,
                                  int model_idx) const {
  ImageRGBA image(width, height);
  image.Clear32(0x000000FF);

  std::map<int64, vector<Record>> collated_records;
  for (const auto &record : records) {
    if (record.model_index == model_idx &&
        column_colors.find(record.column_index) != column_colors.end()) {
      collated_records[record.round_number].push_back(record);
    }
  }

  if (collated_records.size() < 2) {
    image.BlendText2x32(4, (height - 9) / 2,
                        0xFF7777FF,
                        "Not enough records");
    return image;
  }

  Bounds bounds;

  // TODO: Could use Bounds for this instead...
  const int64_t min_round = collated_records.begin()->first;
  const int64_t max_round = collated_records.rbegin()->first;
  const int round_width = max_round - min_round;
  CHECK(round_width > 0);

  // Always including y=0.
  bounds.Bound(min_round, 0.0);
  bounds.Bound(max_round, 0.0);

  // Compute bounds of the data on the y axis.
  for (const auto &[round, vec] : collated_records) {
    for (const Record &r : vec) {
      bounds.Bound(round, r.error_per_example);
    }
  }

  // How to set this? This value seems to work reasonably well
  // but maybe it is weird to depend on the width of the data.
  double exp_stretch = round_width / 8.0;


  // A little bit of vertical margin.
  bounds.AddMarginsFrac(0.05, 0.0, 0.05, 0.0);
  Bounds::Scaler scaler = bounds.Stretch(width, height).FlipY();

  auto ToIntY = [&](double v) {
      return std::clamp((int)std::round(scaler.ScaleY(v)), 0, height - 1);
    };

  auto ValuesForRound = [&](double prev_round, double round) ->
    // column mapped to average, min, max.
    // Columns can be missing from the map, even if they were
    // selected.
    std::map<int, std::tuple<double, double, double>> {
    // Iterator to the first round >= round. We call
    // this the "upper bound" but it's what map<>::lower_bound
    // computes. Note that this point is just one of the columns,
    // so it's just a starting point for us.
    auto ub = collated_records.lower_bound(round);

    std::map<int, std::tuple<double, double, double>> ret;

    // For each column:
    for (const auto &[col, color] : column_colors) {
      // Get the exponential weighted moving average by moving
      // backwards (all the data points are before this one).

      optional<double> vmin = nullopt, vmax = nullopt;

      // Current total.
      double vtotal = 0.0;
      // Total mass already included in the point.
      double vmass = 0.0;

      for (auto it = ub; it != collated_records.begin(); --it) {
        const auto &[r, vec] = *it;
        double dist = round - r;
        const double mass = 1.0 / exp(dist / exp_stretch);

        // Cut off if dist implies the contributions hereafter
        // will be trivial.
        if (vmass > 0.0 && mass / vmass < 0.0001)
          break;

        for (const Record &rec : vec) {
          if (rec.column_index == col) {
            double rv = rec.error_per_example;
            vtotal += rv * mass;
            vmass += mass;

            // Compute the min/max over the range where the
            // contribution is not trivial.
            if (r >= prev_round) {
              if (!vmin.has_value() || rv < vmin.value())
                vmin.emplace(rv);
              if (!vmax.has_value() || rv > vmax.value())
                vmax.emplace(rv);
            }

            // Should not be duplicates at a given round number
            break;
          }
        }
      }

      // If we have any data, plot it.
      if (vmass > 0.0 && vmin.has_value() && vmax.has_value()) {
        ret[col] =
          std::make_tuple(vtotal / vmass, vmin.value(), vmax.value());
      }
    }
    return ret;
  };


  // TODO: Label y axis data.
  int y0 = ToIntY(0.0);
  image.BlendLine32(0, y0, width - 1, y0,
                    0xFFFFFF77);
  image.BlendText32(2, y0 - 9 - 2, 0xFFFFFFAA, "0.0");

  for (const auto &[col, values] : ValuesForRound(0.0, max_round)) {
    const uint32 color = FindWithDefault(column_colors, col, 0xFF0000FF);
    const uint32 hcolor = HalfAlpha(color);
    const auto &[v, vmin_, vmax_] = values;

    int y = ToIntY(v);
    image.BlendLine32(0, y, width - 1, y, HalfAlpha(hcolor));
    // XXX something to avoid labels overlapping each other, and series.
    // Like we could give several candidate positions and find the one
    // that would write on top of the fewest existing non-black pixels.
    string label = StringPrintf("%.4f", v);
    image.BlendText32(width - 2 - label.size() * 9,
                      y - 9 - 4, color, label);
  }


  double prev_round = 0;
  // Calculate each pixel column independently.
  for (int x = 0; x < width; x++) {
    // Interpolated round at this column.
    double round = scaler.UnscaleX(x);

    for (const auto &[col, values] : ValuesForRound(prev_round, round)) {
      // Should always be in the map.
      const uint32 color = FindWithDefault(column_colors, col, 0xFF0000FF);
      const auto &[v, vmin, vmax] = values;

      uint32 hcolor = HalfAlpha(color);
      // Note reversed sense of min/max since y axis is inverted.
      int ymax = ToIntY(vmin);
      int ymin = ToIntY(vmax);
      image.BlendLine32(x, ymin, x, ymax, hcolor);

      const int y = ToIntY(v);
      image.BlendPixel32(x, y - 1, color);
      image.BlendPixel32(x, y, color);
      image.BlendPixel32(x, y + 1, color);
    }

    prev_round = round;
  }

  image.BlendText32(1, height - 10, 0xFFAAAAAA,
                    StringPrintf("round %lld", min_round));

  string maxr = StringPrintf("%lld", max_round);
  image.BlendText32(width - maxr.size() * 9 - 1, height - 10,
                    0xFFAAAAAA, maxr);

  return image;
}
