
#include <cstdio>
#include <format>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "blif.h"
#include "cell-library.h"
#include "chess.h"
#include "chessprop.h"
#include "crypt/sha256.h"
#include "periodically.h"
#include "process-util.h"
#include "prop.h"
#include "simplification.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"
#include "verilog.h"

static constexpr bool USE_ABC = true;
static constexpr bool ALWAYS_ABC = true;
static constexpr bool EXTENDED_EXDC = true;
static constexpr bool ABC_XAG = true;
static constexpr bool FINAL_SIMPLIFY = false;

static constexpr std::string_view GENLIB = "aoinx";

static constexpr bool SAMPLE_ONLY = true;

static bool SampleMove(Position::Move m) {
  if (!SAMPLE_ONLY) return true;

  // b2-b4
  if (m.src_row == 6 && m.src_col == 1 &&
      m.dst_row == 4 && m.dst_col == 1) return true;

  // Covers castling moves, etc.
  if (m.src_row == 7 && m.src_col == 4 &&
      m.src_row <= 7) return true;

  return false;
}

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

static Prop AtMostOne(const std::vector<Prop> &props) {
  Prop any_two = False();
  for (size_t i = 0; i < props.size(); i++) {
    for (size_t j = i + 1; j < props.size(); j++) {
      any_two = any_two | (props[i] & props[j]);
    }
  }
  return -any_two;
}

static Prop OptimizeABC(const World &world,
                        const Prop &prop,
                        const Prop &exdc) {
  if (!USE_ABC) return prop;

  std::set<int> var_set;
  for (int v : PropVars(prop)) var_set.insert(v);
  for (int v : PropVars(exdc)) var_set.insert(v);
  std::vector<std::string> inputs;
  for (int v : var_set) {
    inputs.push_back(world.symbol_names[v]);
  }

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

  std::string cmds;
  if (ABC_XAG) {
    cmds =
      "&get; "
      "&st; "
      "&syn4; &syn4; "
      "&put; "
      // Map to genlib, minimizing area
      "map -a; ";
  } else {
    cmds =
      "sweep; mfs -W 100 -M 10000; "
      "strash; "
      "compress2rs; compress2rs; compress2rs; "
      "dch; fraig; "
      "compress2rs; "
      "dch; fraig; "
      // aiger, but this only supports and/not
      // "write {}; "
      // "map" for depth/area. amap for area.
      "amap; ";
  }

  std::string cmdline =
    std::format("../../berkeley-abc/abc -c \""
                "source ../../berkeley-abc/abc.rc; "
                // "read_genlib aoinx.genlib; "
                "read_genlib {}.genlib; "
                "read_super {}.super; "
                "read_blif {}; "
                "{}"
                "print_stats; "
                "write_verilog {}; "
                // "write_eqn {}; "
                "\"",
                GENLIB, GENLIB,
                blif_filename,
                cmds,
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
  std::optional<Prop> opt = FromVerilog(world, verilog, inputs);
  CHECK(opt.has_value()) << verilog_filename;

  Util::RemoveFile(blif_filename);
  // Util::RemoveFile(verilog_filename);

  return std::move(opt.value());
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

  // On each square, exactly one of the one-hot types is true.
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      std::vector<Prop> square_props;
      for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
        square_props.push_back(board.props[ChessProp::HasContentsIdx(r, c, t)]);
      }
      valid = valid & ExactlyOne(square_props);
    }
  }

  if (EXTENDED_EXDC) {
    // At most one of the en passant columns is set.
    std::vector<Prop> ep_props;
    for (int c = 0; c < 8; c++) {
      ep_props.push_back(board.props[ChessProp::EnPassantColIdx(c)]);
    }
    valid = valid & AtMostOne(ep_props);

    // If an en passant column is set, the capturable black pawn must be
    // in the corresponding spot, and the two squares behind it must be
    // empty (because it just did a double pawn move).
    for (int c = 0; c < 8; c++) {
      Prop ep = board.props[ChessProp::EnPassantColIdx(c)];
      Prop bpawn = board.props[
          ChessProp::HasContentsIdx(3, c, ChessProp::BLACK_PAWN)];
      Prop empty1 = board.props[
          ChessProp::HasContentsIdx(2, c, ChessProp::EMPTY)];
      Prop empty2 = board.props[
          ChessProp::HasContentsIdx(1, c, ChessProp::EMPTY)];
      valid = valid & (-ep | (bpawn & empty1 & empty2));
    }

    // The black king is on exactly one of the 64 squares.
    {
      std::vector<Prop> bk_props;
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
          bk_props.push_back(board.props[
              ChessProp::HasContentsIdx(r, c, ChessProp::BLACK_KING)]);
        }
      }
      valid = valid & ExactlyOne(bk_props);
    }

    // The white king is on exactly one of the 64 squares.
    {
      std::vector<Prop> wk_props;
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
          wk_props.push_back(board.props[
              ChessProp::HasContentsIdx(r, c, ChessProp::WHITE_KING)]);
        }
      }
      valid = valid & ExactlyOne(wk_props);
    }

    // Neither color of pawn can be on the first or last rank.
    for (int c = 0; c < 8; c++) {
      for (int r : {0, 7}) {
        valid = valid & -board.props[
            ChessProp::HasContentsIdx(r, c, ChessProp::WHITE_PAWN)];
        valid = valid & -board.props[
            ChessProp::HasContentsIdx(r, c, ChessProp::BLACK_PAWN)];
      }
    }

    // Castling privileges imply the king and corresponding rook are
    // on their starting squares.
    valid = valid & (-board.props[ChessProp::CastlingIdx(true, true)] |
                     (board.props[ChessProp::HasContentsIdx(
                          7, 4, ChessProp::WHITE_KING)] &
                      board.props[ChessProp::HasContentsIdx(
                          7, 7, ChessProp::WHITE_ROOK)]));
    valid = valid & (-board.props[ChessProp::CastlingIdx(true, false)] |
                     (board.props[ChessProp::HasContentsIdx(
                          7, 4, ChessProp::WHITE_KING)] &
                      board.props[ChessProp::HasContentsIdx(
                          7, 0, ChessProp::WHITE_ROOK)]));
    valid = valid & (-board.props[ChessProp::CastlingIdx(false, true)] |
                     (board.props[ChessProp::HasContentsIdx(
                          0, 4, ChessProp::BLACK_KING)] &
                      board.props[ChessProp::HasContentsIdx(
                          0, 7, ChessProp::BLACK_ROOK)]));
    valid = valid & (-board.props[ChessProp::CastlingIdx(false, false)] |
                     (board.props[ChessProp::HasContentsIdx(
                          0, 4, ChessProp::BLACK_KING)] &
                      board.props[ChessProp::HasContentsIdx(
                          0, 0, ChessProp::BLACK_ROOK)]));
  }

  Prop exdc = -valid;
  exdc = BalanceProp(SimplifyProp(exdc));
  exdc = SimplifyProp(exdc);

  Prop opt = OptimizeABC(world, prop, exdc);

  [[maybe_unused]]
  size_t abc_size = PropSize(opt);
  [[maybe_unused]]
  size_t abc_shared_size = SharedPropSize(opt);

  // Print("ABC opt:\n{}\n\n", PropString(opt));

  Prop fin = FINAL_SIMPLIFY ? sim.Simplify(opt) : opt;
  size_t fin_size = PropSize(fin);
  size_t fin_shared_size = SharedPropSize(fin);

  // Print("Fin opt:\n{}\n\n", PropString(fin));

  const char *color =
    fin_shared_size == orig_shared_size ?
    ANSI_YELLOW :
    (fin_shared_size < orig_shared_size ?
     ANSI_GREEN : ANSI_RED);

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

  if (ALWAYS_ABC || fin_shared_size < orig_shared_size) {
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

  (void)Util::MakeDir("chess");

  std::mutex mu;
  int done = 0;
  ParallelComp2D(
      64, 64,
      [&](int src, int dst) {
        Position::Move m;
        m.src_row = src / 8;
        m.src_col = src % 8;
        m.dst_row = dst / 8;
        m.dst_col = dst % 8;

        if (SampleMove(m)) {
          Generate(library, sim, &status, m);
        }

        {
          MutexLock ml(&mu);
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

