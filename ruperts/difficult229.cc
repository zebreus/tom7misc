// Copyright 2026 Tom 7. All rights reserved.
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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "lib229.h"
#include "periodically.h"
#include "timer.h"

static void PrintHelp() {
  Print("Usage: ./difficult229.exe [options]\n"
        "  --chart <0|1|2>         Cayley chart index (default 0)\n"
        "  --difficult <path>      Path to difficult cells file (default chart<chart>.difficult)\n"
        "  --out_dir <path>        Output directory for .done files and rewritten difficult file (default .)\n"
        "  --max_depth <D>         Max search depth per cell (default 64)\n"
        "  --max_box_depth <D>     Max box subdivision depth (default 48)\n"
        "  --max_view_depth <D>    Max view subdivision depth (default 16)\n"
        "  --batch_size <N>        Batch size for evaluator (default 4096)\n"
        "  --cone_samples <N>      Base cone samples (default 8)\n"
        "  --escalate_depth <D>    First escalation depth (default 44)\n"
        "  --escalate_cone_samples <N> First escalated cone samples (default 12)\n"
        "  --deep_escalate_depth <D> Second escalation depth (default 50)\n"
        "  --deep_escalate_cone_samples <N> Second escalated cone samples (default 14)\n"
        "  --threads <T>           CPU fallback worker threads (default 8)\n"
        "  --cpu                   Force multi-threaded CPU execution\n"
        "  --gpu                   Use OpenCL acceleration (default)\n"
        "  --limit <N>             Process at most N cells (default all)\n"
        "  --cell_id <ID>          Process only specific cell ID\n"
        "  --dry_run               Scan and report status without processing\n"
        "  --verbose               Print verbose per-cell SearchManager status\n"
        "  --help, -h              Show this help message\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  InstallSignalHandlers();

  int chart = 0;
  std::string difficult_path;
  std::string out_dir = ".";
  int max_depth = 64;
  int max_box_depth = 48;
  int max_view_depth = 16;
  int batch_size = 4096;
  int cone_samples = 8;
  int escalate_depth = 44;
  int escalate_cone_samples = 12;
  int deep_escalate_depth = 50;
  int deep_escalate_cone_samples = 14;
  int num_threads = 8;
  bool use_gpu = true;
  int64_t limit_cells = 0;
  int64_t target_cell_id = -1;
  bool dry_run = false;
  bool verbose = false;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "--chart" && i + 1 < argc) {
      chart = std::atoi(argv[++i]);
    } else if (arg == "--difficult" && i + 1 < argc) {
      difficult_path = argv[++i];
    } else if (arg == "--out_dir" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (arg == "--max_depth" && i + 1 < argc) {
      max_depth = std::atoi(argv[++i]);
    } else if (arg == "--max_box_depth" && i + 1 < argc) {
      max_box_depth = std::atoi(argv[++i]);
    } else if (arg == "--max_view_depth" && i + 1 < argc) {
      max_view_depth = std::atoi(argv[++i]);
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
    } else if (arg == "--help" || arg == "-h") {
      PrintHelp();
      return 0;
    } else {
      Print("Unknown arg '{}'. Try ./difficult229.exe --help\n", arg);
      return -1;
    }
  }

  if (difficult_path.empty()) {
    difficult_path = std::format("chart{}.difficult", chart);
  }

  Print(ACYAN("=== Difficult 229 Solver ===\n"));
  Print("Chart: {}, Difficult file: {}, Out dir: {}\n", chart, difficult_path, out_dir);
  Print("Limits: max_depth={}, max_box_depth={}, max_view_depth={}, batch_size={}\n",
        max_depth, max_box_depth, max_view_depth, batch_size);
  Print("Device: {}\n", use_gpu ? "OpenCL (GPU/CPU)" : "Multi-threaded CPU");

  if (!std::filesystem::exists(difficult_path)) {
    Print(ARED("Difficult file '{}' does not exist.\n"), difficult_path);
    return 1;
  }

  std::vector<DifficultCell> unsolved_cells = ReadDifficultFile(difficult_path);
  Print("Loaded {} cells from {}\n", FormatNum(unsolved_cells.size()), difficult_path);

  if (unsolved_cells.empty()) {
    Print(AGREEN("No difficult cells to process!\n"));
    return 0;
  }

  // Scan for any cells that were already solved in previous runs.
  std::vector<DifficultCell> remaining_cells;
  remaining_cells.reserve(unsolved_cells.size());
  size_t already_done_count = 0;

  for (const auto &cell : unsolved_cells) {
    std::string done_file = std::format("{}/chart{}.{}.done", out_dir, cell.chart, cell.id);
    if (std::filesystem::exists(done_file)) {
      already_done_count++;
    } else {
      remaining_cells.push_back(cell);
    }
  }

  if (already_done_count > 0) {
    Print(AYELLOW("Found {} previously completed cells (with .done files).\n"),
          FormatNum(already_done_count));
    unsolved_cells = std::move(remaining_cells);
    WriteDifficultFile(difficult_path, unsolved_cells);
    Print("Updated {} ({} cells remaining).\n", difficult_path, FormatNum(unsolved_cells.size()));
  }

  // Clean up any stale .done.tmp files in out_dir
  for (const auto &cell : unsolved_cells) {
    std::string tmp_file = std::format("{}/chart{}.{}.done.tmp", out_dir, cell.chart, cell.id);
    std::error_code ec;
    if (std::filesystem::exists(tmp_file)) {
      std::filesystem::remove(tmp_file, ec);
    }
  }

  if (dry_run) {
    Print("Dry run requested. {} difficult cells remain to be solved.\n",
          FormatNum(unsolved_cells.size()));
    return 0;
  }

  // Initialize SearchManager once to avoid repeated OpenCL recompilation.
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
  mgr.prioritize_related = false;
  mgr.output_dir = out_dir;

  // Driver manages persistence and life cycle
  mgr.auto_init_root = false;
  mgr.write_row_file = false;
  mgr.write_difficult_file = false;
  mgr.enable_checkpoint = false;
  mgr.verbose_status = verbose;

  if (use_gpu) {
    mgr.InitOpenCL();
  }

  Timer total_timer;
  size_t total_processed = 0;
  size_t total_solved = 0;
  size_t total_unsolved = 0;
  uint64_t total_rows_written = 0;

  for (size_t i = 0; i < unsolved_cells.size(); ) {
    if (SigIntReceived()) {
      Print(AYELLOW("\nInterrupted by SIGINT. Preserving remaining unsolved cells in {}.\n"),
            difficult_path);
      break;
    }

    DifficultCell cell = unsolved_cells[i];

    if (target_cell_id >= 0 && cell.id != target_cell_id) {
      i++;
      continue;
    }

    if (limit_cells > 0 && total_processed >= (size_t)limit_cells) {
      Print("Reached cell processing limit of {}.\n", limit_cells);
      break;
    }

    total_processed++;

    std::string done_filename = std::format("chart{}.{}.done", cell.chart, cell.id);
    std::string done_path = std::format("{}/{}", out_dir, done_filename);
    std::string tmp_filename = std::format("chart{}.{}.done.tmp", cell.chart, cell.id);
    std::string tmp_path = std::format("{}/{}", out_dir, tmp_filename);

    FILE *tmp_fp = fopen(tmp_path.c_str(), "w");
    if (!tmp_fp) {
      Print(ARED("Failed to create temporary file: {}\n"), tmp_path);
      i++;
      continue;
    }

    uint64_t cell_rows = 0;
    mgr.row_callback = [&](std::string_view row) {
      if (tmp_fp) {
        std::fwrite(row.data(), 1, row.size(), tmp_fp);
        cell_rows++;
      }
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

    Print("[{}/{}] Cell #{} (depth {}, box_depth {}, view_depth {}, margin: {:.6g})...\n",
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
      Print(AYELLOW("\nInterrupted during cell #{}. Cleaned up {}.\n"), cell.id, tmp_filename);
      break;
    }

    bool solved = mgr.stack.empty() && new_difficult.empty();
    if (solved) {
      std::error_code ec;
      std::filesystem::rename(tmp_path, done_path, ec);
      if (ec) {
        Print(ARED("  Error renaming {} to {}: {}\n"), tmp_path, done_path, ec.message());
        i++;
      } else {
        total_solved++;
        total_rows_written += cell_rows;
        unsolved_cells.erase(unsolved_cells.begin() + i);
        WriteDifficultFile(difficult_path, unsolved_cells);
        Print(AGREEN("  ✔ Cell #{} SOLVED in {}! ({} rows -> {}) [{} remaining in {}]\n"),
              cell.id, ANSI::Time(cell_seconds), FormatNum(cell_rows),
              done_filename, FormatNum(unsolved_cells.size()), difficult_path);
        // Do not increment i; next element slid into index i.
      }
    } else {
      std::error_code ec;
      std::filesystem::remove(tmp_path, ec);
      total_unsolved++;
      Print(AORANGE("  ✘ Cell #{} NOT fully certified in {} (remaining stack: {}, shelved: {}). Retaining in {}.\n"),
            cell.id, ANSI::Time(cell_seconds), FormatNum(mgr.stack.size()),
            FormatNum(new_difficult.size()), difficult_path);
      i++;
    }
  }

  // Ensure state is cleanly persisted
  WriteDifficultFile(difficult_path, unsolved_cells);

  Print(ACYAN("\n=== Summary ===\n"));
  Print("Total processed: {}\n"
        "Total solved:    {}\n"
        "Total failed:    {}\n"
        "Remaining:       {} in {}\n"
        "Rows written:    {}\n"
        "Elapsed time:    {}\n",
        FormatNum(total_processed),
        FormatNum(total_solved),
        FormatNum(total_unsolved),
        FormatNum(unsolved_cells.size()), difficult_path,
        FormatNum(total_rows_written),
        ANSI::Time(total_timer.Seconds()));

  return 0;
}
