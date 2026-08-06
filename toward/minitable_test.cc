
#include "minitable.h"

#include <cstdint>
#include <optional>
#include <tuple>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "prop.h"
#include "timer.h"

static void TestFunctionallyComplete(const MiniTable &mt) {
  // Spot check to keep the test fast.
  for (int i = 0; i < 65536; i += 257) {
    uint16_t fn = (uint16_t)i;
    Prop p = mt.Minimal(0, 1, 2, 3, fn);
    uint16_t eval = MiniTable::Eval(p);
    CHECK(eval == fn) << "Eval should match requested fn.";
  }
}

static void TestGetQuadReconstruction(const MiniTable &mt) {
  // Pick a truth table that depends on multiple variables.
  uint16_t fn = 0xAA55;
  Prop p = mt.Minimal(2, 4, 6, 8, fn);

  std::optional<std::tuple<int, int, int, int, uint16_t>> quad =
      MiniTable::GetQuad(p);
  CHECK(quad.has_value()) << "GetQuad should succeed on minimal prop.";

  auto [v0, v1, v2, v3, tt] = quad.value();

  Prop reconstructed = mt.Minimal(v0, v1, v2, v3, tt);
  std::optional<std::tuple<int, int, int, int, uint16_t>> quad2 =
      MiniTable::GetQuad(reconstructed);

  CHECK(quad2.has_value()) << "Reconstructed should succeed.";

  auto [rv0, rv1, rv2, rv3, rtt] = quad2.value();
  CHECK(rtt == tt) << "Truth table should be preserved under reconstruction.";
}

static void TestConstantProperties(const MiniTable &mt) {
  // A truth table that evaluates to True everywhere.
  uint16_t fn = 0xFFFF;
  Prop p = mt.Minimal(10, 11, 12, 13, fn);

  std::optional<std::tuple<int, int, int, int, uint16_t>> quad =
      MiniTable::GetQuad(p);
  CHECK(quad.has_value()) << "GetQuad should succeed.";

  auto [v0, v1, v2, v3, tt] = quad.value();
  CHECK(tt == 0xFFFF) << "Constant function should retain its truth table.";
}

static void TestBinopProperties(const MiniTable &mt) {
  auto CheckBinop = [](const Prop& p, BinopOp expected_op) {
      CHECK(PropSize(p) == 3)
        << "Minimal binop should just be the node itself.";
      const Binop *b = std::get_if<Binop>(&p.p);
      CHECK(b != nullptr) << "Expected a Binop";
      CHECK(b->op == expected_op);
      CHECK(b->a != nullptr && b->b != nullptr);
      const Var *v1 = std::get_if<Var>(&b->a->p);
      const Var *v2 = std::get_if<Var>(&b->b->p);
      CHECK(v1 != nullptr && v2 != nullptr);
      CHECK(v1->id != v2->id);
    };

  uint16_t and_fn = MiniTable::Eval(Prop{Var{.id = 0}} & Prop{Var{.id = 1}});
  Prop p_and = mt.Minimal(0, 1, 2, 3, and_fn);
  CheckBinop(p_and, BinopOp::AND);

  uint16_t or_fn = MiniTable::Eval(Prop{Var{.id = 0}} | Prop{Var{.id = 1}});
  Prop p_or = mt.Minimal(0, 1, 2, 3, or_fn);
  CheckBinop(p_or, BinopOp::OR);

  uint16_t nand_fn = MiniTable::Eval(
      Nand(Prop{Var{.id = 0}}, Prop{Var{.id = 1}}));
  Prop p_nand = mt.Minimal(0, 1, 2, 3, nand_fn);
  CheckBinop(p_nand, BinopOp::NAND);

  uint16_t nor_fn = MiniTable::Eval(
      Nor(Prop{Var{.id = 0}}, Prop{Var{.id = 1}}));
  Prop p_nor = mt.Minimal(0, 1, 2, 3, nor_fn);
  CheckBinop(p_nor, BinopOp::NOR);
}

static void TestIteProperties(const MiniTable &mt_no_ite) {
  Print("Test ITE...\n");
  Timer timer;
  MiniTable mt_ite(MiniTable::OPT_AND | MiniTable::OPT_NOT | MiniTable::OPT_OR |
                   MiniTable::OPT_ITE);
  Print("ITE minitable in {}\n", ANSI::Time(timer.Seconds()));

  TestFunctionallyComplete(mt_ite);

  uint16_t ite_fn = MiniTable::Eval(Ite(Prop{Var{.id = 0}}, Prop{Var{.id = 1}},
                                        Prop{Var{.id = 2}}));

  Prop p_ite = mt_ite.Minimal(0, 1, 2, 3, ite_fn);
  Prop p_no_ite = mt_no_ite.Minimal(0, 1, 2, 3, ite_fn);

  CHECK(PropSize(p_ite) < PropSize(p_no_ite))
      << "ITE should provide a more compact representation.";

  CHECK(PropSize(p_ite) == 4)
      << "Minimal ITE should just be the ITE node itself.";
  Print("ITE OK in {}\n", ANSI::Time(timer.Seconds()));
}

int main(int argc, char **argv) {
  ANSI::Init();

  Print("Create minitable...\n");
  Timer timer;
  // MiniTable::OPT_XOR is not used in our programs
  MiniTable mt(MiniTable::OPT_AND | MiniTable::OPT_NOT | MiniTable::OPT_OR |
               MiniTable::OPT_NAND | MiniTable::OPT_NOR);
  Print("Minitable created in {}.\n", ANSI::Time(timer.Seconds()));

  TestFunctionallyComplete(mt);
  TestGetQuadReconstruction(mt);
  TestConstantProperties(mt);
  TestBinopProperties(mt);
  TestIteProperties(mt);

  Print("OK\n");
  return 0;
}

