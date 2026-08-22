
#include <cstdio>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
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

static constexpr bool SAMPLE_ONLY = true;

static constexpr bool USE_ABC = true;
static constexpr bool ALWAYS_ABC = false;
static constexpr bool ABC_XAG = true;
static constexpr bool FINAL_SIMPLIFY = false;

static constexpr std::string_view GENLIB = "aoinrq";

/*
  Goal for this program:

  I'm trying to find a bug where optimizing my proposition with ABC
  changes its meaning. It could be a bug in abc or the way I'm using
  it. It only appears to happen with the castling proposition
  (chessprop.cc). I check that the propositions correctly implement
  the chess rules in chess-stats.cc. I've verified that the input
  and output propositions are unequal in chess-abc (by exporting
  a sat problem to Z3).
*/

static bool PropsAgree(const Prop &before, const Prop &after) {
  ArcFour rc("chess-abc-bug");
  std::vector<Position> pool = ChessProp::LegalPositions(&rc, 1000);

  for (const Position &pos : pool) {
    ChessProp::Board board = ChessProp::BoardFromPosition(pos);
    std::vector<bool> assignments;
    assignments.reserve(board.props.size());
    for (const Prop &p : board.props) {
      assignments.push_back(std::get<Value>(p.p).value);
    }

    if (EvaluateProp(assignments, before) != EvaluateProp(assignments, after)) {
      return false;
    }
  }

  return true;
}

static bool SampleMove(Position::Move m) {
  if (!SAMPLE_ONLY) return true;

  // The bug happens with castling, so just sample
  // kingside castling to try to isolate a test case.

  // Just kingside castle
  if (m.src_row == 7 && m.src_col == 4 &&
      m.dst_row == 7 && m.dst_col == 2) return true;

  return false;
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
      "&syn4; &fraig; &syn4; "

      // sloooo
      "&mfs -W 100 -M 10000; "

      // extra juice
      "&fraig; "
      "&syn4; "

      "&put; "

      // Also give it options from the original circuit
      "&dch; "

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

  auto Error = [&]() -> std::string {
      if (abc_out.has_value()) {
        return std::format("\nABC output:\n{}\n", abc_out.value());
      } else {
        return "\n(no ABC output)\n";
      }
    };

  // Print("Ran abc in {}\n", ANSI::Time(abc_sec));
  CHECK(abc_out.has_value());
  // Print(AGREY("{}") "\n", abc_out.value());

  // Success if the file appears!
  // std::string aiger = Util::ReadFile(aiger_filename);
  // CHECK(!aiger.empty()) << aiger_filename;
  // std::optional<Prop> opt = FromAIGER(aiger);

  std::string verilog = Util::ReadFile(verilog_filename);
  CHECK(!verilog.empty()) << verilog_filename << Error();
  std::optional<Prop> opt = FromVerilog(world, verilog, inputs);
  CHECK(opt.has_value()) << verilog_filename << Error();

  Util::RemoveFile(blif_filename);
  // Util::RemoveFile(verilog_filename);

  return std::move(opt.value());
}

static Prop RandomShrink(const Prop &prop, ArcFour *rc) {
  std::map<Prop, Prop> cache;
  auto Rec = [&](auto &self, const Prop &p) -> Prop {
    if (auto it = cache.find(p); it != cache.end()) return it->second;
    Prop ret;

    if (rc->Byte() < 10) {
      ret = (rc->Byte() & 1) ? True() : False();
    } else {
      std::visit([&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Value> || std::is_same_v<T, Var>) {
          ret = p;
        } else if constexpr (std::is_same_v<T, Unop>) {
          if (rc->Byte() < 10) ret = self(self, *v.a);
          else ret = Prop{
              .p = Unop{v.op,
                std::make_shared<Prop>(self(self, *v.a))}
            };
        } else if constexpr (std::is_same_v<T, Binop>) {
          if (rc->Byte() < 15) ret = self(self, *v.a);
          else if (rc->Byte() < 15) ret = self(self, *v.b);
          else ret = Prop{
              .p = Binop{v.op,
                std::make_shared<Prop>(self(self, *v.a)),
                std::make_shared<Prop>(self(self, *v.b))}
            };
        } else if constexpr (std::is_same_v<T, Ternop>) {
          if (rc->Byte() < 10) ret = self(self, *v.a);
          else if (rc->Byte() < 10) ret = self(self, *v.b);
          else if (rc->Byte() < 10) ret = self(self, *v.c);
          else ret = Prop{
              .p = Ternop{v.op,
                std::make_shared<Prop>(self(self, *v.a)),
                std::make_shared<Prop>(self(self, *v.b)),
                std::make_shared<Prop>(self(self, *v.c))}
            };
        }
      }, p.p);
    }
    ret = SimplifyProp(ret);
    cache[p] = ret;
    return ret;
  };
  return SimplifyProp(Rec(Rec, prop));
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
      std::string outfile = std::format("chessbug/islegal-{}-{}.prop",
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

  Prop opt = OptimizeABC(world, prop, True());
  if (PropsAgree(prop, opt)) {
    status->Print("[{}-{}] Bug did not reproduce on initial proposition.\n",
                  ChessProp::Square(m.src_row, m.src_col),
                  ChessProp::Square(m.dst_row, m.dst_col));
    return;
  }

  Prop best_prop = prop;
  size_t best_size = SharedPropSize(best_prop);
  status->Print("[{}-{}] Initial buggy size: {} shared\n",
                ChessProp::Square(m.src_row, m.src_col),
                ChessProp::Square(m.dst_row, m.dst_col),
                best_size);

  ArcFour rc("shrink");
  Periodically write_per(10.0);
  Periodically print_per(1.0);
  size_t last_written_size = SIZE_MAX;

  for (int iter = 0; ; iter++) {
    if (write_per.ShouldRun()) {
      if (best_size < last_written_size) {
        Output(best_prop);
        last_written_size = best_size;
      }
    }

    if (print_per.ShouldRun()) {
      status->Status("Iter {}: best size {} shared\n", iter, best_size);
    }

    Prop candidate = RandomShrink(best_prop, &rc);
    size_t cand_size = SharedPropSize(candidate);

    if (cand_size >= best_size) {
      continue;
    }

    Prop cand_opt = OptimizeABC(world, candidate, True());
    if (!PropsAgree(candidate, cand_opt)) {
      best_prop = candidate;
      best_size = cand_size;
      status->Print("Iter {}: New best size: " AGREEN("{}")
                    " shared\n", iter, best_size);
      if (PropSize(candidate) < 128 && PropSize(cand_opt) < 128) {
        status->Print(AGREEN("{}") "  " APURPLE("→") "  " ARED("{}") "\n",
                      PropString(world, candidate),
                      PropString(world, cand_opt));
      }
    }
  }
}

static void Generate() {
  CellLibrary library;
  Simplification sim;
  StatusBar status(1);
  Timer timer;
  Periodically status_per(1);

  (void)Util::MakeDir("chessbug");

  Position::Move m;
  m.src_row = 7;
  m.src_col = 4;
  m.dst_row = 7;
  m.dst_col = 2;

  Generate(library, sim, &status, m);
}

int main(int argc, char **argv) {
  ANSI::Init();
  Generate();
  Print("OK\n");
  return 0;
}

