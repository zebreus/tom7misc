
// Optimize a choppy database to make smaller equivalent expressions.
// How did I not call this choptimize??

#include <optional>
#include <array>
#include <utility>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "expression.h"
#include "half.h"
#include "hashing.h"

#include "choppy.h"
#include "grad-util.h"
#include "color-util.h"
#include "arcfour.h"
#include "ansi.h"
#include "threadutil.h"
#include "timer.h"
#include "periodically.h"
#include "opt/large-optimizer.h"
#include "diff.h"

#include "state.h"

using Choppy = ChoppyGrid<256>;
using DB = Choppy::DB;
using Allocator = Exp::Allocator;
using Table = Exp::Table;
using namespace std;

static constexpr int MAX_THREADS = 8;

static double Cost(const Exp *e) {
  static constexpr double NODE = 100000.0;
  switch (e->type) {
  case VAR:
    return NODE;
  case PLUS_C:
  case TIMES_C:
    // Can be deleted.
    if (e->iters == 0)
      return 0.01 + Cost(e->a);

    if (e->a->type == e->type &&
        e->c == e->a->c) {
      // Can be fused (unless too many iters).
      return 0.1 + e->iters + Cost(e->a);
    } else {
      return NODE + e->iters + Cost(e->a);
    }
  case PLUS_E:
    return NODE + Cost(e->a) + Cost(e->b);
  default:
    CHECK(false);
    return 0.0;
  }
};

static int NumParameters(const Exp *e) {
  switch (e->type) {
  case VAR:
    return 0;
  case PLUS_C:
  case TIMES_C:
    // constant and iterations.
    return 2 + NumParameters(e->a);
  case PLUS_E: {
    // Include the possibility of dropping one child?
    return NumParameters(e->a) + NumParameters(e->b);
  }
  default:
    CHECK(false);
    return 0;
  }
}

static int64 TotalDepth(const Exp *e) {
  int64 depth = 0;
  std::function<void(const Exp *)> Rec = [&Rec, &depth](const Exp *e) {
      depth++;
      switch (e->type) {
      case VAR:
        return;
      case PLUS_C:
        Rec(e->a);
        return;
      case TIMES_C:
        Rec(e->a);
        return;
      case PLUS_E: {
        Rec(e->a);
        Rec(e->b);
        return;
      }
      default:
        CHECK(false);
        return;
      }
    };

  Rec(e);
  return depth;
}

// Jitter each constant and iters independently.
struct JointOptArgs1 {
  using LO = LargeOptimizer<false>;
  std::vector<LO::arginfo> arginfos;
  std::vector<double> cur;

  JointOptArgs1(const Exp *exp) {
    GetArgs(exp);
  }

  void GetArgs(const Exp *e) {
    switch (e->type) {
    case VAR: return;
    case PLUS_C:
    case TIMES_C:
      // Removes nans/infs at the high end. But maybe should
      // reorder to avoid all of them?
      // Only search a small region around the current best.
      arginfos.push_back(LO::Integer(0, 0xfc00, -8, +8));
      cur.push_back((double)e->c);
      // Allow more downward movement than up.
      arginfos.push_back(LO::Integer(0, 0x10000, -16, +6));
      cur.push_back((double)e->iters);
      GetArgs(e->a);
      return;
    case PLUS_E:
      GetArgs(e->a);
      GetArgs(e->b);
      return;
    default:
      CHECK(false);
      return;
    }
  }


  const Exp *SetArgs(Allocator *alloc,
                     const Exp *exp,
                     const std::vector<double> &args) {
    int idx = 0;
    std::function<const Exp *(const Exp *)> Rec =
      [alloc, &args, &idx, &Rec](const Exp *old) {
        switch (old->type) {
        case VAR: return old;
        case PLUS_C: {
          CHECK(idx < args.size() - 1);
          uint16_t c = args[idx++];
          uint16_t iters = args[idx++];
          const Exp *a = Rec(old->a);
          return alloc->PlusC(a, c, iters);
        }
        case TIMES_C: {
          CHECK(idx < args.size() - 1);
          uint16_t c = args[idx++];
          uint16_t iters = args[idx++];
          const Exp *a = Rec(old->a);
          return alloc->TimesC(a, c, iters);
        }
        case PLUS_E: {
          const Exp *a = Rec(old->a);
          const Exp *b = Rec(old->b);
          return alloc->PlusE(a, b);
        }
        default:
          CHECK(false);
          return old;
        }
      };

    const Exp *ret = Rec(exp);
    CHECK(idx == args.size());
    return ret;
  }

};

// Group by constant so that all occurrences are optimized together.
struct JointOptArgs2 {
  using LO = LargeOptimizer<false>;
  std::vector<LO::arginfo> arginfos;
  std::vector<double> cur;

  // Same size as arginfos.
  // For an argument index, the locations in the in-order traversal
  // where it occurs. bool indicates whether this is iters (true)
  // or constant (false).
  std::vector<std::pair<bool, std::vector<int>>> locations;
  // Number of positions (both constant/iters) being optimized.
  int num_flat = 0;

  JointOptArgs2(const Exp *exp) {
    int next_index = 0;
    std::map<std::pair<uint16_t, uint16_t>,
             std::vector<int>> constants;
    CollateArgs(exp, &next_index, &constants);

    for (const auto &[k, v] : constants) {
      const auto &[c, iters] = k;
      // For each, two optimization arguments.
      locations.emplace_back(false, v);
      arginfos.push_back(LO::Integer(0, 0xfc00, -8, +8));
      cur.push_back(c);

      locations.emplace_back(true, v);
      arginfos.push_back(LO::Integer(0, 0x10000, -16, +6));
      cur.push_back(iters);

      num_flat += v.size();
    }

    CHECK(locations.size() == arginfos.size());
    CHECK(locations.size() == cur.size());
  }

  const Exp *SetArgs(Allocator *alloc,
                     const Exp *exp,
                     const std::vector<double> &collated_args) {

    // Put back in flat order.
    CHECK(locations.size() == collated_args.size());
    std::vector<std::pair<uint16_t, uint16_t>> args(num_flat,
                                                    make_pair(0, 0));
    for (int i = 0; i < (int)collated_args.size(); i++) {
      uint16_t val = collated_args[i];
      const auto &[is_iters, vec] = locations[i];
      for (int loc : vec) {
        CHECK(loc >= 0 && loc < args.size());
        if (is_iters) args[loc].second = val;
        else args[loc].first = val;
      }
    }

    int idx = 0;
    std::function<const Exp *(const Exp *)> Rec =
      [alloc, &args, &idx, &Rec](const Exp *old) {
        switch (old->type) {
        case VAR: return old;
        case PLUS_C: {
          CHECK(idx < args.size());
          uint16_t c = args[idx].first;
          uint16_t iters = args[idx].second;
          idx++;
          const Exp *a = Rec(old->a);
          return alloc->PlusC(a, c, iters);
        }
        case TIMES_C: {
          CHECK(idx < args.size());
          uint16_t c = args[idx].first;
          uint16_t iters = args[idx].second;
          idx++;
          const Exp *a = Rec(old->a);
          return alloc->TimesC(a, c, iters);
        }
        case PLUS_E: {
          const Exp *a = Rec(old->a);
          const Exp *b = Rec(old->b);
          return alloc->PlusE(a, b);
        }
        default:
          CHECK(false);
          return old;
        }
      };

    const Exp *ret = Rec(exp);
    CHECK(idx == args.size()) << idx << " " << args.size();
    return ret;
  }

 private:
  // Map from a constant/iters (in the original expression) to
  // its indices in an in-order traversal.
  void CollateArgs(const Exp *e,
                   int *next_index,
                   std::map<std::pair<uint16_t, uint16_t>,
                   std::vector<int>> *constants) {
    switch (e->type) {
    case VAR: return;
    case PLUS_C:
    case TIMES_C:
      (*constants)[make_pair(e->c, e->iters)].push_back(*next_index);
      ++*next_index;
      CollateArgs(e->a, next_index, constants);
      return;
    case PLUS_E:
      CollateArgs(e->a, next_index, constants);
      CollateArgs(e->b, next_index, constants);
      return;
    default:
      CHECK(false);
      return;
    }
  }

};


/*
// Excludes -0.
static constexpr int CHOPTABLE_SIZE = (0x3c00 + 1) * 2 - 1;
using ChopTable = std::array<uint16_t, CHOPTABLE_SIZE>;

// Fill from -1 to 1, inclusive, skipping -0.
static void FillChopTable(const Exp *e, ChopTable *table) {
  int idx = 0;
  for (uint16 upos = 0x8001; upos <= 0xbc00; upos++)
    (*table)[idx++] = Exp::EvaluateOn(e, upos);
  for (uint16 upos = 0x0000; upos <= 0x3c00; upos++)
    (*table)[idx++] = Exp::EvaluateOn(e, upos);
  CHECK(idx == CHOPTABLE_SIZE) << idx;
}
*/

#if 0
// LO must be negative, HI positive.
template<class F, uint16_t LO, uint16_t HI, uint16_t STRIDE>
static inline void ForNegToPosAscending(F f) {
  // Negative
  for (int u = LO; u >= GradUtil::NEG_LOW; u -= STRIDE)
    f((uint16)u);
  // Positive
  for (int u = GradUtil::POS_LOW; u <= HI; u += STRIDE)
    f((uint16)u);
}
#endif

// XXX better would be to take the size of the correct interval around
// zero
#if 0
static int64_t ScoreZT(const Exp *e) {
  // static constexpr uint16_t LOW = 0xfbff; // min finite
  // static constexpr uint16_t HIGH = 0x7bff; // max finite

  int num_wrong = 0;
  for (uint16 uneg = 0x8001; uneg <= 0xfbff; uneg++) {
    uint16_t uo = Exp::EvaluateOn(e, uneg);
    // Should be zero (or negative zero) for this range.
    if (uo != 0x0000 && uo != 0x8000) num_wrong++;
  }
  for (uint16 upos = 0x0000; upos <= 0x7bff; upos++) {
    // Should be one for this range.
    uint16_t uo = Exp::EvaluateOn(e, upos);
    if (uo != 0x3c00) num_wrong++;
  }

  return num_wrong;
}
#endif

// -size of correct interval around zero
static int64_t ScoreZT(const Exp *e) {
  // Could also do min of pos,neg?
  int num_neg_correct = 0, num_pos_correct = 0;
  for (uint16 uneg = 0x8001; uneg <= 0xfbff; uneg++) {
    uint16_t uo = Exp::EvaluateOn(e, uneg);
    // Should be zero (or negative zero) for this range.
    if (uo == 0x0000 || uo == 0x8000) num_neg_correct++;
    else break;
  }
  for (uint16 upos = 0x0000; upos <= 0x7bff; upos++) {
    // Should be one for this range.
    uint16_t uo = Exp::EvaluateOn(e, upos);
    if (uo == 0x3c00) num_pos_correct++;
    else break;
  }

  return -(num_neg_correct + num_pos_correct);
}


template<class JointOptArg>
static const Exp *JointOpt(Allocator *alloc,
                           const Exp *exp,
                           double sec,
                           uint64_t seed = 0xCAFEBABE) {
  JointOptArg jo(exp);
  const int n = jo.arginfos.size();

  using LO = LargeOptimizer<false>;

  double start_score = 0.0/0.0;
  auto Score = [&](const LO::arg_type &args) {
      Allocator alloc;
      const Exp *e2 = jo.SetArgs(&alloc, exp, args);
      int errors = ScoreZT(e2);
      return std::make_pair((double)errors, true);
    };

  LO opt(Score, n, seed);
  opt.Sample(jo.cur);
  auto ostart = opt.GetBest();
  CHECK(ostart.has_value());
  start_score = ostart.value().second;

  opt.Run(jo.arginfos, nullopt, nullopt, {sec},
          nullopt,
          8);
  auto oend = opt.GetBest();
  CHECK(oend.has_value());
  double end_score = oend.value().second;

  printf("Ran " APURPLE("%lld") " evaluations\n",
         opt.NumEvaluations());
  if (end_score < start_score) {
    printf(AGREEN("JointOpt reduced!") " From "
           ACYAN("%.3f") " -> " ABLUE("%.3f") "\n",
           start_score, end_score);

    const Exp *opt_exp = jo.SetArgs(alloc, exp, oend.value().first);

    const auto [before, after] = ColorDiff(exp, opt_exp);
    printf(AWHITE("Was") ": %s" ANSI_RESET "\n", before.c_str());
    printf(AWHITE("Now") ": %s" ANSI_RESET "\n", after.c_str());

    return opt_exp;
  } else {
    return exp;
  }
}


int main(int argc, char **argv) {
  AnsiInit();
  Timer run_timer;

  Allocator alloc;

  // Note I removed the T30001 at the end, so this outputs 1, not 1/8.
  // static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c051 T3c01539 T39031 T3c011160 T35421 T3c0423 T39df1 T3c01137 T371e1 T3c01365 T39e61 T3c03346 T39a21 T3c01676 T38631 T3c01550 T39081 T3c01830 T32991 T3c01336 T3a053 T3c01666 T1f111 Pe94b1 P694a1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001";

  static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c010 P5fff1 T44001 P3c051 T3c01539 T39071 T3c011160 T35421 T3c0423 T39df1 T3c01137 T371e1 T3c03365 T39e61 T3c03346 T39a21 T3c01676 T385c1 T3c01550 T39081 T3c01830 T32991 T3c01336 T3a053 T3c01671 T1f111 Pe94b1 P694a1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001";

  const Exp *exp = Exp::Deserialize(&alloc, ZERO_THRESHOLD);

  const int64_t orig_score = ScoreZT(exp);
  printf("Orig score: " ABLUE("%lld") "\n", orig_score);

  const Exp *improved = JointOpt<JointOptArgs1>(
      &alloc, exp, 15.0 * 60.0, (uint64_t)time(nullptr));

  Util::WriteFile("zt-improved.txt",
                  Exp::Serialize(improved));

  const int64_t new_score = ScoreZT(improved);
  printf("Orig score: " ARED("%lld") "\n", orig_score);
  printf("New score: " AGREEN("%lld") "\n", new_score);

  /*
  printf("Done in " AYELLOW("%.3f") "s\n", run_timer.Seconds());
  if (total_in == total_out) {
    printf(AGREY("Size: %lld. No change.") "\n", total_in);
  } else {
    printf(AGREEN("Reduced!")
           " Toal from " ACYAN("%lld") " -> " ABLUE("%lld")
           "  (" APURPLE("%.2f%%") ")\n",
           total_in, total_out,
           (100.0 * (total_in - total_out)) / total_in);
  }
  */

  printf(AGREEN("OK") "\n");
  return 0;
}
