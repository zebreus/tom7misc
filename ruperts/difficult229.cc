// difficult229.cc: Driver application for resolving difficult cells from lean229.
//
// Reads all difficult cells into memory and processes them one at a time.
// All-or-nothing per cell:
//   - Accumulates progress for cell #123456 into chart<chart>.123456.done.tmp
//   - Only if cell #123456 is 100% solved (all branches certified with no difficult leaves),
//     it renames chart<chart>.123456.done.tmp -> chart<chart>.123456.done, removes the cell
//     from the in-memory unsolved list, and atomically rewrites chart<chart>.difficult.
//   - If interrupted (Ctrl-C / SIGINT) or not fully certifiable, the temporary file is deleted,
//     the cell remains in the unsolved list, and chart<chart>.difficult contains all unsolved cells.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "lib229.h"
#include "periodically.h"
#include "ruperts-util.h"
#include "timer.h"

static int RunDifficult(
    int chart, std::string difficult_path, std::string out_dir, int max_depth,
    int max_box_depth, int max_view_depth, int batch_size, int cone_samples,
    int escalate_depth, int escalate_cone_samples, int deep_escalate_depth,
    int deep_escalate_cone_samples, int lp_escalate_box_depth,
    double tube_radius, double limit_sec, int num_threads, bool use_gpu,
    int64_t limit_cells, int64_t target_cell_id, bool dry_run, bool verbose,
    bool show_status = true) {
  if (difficult_path.empty()) {
    difficult_path = std::format("chart{}.difficult", chart);
  }

  Print(ACYAN("=== Difficult 229 Solver ===\n"));
  Print("Chart: {}, Difficult file: {}, Out dir: {}\n", chart, difficult_path, out_dir);
  Print("Limits: max_depth={}, max_box_depth={}, max_view_depth={}, lp_box_depth={}, tube_radius={:.3g}, limit_sec={}, batch_size={}\n",
        max_depth, max_box_depth, max_view_depth, lp_escalate_box_depth, tube_radius,
        limit_sec > 0.0 ? std::format("{}s", limit_sec) : "none", batch_size);
  Print("Device: {}\n", use_gpu ? "OpenCL (GPU/CPU)" : "Multi-threaded CPU");

  if (!std::filesystem::exists(difficult_path)) {
    Print(ARED("Difficult file '{}' does not exist.\n"), difficult_path);
    return -1;
  }

  std::vector<DifficultCell> cells = ReadDifficultFile(difficult_path);
  Print("Loaded {} cells from {}\n", cells.size(), difficult_path);

  // Scan existing .done files to identify completed cells
  std::vector<DifficultCell> unsolved_cells;
  size_t already_done = 0;
  for (const auto &c : cells) {
    std::string done_file = std::format("{}/chart{}.{}.done", out_dir, c.chart, c.id);
    if (std::filesystem::exists(done_file)) {
      already_done++;
    } else {
      unsolved_cells.push_back(c);
    }
  }

  if (already_done > 0) {
    Print("Found {} previously completed .done files; {} cells remaining to solve.\n",
          already_done, unsolved_cells.size());
    WriteDifficultFile(difficult_path, unsolved_cells);
  }

  if (unsolved_cells.empty()) {
    Print(AGREEN("All difficult cells in chart {} are already certified!\n"), chart);
    return 0;
  }

  if (dry_run) {
    Print("Dry run complete. {} cells need processing.\n", unsolved_cells.size());
    return 0;
  }

  SearchManager mgr;
  mgr.chart = chart;
  mgr.use_gpu = use_gpu;
  mgr.num_threads = num_threads;
  mgr.batch_size = batch_size;
  mgr.max_depth = max_depth;
  mgr.max_box_depth = max_box_depth;
  mgr.max_view_depth = max_view_depth;
  mgr.cone_samples = cone_samples;
  mgr.escalate_depth = escalate_depth;
  mgr.escalate_cone_samples = escalate_cone_samples;
  mgr.deep_escalate_depth = deep_escalate_depth;
  mgr.deep_escalate_cone_samples = deep_escalate_cone_samples;
  mgr.lp_escalate_box_depth = lp_escalate_box_depth;
  mgr.tube_radius = tube_radius;
  mgr.suspicious_depth = 64;
  mgr.max_seconds = limit_sec;
  mgr.prioritize_related = false;
  mgr.output_dir = out_dir;

  // Driver manages persistence and life cycle
  mgr.auto_init_root = false;
  mgr.write_row_file = false;
  mgr.write_difficult_file = false;
  mgr.enable_checkpoint = false;
  mgr.show_banner = false;
  mgr.verbose_status = show_status;

  if (use_gpu) {
    mgr.InitOpenCL();
  }

  Timer total_timer;
  size_t total_processed = 0;
  size_t total_solved = 0;
  size_t total_unsolved = 0;
  size_t total_timed_out = 0;
  uint64_t total_rows_written = 0;

  for (size_t i = 0; i < unsolved_cells.size(); ) {
    if (SigIntReceived()) {
      mgr.status.Print(AYELLOW("\nInterrupted by SIGINT. Preserving remaining unsolved cells in {}.\n"),
                       difficult_path);
      break;
    }

    DifficultCell cell = unsolved_cells[i];

    if (target_cell_id >= 0 && cell.id != target_cell_id) {
      i++;
      continue;
    }

    if (limit_cells > 0 && total_processed >= (size_t)limit_cells) {
      mgr.status.Print("Reached cell processing limit of {}.\n", limit_cells);
      break;
    }

    total_processed++;

    std::string done_filename = std::format("chart{}.{}.done", cell.chart, cell.id);
    std::string done_path = std::format("{}/{}", out_dir, done_filename);
    std::string tmp_filename = std::format("{}.tmp", done_filename);
    std::string tmp_path = std::format("{}/{}", out_dir, tmp_filename);

    FILE *tmp_fp = fopen(tmp_path.c_str(), "w");
    if (!tmp_fp) {
      mgr.status.Print(ARED("Failed to create temporary file {}\n"), tmp_path);
      i++;
      continue;
    }

    uint64_t cell_rows = 0;
    mgr.row_callback = [&](std::string_view row) {
      fputs(std::string(row).c_str(), tmp_fp);
      cell_rows++;
    };

    std::vector<DifficultCell> new_difficult;
    mgr.difficult_callback = [&](const SearchNode &node, double margin) {
      DifficultCell dc;
      dc.id = node.id;
      dc.parent_id = node.parent_id;
      dc.depth = node.depth;
      dc.box_depth = node.box_depth;
      dc.view_depth = node.view_depth;
      dc.chart = node.chart;
      dc.c = node.box.center;
      dc.r = node.box.radii;
      dc.v[0] = node.tri.corners[0];
      dc.v[1] = node.tri.corners[1];
      dc.v[2] = node.tri.corners[2];
      dc.best_margin = margin;
      new_difficult.push_back(dc);
    };

    mgr.ResetState();
    mgr.chart = cell.chart;
    mgr.next_node_id = std::max<int64_t>(1000000000LL, cell.id * 1000LL);
    mgr.stack.push_back(cell.ToSearchNode());
    mgr.status_detail = std::format("Cell #{}: [{}/{}] (depth {})",
                                    cell.id, total_processed,
                                    unsolved_cells.size(), cell.depth);

    mgr.status.Print("[{}/{}] Cell #{} (depth {}, box_depth {}, view_depth {}, margin: {:.6g})...\n",
          total_processed, unsolved_cells.size(),
          cell.id, cell.depth, cell.box_depth, cell.view_depth, cell.best_margin);

    Timer cell_timer;
    mgr.Run();

    std::fflush(tmp_fp);
    fclose(tmp_fp);
    tmp_fp = nullptr;

    double cell_seconds = cell_timer.Seconds();

    if (SigIntReceived()) {
      std::error_code ec;
      std::filesystem::remove(tmp_path, ec);
      mgr.status.Print(AYELLOW("\nInterrupted during cell #{}. Cleaned up {}.\n"), cell.id, tmp_filename);
      break;
    }

    bool timed_out = mgr.timed_out || (limit_sec > 0.0 && cell_seconds >= limit_sec);
    if (timed_out) {
      std::error_code ec;
      std::filesystem::remove(tmp_path, ec);
      total_timed_out++;
      mgr.status.Print(AYELLOW("  ⏱")
            " Cell #{} TIMED OUT after {} (remaining stack: {}, shelved: {}).\n"
            " Retaining in {}.\n",
            cell.id, ANSI::Time(cell_seconds), FormatNum(mgr.stack.size()),
            FormatNum(new_difficult.size()), difficult_path);
      i++;
      continue;
    }

    bool solved = mgr.stack.empty() && new_difficult.empty();
    if (solved) {
      std::error_code ec;
      std::filesystem::rename(tmp_path, done_path, ec);
      if (ec) {
        mgr.status.Print(ARED("  Error renaming {} to {}: {}\n"), tmp_path, done_path, ec.message());
        i++;
      } else {
        total_solved++;
        total_rows_written += cell_rows;
        unsolved_cells.erase(unsolved_cells.begin() + i);
        WriteDifficultFile(difficult_path, unsolved_cells);
        mgr.status.Print(AGREEN("  ✔")
              " Cell #{} SOLVED in {}! ({} rows -> {}) [{} remaining in {}]\n",
              cell.id, ANSI::Time(cell_seconds), FormatNum(cell_rows),
              done_filename, FormatNum(unsolved_cells.size()), difficult_path);
        // Do not increment i; next element slid into index i.
      }
    } else {
      std::error_code ec;
      std::filesystem::remove(tmp_path, ec);
      total_unsolved++;
      mgr.status.Print(AORANGE("  ✘")
            " Cell #{} NOT fully certified in {} (remaining stack: {}, shelved: {}).\n"
            " Retaining in {}.\n",
            cell.id, ANSI::Time(cell_seconds), FormatNum(mgr.stack.size()),
            FormatNum(new_difficult.size()), difficult_path);
      i++;
    }
  }

  mgr.status.Clear();

  // Ensure state is cleanly persisted
  WriteDifficultFile(difficult_path, unsolved_cells);

  Print(ACYAN("\n=== Summary ===\n"));
  Print("Total processed: {}\n"
        "Total solved:    {}\n"
        "Total failed:    {}\n"
        "Total timed out: {}\n"
        "Remaining:       {} in {}\n"
        "Rows written:    {}\n"
        "Elapsed time:    {}\n",
        FormatNum(total_processed),
        FormatNum(total_solved),
        FormatNum(total_unsolved),
        FormatNum(total_timed_out),
        FormatNum(unsolved_cells.size()), difficult_path,
        FormatNum(total_rows_written),
        ANSI::Time(total_timer.Seconds()));

  return 0;
}

static int RunDifficultMixture(
    int chart, std::string difficult_path, std::string out_dir, int max_depth,
    int max_box_depth, int max_view_depth, int max_nodes, int max_split_delta,
    int cone_samples, int max_components,
    double split_kappa, double tube_radius, double limit_sec, int num_threads,
    int64_t limit_cells, int64_t target_cell_id, bool dry_run, bool verbose,
    bool show_status = true) {
  if (difficult_path.empty()) {
    difficult_path = std::format("chart{}.difficult", chart);
  }

  Print(ACYAN("=== Difficult 229 Solver (Convex Mixture Mode) ===\n"));
  Print("Chart: {}, Difficult file: {}, Out dir: {}\n", chart, difficult_path, out_dir);
  Print("Limits: max_nodes={}, max_split_delta={}, max_depth={}, max_box_depth={}, max_view_depth={}\n",
        max_nodes, max_split_delta, max_depth, max_box_depth, max_view_depth);
  Print("Mixture: max_components={}, split_kappa={:.2g}, tube_radius={:.3g}, limit_sec={}\n",
        max_components, split_kappa, tube_radius,
        limit_sec > 0.0 ? std::format("{}s", limit_sec) : "none");
  Print("Parallelism: {} worker threads (CPU)\n", num_threads);

  if (!std::filesystem::exists(difficult_path)) {
    Print(ARED("Difficult file '{}' does not exist.\n"), difficult_path);
    return -1;
  }

  std::vector<DifficultCell> cells = ReadDifficultFile(difficult_path);
  Print("Loaded {} cells from {}\n", cells.size(), difficult_path);

  // Scan existing .done files to identify completed cells
  std::vector<DifficultCell> unsolved_cells;
  size_t already_done = 0;
  for (const auto &c : cells) {
    std::string done_file = std::format("{}/chart{}.{}.done", out_dir, c.chart, c.id);
    if (std::filesystem::exists(done_file)) {
      already_done++;
    } else {
      unsolved_cells.push_back(c);
    }
  }

  if (already_done > 0) {
    Print("Found {} previously completed .done files; {} cells remaining to solve.\n",
          already_done, unsolved_cells.size());
    WriteDifficultFile(difficult_path, unsolved_cells);
  }

  if (unsolved_cells.empty()) {
    Print(AGREEN("All difficult cells in chart {} are already certified!\n"), chart);
    return 0;
  }

  if (dry_run) {
    Print("Dry run complete. {} cells need processing.\n", unsolved_cells.size());
    return 0;
  }

  StatusBar status(1);
  Timer total_timer;
  std::mutex state_mu;
  size_t total_processed = 0;
  size_t total_solved = 0;
  size_t total_unsolved = 0;
  size_t total_timed_out = 0;
  uint64_t total_rows_written = 0;
  std::vector<DifficultCell> remaining_unsolved = unsolved_cells;

  std::atomic<size_t> next_cell_idx = 0;
  int actual_threads = std::max(1, num_threads);

  auto WorkerLoop = [&]() {
    while (!SigIntReceived()) {
      size_t idx = next_cell_idx.fetch_add(1);
      if (idx >= unsolved_cells.size()) break;
      if (limit_cells > 0 && idx >= (size_t)limit_cells) break;

      DifficultCell cell = unsolved_cells[idx];
      if (target_cell_id >= 0 && cell.id != target_cell_id) continue;

      size_t current_proc = 0;
      {
        std::lock_guard<std::mutex> lock(state_mu);
        total_processed++;
        current_proc = total_processed;
        status.Print("[{}/{}] Cell #{} (depth {}, box_depth {}, view_depth {}, margin: {:.6g})...\n",
                     current_proc, unsolved_cells.size(),
                     cell.id, cell.depth, cell.box_depth, cell.view_depth, cell.best_margin);
      }

      std::string done_filename = std::format("chart{}.{}.done", cell.chart, cell.id);
      std::string done_path = std::format("{}/{}", out_dir, done_filename);
      std::string tmp_filename = std::format("{}.tmp", done_filename);
      std::string tmp_path = std::format("{}/{}", out_dir, tmp_filename);

      FILE *tmp_fp = fopen(tmp_path.c_str(), "w");
      if (!tmp_fp) {
        std::lock_guard<std::mutex> lock(state_mu);
        status.Print(ARED("Failed to create temporary file {}\n"), tmp_path);
        continue;
      }

      uint64_t cell_rows = 0;
      auto row_cb = [&](std::string_view row) {
        fputs(std::string(row).c_str(), tmp_fp);
        cell_rows++;
      };

      Timer cell_timer;
      std::atomic<bool> cell_interrupted = false;
      auto stats = SolveCellMixture(
          cell, max_depth, max_box_depth, max_view_depth,
          max_nodes, max_split_delta,
          cone_samples, max_components, split_kappa,
          /*tube_radius=*/tube_radius, limit_sec, row_cb, &cell_interrupted);

      std::fflush(tmp_fp);
      fclose(tmp_fp);
      tmp_fp = nullptr;

      double cell_seconds = cell_timer.Seconds();

      if (SigIntReceived()) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        break;
      }

      if (!stats.solved) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        std::lock_guard<std::mutex> lock(state_mu);
        if (limit_sec > 0.0 && cell_seconds >= limit_sec) {
          total_timed_out++;
          status.Print(AYELLOW("  ⏱") " Cell #{} TIMED OUT after {} ({} nodes, {} certified, {} ceiling hits, {} on stack, worst margin: {:.6g}). Retaining in {}.\n",
                       cell.id, ANSI::Time(cell_seconds), stats.total_nodes, stats.certified_leaves, stats.ceiling_hits, stats.remaining_nodes, stats.worst_margin, difficult_path);
        } else {
          total_unsolved++;
          status.Print(AORANGE("  ✘") " Cell #{} NOT fully certified in {} ({} nodes, {} certified, {} ceiling hits, worst margin: {:.6g}). Retaining in {}.\n",
                       cell.id, ANSI::Time(cell_seconds), stats.total_nodes, stats.certified_leaves, stats.ceiling_hits, stats.worst_margin, difficult_path);
        }
      } else {
        std::error_code ec;
        std::filesystem::rename(tmp_path, done_path, ec);
        std::lock_guard<std::mutex> lock(state_mu);
        if (ec) {
          status.Print(ARED("  Error renaming {} to {}: {}\n"), tmp_path, done_path, ec.message());
        } else {
          total_solved++;
          total_rows_written += cell_rows;
          std::erase_if(remaining_unsolved, [&](const DifficultCell &c) {
            return c.id == cell.id && c.chart == cell.chart;
          });
          WriteDifficultFile(difficult_path, remaining_unsolved);
          status.Print(AGREEN("  ✔") " Cell #{} SOLVED in {}! ({} rows, {} leaves -> {}) [{} remaining in {}]\n",
                       cell.id, ANSI::Time(cell_seconds), cell_rows, stats.certified_leaves,
                       done_filename, remaining_unsolved.size(), difficult_path);
        }
      }
    }
  };

  std::vector<std::thread> workers;
  for (int t = 0; t < actual_threads; t++) {
    workers.emplace_back(WorkerLoop);
  }
  for (auto &w : workers) {
    w.join();
  }

  status.Clear();

  // Final synchronization of difficult file
  WriteDifficultFile(difficult_path, remaining_unsolved);

  Print(ACYAN("\n=== Mixture Solver Summary ===\n"));
  Print("Total processed: {}\n"
        "Total solved:    {}\n"
        "Total failed:    {}\n"
        "Total timed out: {}\n"
        "Remaining:       {} in {}\n"
        "Rows written:    {}\n"
        "Elapsed time:    {}\n",
        FormatNum(total_processed),
        FormatNum(total_solved),
        FormatNum(total_unsolved),
        FormatNum(total_timed_out),
        FormatNum(remaining_unsolved.size()), difficult_path,
        FormatNum(total_rows_written),
        ANSI::Time(total_timer.Seconds()));

  return 0;
}

static void PrintHelp() {
  Print("Usage: ./difficult229.exe [options]\n"
        "  --chart <0|1|2>         Cayley chart index (default 0)\n"
        "  --difficult <path>      Path to difficult cells file (default chart<chart>.difficult)\n"
        "  --out_dir <path>        Output directory for .done files and rewritten difficult file (default .)\n"
        "  --mixture               Use convex triple mixture branch-and-bound solver (CPU)\n"
        "  --max_components <N>    Max mixture components in mixture mode (default 4)\n"
        "  --split_kappa <K>       Rotation to view diameter ratio for splits in mixture mode (default 0.5)\n"
        "  --max_nodes <N>         Max total nodes evaluated per cell (default 64 in mixture mode)\n"
        "  --max_split_delta <N>   Max split depth from root per cell (default 4 in mixture mode)\n"
        "  --max_depth <D>         Max search depth per cell (default 80 GPU / 54 mixture)\n"
        "  --max_box_depth <D>     Max box subdivision depth (default 58 GPU / 42 mixture)\n"
        "  --max_view_depth <D>    Max view subdivision depth (default 20 GPU / 14 mixture)\n"
        "  --batch_size <N>        Batch size for evaluator (default 4096)\n"
        "  --cone_samples <N>      Base cone samples (default 8)\n"
        "  --escalate_depth <D>    First escalation depth (default 44)\n"
        "  --escalate_cone_samples <N> First escalated cone samples (default 12)\n"
        "  --deep_escalate_depth <D> Second escalation depth (default 50)\n"
        "  --deep_escalate_cone_samples <N> Second escalated cone samples (default 14)\n"
        "  --lp_box_depth <D>      Box depth to trigger CPU LP escalation (default 54)\n"
        "  --tube_radius <R>       Identity symmetry tube radius (default 1e-4)\n"
        "  --limit_sec <S>         Time limit in seconds per cell (default 0 = no limit in GPU / 60s in mixture)\n"
        "  --threads <T>           Worker threads (default 8)\n"
        "  --cpu                   Force multi-threaded CPU execution in standard mode\n"
        "  --gpu                   Use OpenCL acceleration in standard mode (default)\n"
        "  --limit <N>             Process at most N cells (default all)\n"
        "  --cell_id <ID>          Process only specific cell ID\n"
        "  --dry_run               Scan and report status without processing\n"
        "  --verbose               Print verbose per-cell status\n"
        "  --no_status             Disable live status bar\n"
        "  --help, -h              Show this help message\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  InstallSignalHandlers();

  int chart = 0;
  std::string difficult_path;
  std::string out_dir = ".";
  bool mixture_mode = false;
  int max_components = 4;
  double split_kappa = 0.5;
  int max_nodes = 64;
  int max_split_delta = 4;
  bool max_depth_specified = false;
  bool max_box_depth_specified = false;
  bool max_view_depth_specified = false;
  bool limit_sec_specified = false;
  int max_depth = 80;
  int max_box_depth = 58;
  int max_view_depth = 20;
  int batch_size = 4096;
  int cone_samples = 8;
  int escalate_depth = 44;
  int escalate_cone_samples = 12;
  int deep_escalate_depth = 50;
  int deep_escalate_cone_samples = 14;
  int lp_escalate_box_depth = 54;
  double tube_radius = 1e-4;
  double limit_sec = 0.0;
  int num_threads = 8;
  bool use_gpu = true;
  int64_t limit_cells = 0;
  int64_t target_cell_id = -1;
  bool dry_run = false;
  bool verbose = false;
  bool show_status = true;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "--chart" && i + 1 < argc) {
      chart = std::atoi(argv[++i]);
    } else if (arg == "--difficult" && i + 1 < argc) {
      difficult_path = argv[++i];
    } else if (arg == "--out_dir" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (arg == "--mixture" || arg == "--mode=mixture") {
      mixture_mode = true;
    } else if (arg == "--mode" && i + 1 < argc) {
      std::string_view m = argv[++i];
      if (m == "mixture") mixture_mode = true;
    } else if (arg == "--max_components" && i + 1 < argc) {
      max_components = std::atoi(argv[++i]);
    } else if (arg == "--split_kappa" && i + 1 < argc) {
      split_kappa = std::atof(argv[++i]);
    } else if (arg == "--max_nodes" && i + 1 < argc) {
      max_nodes = std::atoi(argv[++i]);
    } else if (arg == "--max_split_delta" && i + 1 < argc) {
      max_split_delta = std::atoi(argv[++i]);
    } else if (arg == "--max_depth" && i + 1 < argc) {
      max_depth = std::atoi(argv[++i]);
      max_depth_specified = true;
    } else if (arg == "--max_box_depth" && i + 1 < argc) {
      max_box_depth = std::atoi(argv[++i]);
      max_box_depth_specified = true;
    } else if (arg == "--max_view_depth" && i + 1 < argc) {
      max_view_depth = std::atoi(argv[++i]);
      max_view_depth_specified = true;
    } else if (arg == "--batch_size" && i + 1 < argc) {
      batch_size = std::atoi(argv[++i]);
    } else if (arg == "--cone_samples" && i + 1 < argc) {
      cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--escalate_depth" && i + 1 < argc) {
      escalate_depth = std::atoi(argv[++i]);
    } else if (arg == "--escalate_cone_samples" && i + 1 < argc) {
      escalate_cone_samples = std::atoi(argv[++i]);
    } else if (arg == "--deep_escalate_depth" && i + 1 < argc) {
      deep_escalate_depth = std::atoi(argv[++i]);
    } else if (arg == "--deep_escalate_cone_samples" && i + 1 < argc) {
      deep_escalate_cone_samples = std::atoi(argv[++i]);
    } else if ((arg == "--lp_box_depth" || arg == "--lp_escalate_box_depth") && i + 1 < argc) {
      lp_escalate_box_depth = std::atoi(argv[++i]);
    } else if (arg == "--tube_radius" && i + 1 < argc) {
      tube_radius = std::atof(argv[++i]);
    } else if (arg == "--limit_sec" && i + 1 < argc) {
      limit_sec = std::atof(argv[++i]);
      limit_sec_specified = true;
    } else if (arg == "--threads" && i + 1 < argc) {
      num_threads = std::atoi(argv[++i]);
    } else if (arg == "--cpu") {
      use_gpu = false;
    } else if (arg == "--gpu") {
      use_gpu = true;
    } else if (arg == "--limit" && i + 1 < argc) {
      limit_cells = std::atoll(argv[++i]);
    } else if (arg == "--cell_id" && i + 1 < argc) {
      target_cell_id = std::atoll(argv[++i]);
    } else if (arg == "--dry_run") {
      dry_run = true;
    } else if (arg == "--verbose") {
      verbose = true;
    } else if (arg == "--no_status") {
      show_status = false;
    } else if (arg == "--help" || arg == "-h") {
      PrintHelp();
      return 0;
    } else {
      Print("Unknown arg '{}'. Try ./difficult229.exe --help\n", arg);
      return -1;
    }
  }

  if (mixture_mode) {
    if (!max_depth_specified) max_depth = 54;
    if (!max_box_depth_specified) max_box_depth = 48;
    if (!max_view_depth_specified) max_view_depth = 16;
    if (!limit_sec_specified) limit_sec = 60.0;
    return RunDifficultMixture(
        chart, difficult_path, out_dir, max_depth, max_box_depth,
        max_view_depth, max_nodes, max_split_delta, cone_samples,
        max_components, split_kappa, tube_radius,
        limit_sec, num_threads, limit_cells, target_cell_id, dry_run, verbose,
        show_status);
  }

  RunDifficult(
      chart, difficult_path, out_dir, max_depth, max_box_depth,
      max_view_depth, batch_size, cone_samples, escalate_depth,
      escalate_cone_samples, deep_escalate_depth, deep_escalate_cone_samples,
      lp_escalate_box_depth, tube_radius, limit_sec,
      num_threads, use_gpu, limit_cells, target_cell_id, dry_run, verbose,
      show_status);

  return 0;
}
