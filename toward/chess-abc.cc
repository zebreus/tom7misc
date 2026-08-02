
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "timer.h"
#include "crypt/sha256.h"
#include "ansi.h"
#include "base/print.h"
#include "cell-library.h"
#include "chessprop.h"
#include "functional-map.h"
#include "prop.h"
#include "simplification.h"
#include "util.h"
#include "aiger.h"
#include "process-util.h"
#include "verilog.h"
#include "blif.h"
#include "threadutil.h"
#include "status-bar.h"
#include "periodically.h"

static Prop ExactlyOne(const std::vector<Prop> &props) {
  Prop any = False();
  Prop any_two = False();
  for (size_t i = 0; i < props.size(); i++) {
    for (size_t j = i + 1; j < props.size(); j++) {
      any_two = any_two | (props[i] & props[j]);
    }
    any = any | props[i];
  }
  return any & -any_two;
}

static void Generate(const CellLibrary &library,
                     const Simplification &sim,
                     StatusBar *status,
                     Position::Move m) {
  // Print("Init...\n");
  Timer timer;
  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);

  // Print("Get chess prop...\n");
  // fflush(stdout);
  Prop prop = ChessProp::IsLegal(board,
                                 m.src_row, m.src_col,
                                 m.dst_row, m.dst_col,
                                 ChessProp::REAL_CHESS);

  [[maybe_unused]] size_t start_size = PropSize(prop);
  [[maybe_unused]] size_t start_shared_size = PropSize(prop);
  // Print("Starting size: {} ({} shared)\n",
  // start_size, start_shared_size);

  prop = BalanceProp(SimplifyProp(prop));
  prop = SimplifyProp(prop);
  // Print("Simplify...\n");
  prop = sim.Simplify(prop);
  [[maybe_unused]]
  size_t orig_size = PropSize(prop);
  [[maybe_unused]]
  size_t orig_shared_size = SharedPropSize(prop);


  auto Output = [&](const Prop &p) {
      std::string outfile = std::format("chess/islegal-{}-{}.prop",
                                        ChessProp::Square(m.src_row,
                                                          m.src_col),
                                        ChessProp::Square(m.dst_row,
                                                          m.dst_col));
      Util::WriteFile(outfile, SerializeProp(p));
    };

  if (prop == False()) {
    status->Print("[{}-{}] " AGREY("Trivially false") ".\n",
                  ChessProp::Square(m.src_row, m.src_col),
                  ChessProp::Square(m.dst_row, m.dst_col));

    Output(prop);
    return;
  }

  // Print("Create exdc (don't care) condition...\n");
  Prop valid = True();
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      std::vector<Prop> square_props;
      for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
        square_props.push_back(board.props[ChessProp::HasContentsIdx(r, c, t)]);
      }
      valid = valid & ExactlyOne(square_props);
    }
  }
  Prop exdc = -valid;
  exdc = BalanceProp(SimplifyProp(exdc));
  exdc = SimplifyProp(exdc);

  // Print("Write blif...\n");
  std::string contents = ToBLIF("chess", world, prop, exdc);
  std::string sha = SHA256::Ascii(SHA256::HashStringView(contents));
  std::string blif_filename = std::format("chess-{}.blif", sha);

  Util::WriteFile(blif_filename, contents);

  std::string aiger_filename = std::format("chess-{}.aig", sha);
  (void)Util::RemoveFile(aiger_filename);

  std::string verilog_filename = std::format("chess-{}.v", sha);
  (void)Util::RemoveFile(verilog_filename);

  std::string eqn_filename = std::format("chess-{}.eqn", sha);
  (void)Util::RemoveFile(eqn_filename);

  std::string cmdline =
    std::format("../../berkeley-abc/abc -c \""
                "source ../../berkeley-abc/abc.rc; "
                "read_genlib and-or-not.genlib; "
                "read_blif {}; "
                "sweep; mfs -W 100 -M 10000; "
                "strash; "
                "compress2rs; compress2rs; compress2rs; "
                "dch; fraig; "
                "compress2rs; "
                "dch; fraig; "
                // aiger, but this only supports and/not
                // "write {}; "
                "map; "
                "print_stats; "
                "write_verilog {}; "
                // "write_eqn {}; "
                "\"",
                blif_filename,
                verilog_filename);

  Timer abc_timer;
  // Print("Run abc...\n");
  // Print(ABLUE("{}") "\n", cmdline);
  std::optional<std::string> abc_out = ProcessUtil::GetOutput(cmdline);
  [[maybe_unused]] double abc_sec = abc_timer.Seconds();
  // Print("Ran abc in {}\n", ANSI::Time(abc_sec));
  CHECK(abc_out.has_value());
  // Print(AGREY("{}") "\n", abc_out.value());

  // Success if the file appears!
  // std::string aiger = Util::ReadFile(aiger_filename);
  // CHECK(!aiger.empty()) << aiger_filename;
  // std::optional<Prop> opt = FromAIGER(aiger);

  std::string verilog = Util::ReadFile(verilog_filename);
  CHECK(!verilog.empty()) << verilog_filename;
  std::optional<Prop> opt = FromVerilog(verilog);
  CHECK(opt.has_value()) << verilog_filename;

  [[maybe_unused]]
  size_t abc_size = PropSize(opt.value());
  [[maybe_unused]]
  size_t abc_shared_size = SharedPropSize(opt.value());

  // Print("ABC opt:\n{}\n\n", PropString(opt.value()));

  Prop fin = sim.Simplify(opt.value());
  size_t fin_size = PropSize(fin);
  size_t fin_shared_size = SharedPropSize(fin);

  // Print("Fin opt:\n{}\n\n", PropString(fin));

  Util::RemoveFile(blif_filename);
  Util::RemoveFile(verilog_filename);

  const char *color =
    fin_shared_size < orig_shared_size ?
    ANSI_GREEN : ANSI_RED;

  status->Print("[{}-{}] {} "
                "Orig: {} ({} shared) → "
                "abc: {} ({} shared) → "
                "{}fin: {} ({} shared)" ANSI_RESET "\n",
                ChessProp::Square(m.src_row, m.src_col),
                ChessProp::Square(m.dst_row, m.dst_col),
                ANSI::Time(timer.Seconds()),
                orig_size, orig_shared_size,
                abc_size, abc_shared_size,
                color,
                fin_size, fin_shared_size);

  if (fin_shared_size < orig_shared_size) {
    Output(fin);
  } else {
    Output(prop);
  }

}

static void Generate() {
  CellLibrary library;
  Simplification sim;
  StatusBar status(1);
  Timer timer;
  Periodically status_per(1);

  std::mutex m;
  int done = 0;
  ParallelComp2D(
      64, 64,
      [&](int src, int dst) {
        Position::Move m;
        m.src_row = src / 8;
        m.src_col = src % 8;
        m.dst_row = dst / 8;
        m.dst_col = dst % 8;

        Generate(library, sim, &status, m);

        {
          MutexLock ml(&m);
          done++;
        }
        status_per.RunIf([&]{
            status.Progress(done, 64 * 64, "chessing");
          });
      },
      12);

  Print("Done in {}!\n", ANSI::Time(timer.Seconds()));
}

int main(int argc, char **argv) {
  ANSI::Init();
  Generate();
  Print("OK\n");
  return 0;
}

