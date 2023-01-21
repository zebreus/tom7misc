
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

// Other keys will work, but we generally optimize bases, so
// pipe up if something is unexpected.
static constexpr bool REQUIRE_BASIS = false;

// Don't optimize expressions smaller than this
static constexpr int MIN_OPT = 1; // 000;

static constexpr int MAX_THREADS = 8;

// TODO: Guard more optimizations with flags.
static constexpr uint64_t OPT_JOINT1 = (1ULL << 1);
static constexpr uint64_t OPT_JOINT2 = (1ULL << 2);
static constexpr uint64_t OPT_REDUCEITERS = (1ULL << 3);
static constexpr uint64_t OPT_DROPNODE = (1ULL << 4);

static constexpr uint64_t OPTIMIZATIONS_ENABLED =
   OPT_JOINT1 | OPT_JOINT2;

static void MaybeSaveDB(DB *db) {
  static std::mutex *m = new std::mutex;
  static Periodically *save_per = new Periodically(60.0);

  {
    std::unique_lock<std::mutex> ml(*m);
    if (save_per->ShouldRun()) {
      Util::WriteFile("optimized-checkpoint.txt", db->Dump());
      printf("Wrote optimized checkpoint.\n");
    }
  }
}


// Reduce iterations multiplicatively.
// Sometimes there are way too many iterations, and it would
// be faster to use binary search to find the minimal number.
// This is hard to arrange with the "depth" concept. So this
// is basically just like the DropNode pass, but it reduces
// the number of iterations by a multiplicative factor,
// rather than one at a time.
struct ReduceIters {
  const std::string Name() const { return "ReduceIters"; }

  const Exp *Run(Allocator *alloc_, const Exp *e) {
    alloc = alloc_;
    did_drop = false;
    depth = 0;
    // idea is to keep some secondary part of the cursor,
    // like the lower/upper bound?
    return Rec(e);
  }

  bool Done() const { return !did_drop; }

  string CurrentDepth() const {
    return StringPrintf("%lld", stop_depth);
  }
  void NextDepth() { stop_depth++; }

  const Exp *Rec(const Exp *e) {
    if (depth == stop_depth) {
      // Drop the current node.
      did_drop = true;
      switch (e->type) {
      case VAR:
        return e;
      case PLUS_C:
        if (e->iters > 1) {
          int new_iters = std::max(1, (int)std::round(e->iters * 0.90));
          return alloc->PlusC(e->a, e->c, new_iters);
        } else {
          return e;
        }
        break;
      case TIMES_C:
        CHECK(e->iters != 0);
        if (e->iters > 1) {
          int new_iters = std::max(1, (int)std::round(e->iters * 0.90));
          return alloc->TimesC(e->a, e->c, new_iters);
        } else {
          return e;
        }
        break;
      case PLUS_E:
        return e;
      default:
        CHECK(false);
        return nullptr;
      }

    } else {
      // Go deeper.
      depth++;
      switch (e->type) {
      case VAR:
        return e;
      case PLUS_C:
        return alloc->PlusC(Rec(e->a), e->c, e->iters);
      case TIMES_C:
        return alloc->TimesC(Rec(e->a), e->c, e->iters);
      case PLUS_E: {
        const Exp *ea = Rec(e->a);
        const Exp *eb = Rec(e->b);
        return alloc->PlusE(ea, eb);
      }
      default:
        CHECK(false);
        return nullptr;
      }
    }
  }

private:
  Allocator *alloc = nullptr;
  int64 depth = 0;
  int64 stop_depth = 0;
  bool did_drop = false;
};

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

struct DropNode {
  const std::string Name() const { return "DropNode"; }

  const Exp *Run(Allocator *alloc_, const Exp *e) {
    alloc = alloc_;
    did_drop = false;
    depth = 0;
    return Rec(e);
  }

  bool Done() const { return !did_drop; }

  string CurrentDepth() const {
    return StringPrintf("%lld", stop_depth);
  }
  void NextDepth() { stop_depth++; }

  const Exp *Rec(const Exp *e) {
    if (depth == stop_depth) {
      // Drop the current node.
      did_drop = true;
      switch (e->type) {
      case VAR:
        return e;
      case PLUS_C:
        CHECK(e->iters != 0);
        if (e->iters == 1) {
          return e->a;
        } else {
          return alloc->PlusC(e->a, e->c, e->iters - 1);
        }
        break;
      case TIMES_C:
        CHECK(e->iters != 0);
        if (e->iters == 1) {
          return e->a;
        } else {
          return alloc->TimesC(e->a, e->c, e->iters - 1);
        }
        break;
      case PLUS_E:
        // We don't expect it to be useful to drop a whole
        // arm (nor do we use this in the expressions that need
        // most to be optimized), so we don't worry about trying
        // both a and b here.
        return e->a;
      default:
        CHECK(false);
        return nullptr;
      }

    } else {
      // Go deeper.
      depth++;
      switch (e->type) {
      case VAR:
        return e;
      case PLUS_C:
        return alloc->PlusC(Rec(e->a), e->c, e->iters);
      case TIMES_C:
        return alloc->TimesC(Rec(e->a), e->c, e->iters);
      case PLUS_E: {
        const Exp *ea = Rec(e->a);
        const Exp *eb = Rec(e->b);
        return alloc->PlusE(ea, eb);
      }
      default:
        CHECK(false);
        return nullptr;
      }
    }
  }

private:
  Allocator *alloc = nullptr;
  int64 depth = 0;
  int64 stop_depth = 0;
  bool did_drop = false;
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

static const Exp *CleanRec(Allocator *alloc, const Exp *exp) {
  switch (exp->type) {
  case VAR:
    return exp;
  case PLUS_C: {
    if (exp->iters == 0)
      return CleanRec(alloc, exp->a);

    // Adding zero or negative zero (usually) does nothing.
    if (exp->c == 0x0000 || exp->c == 0x8000)
      return CleanRec(alloc, exp->a);

    const Exp *ea = CleanRec(alloc, exp->a);
    if (ea->type == PLUS_C &&
        ea->c == exp->c) {
      int32 new_iters = (int32)exp->iters + (int32)ea->iters;
      if (new_iters <= 65535) {
        return alloc->PlusC(ea->a, exp->c, new_iters);
      }
    }

    return alloc->PlusC(ea, exp->c, exp->iters);
  }
  case TIMES_C: {
    if (exp->iters == 0)
      return CleanRec(alloc, exp->a);

    // Multiplying by one does nothing.
    if (exp->c == 0x3c00)
      return CleanRec(alloc, exp->a);

    // TODO even iterations of negative can be eliminated

    // Muliplying by -1 can be done exactly if the nested
    // operation is a multiplication that's done an ODD
    // number of times.
    if (exp->c == 0xbc00 &&
        // this negation must be odd (or does nothing)
        (exp->iters & 1) == 1 &&
        exp->a->type == TIMES_C &&
        // must be odd
        (exp->a->iters & 1) == 1) {
      // (Should also insist that the value is actually finite, etc.)
      return CleanRec(alloc,
                      alloc->TimesC(exp->a->a, exp->a->c ^ 0x8000,
                                    exp->a->iters));
    }

    const Exp *ea = CleanRec(alloc, exp->a);
    if (ea->type == TIMES_C &&
        ea->c == 0xbc00 &&
        (ea->iters & 1) == 1 &&
        (exp->iters & 1) == 1) {
      // As above, but in the opposite order.
      // Pull a nested odd multiplication by -1 into this one.
      return alloc->TimesC(ea->a, exp->c ^ 0x8000, exp->iters);

    } else if (ea->type == TIMES_C &&
               ea->c == exp->c) {
      // Collapse two iterated multiplications by the same
      // constant.
      int32 new_iters = (int32)exp->iters + (int32)ea->iters;
      if (new_iters <= 65535) {
        return alloc->TimesC(ea->a, exp->c, new_iters);
      }
    }

    return alloc->TimesC(ea, exp->c, exp->iters);
  }
  case PLUS_E:
    return alloc->PlusE(CleanRec(alloc, exp->a),
                        CleanRec(alloc, exp->b));
  default:
    CHECK(false);
    return nullptr;
  }
}

// 8000-bc00: -0 to -1
// 0-3c00: 0 to 1

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


template<class JointOptArg>
static const Exp *JointOpt(DB *db,
                           const DB::key_type &key,
                           const Exp *exp,
                           double sec,
                           uint64_t seed) {

  ChopTable orig_table;
  FillChopTable(exp, &orig_table);

  JointOptArg jo(exp);
  const int n = jo.arginfos.size();

  ChopTable tmp_table;
  auto StillWorks = [&](const Exp *exp, double *score) {
      FillChopTable(exp, &tmp_table);
      int64 diff = 0;
      for (int i = 0; i < CHOPTABLE_SIZE; i++) {
        diff += abs((int)orig_table[i] - (int)tmp_table[i]);
      }

      if (diff != 0) {
        *score = diff;
        return false;
      }

      *score = 0;
      return true;
    };

  using LO = LargeOptimizer<false>;


  double start_score = 0.0/0.0;
  static constexpr bool VERBOSE = false;
  auto Score = [&](const LO::arg_type &args) {
      if (VERBOSE) {
        printf("Args: ");
        for (double d : args) printf(" %d", (int)d);
      }

      Allocator alloc;
      const Exp *e2 = jo.SetArgs(&alloc, exp, args);
      // TODO: Gradient in infeasible region
      double diff;
      if (!StillWorks(e2, &diff)) {
        if (VERBOSE) printf(" -> "
                            ANSI_PURPLE "NO %.3f" ANSI_RESET
                            "\n", diff);
        return std::make_pair(1.0e30 + diff, false);
      }
      double cc = Cost(e2);
      if (VERBOSE) printf(" -> %s%.3f" ANSI_RESET "\n",
                          cc < start_score ? ANSI_GREEN :
                          cc == start_score ? ANSI_WHITE :
                          ANSI_RED,
                          cc);
      return std::make_pair(Cost(e2), true);
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
  if (end_score < start_score) {
    printf(AGREEN("JointOpt reduced!") " From "
           ACYAN("%.3f") " -> " ABLUE("%.3f") "\n",
           start_score, end_score);
    const Exp *opt_exp = jo.SetArgs(&db->alloc, exp, oend.value().first);
    return opt_exp;
  } else {
    return exp;
  }
}

static void OptimizeOne(DB *db,
                        int idx,
                        int num,
                        const DB::key_type &key,
                        const Exp *exp,
                        int *size_in, int *size_out) {
  ArcFour rc(StringPrintf("%d.%lld", idx, time(nullptr)));
  Allocator *alloc = &db->alloc;

  static constexpr bool VERBOSE = true;

  static constexpr double SAVE_EVERY = 60.0 * 5.0;

  auto StillWorks = [&](const Exp *exp) {
      auto chopo = Choppy::GetChoppy(exp);
      if (!chopo.has_value()) return false;
      if (chopo.value() != key) return false;

      return true;
    };

  auto StillWorksLinear = [&](const vector<Step> &steps) {
      Allocator alloc;
      const Exp *exp = State::GetExpressionFromSteps(&alloc, steps);
      return StillWorks(exp);
    };

  const Exp *start_exp = exp;
  const int start_size = Exp::ExpSize(exp);
  if (start_size < MIN_OPT) return;

  // Make sure the entry has the key it purports to.
  CHECK(StillWorks(exp));
  if (VERBOSE) printf("Still works at start.\n");

  {
    // First, delete expressions that do nothing.
    const Exp *clean_exp = CleanRec(alloc, exp);
    if (StillWorks(exp)) {
      exp = clean_exp;
    } else {
      CPrintf(ANSI_RED "Clean did not preserve behavior!"
              ANSI_RESET "\n");
      CHECK(false);
    }
  }

  if (VERBOSE) printf("Cleaned.\n");

  // XXX run both JointOptArgs1 and JointOptArgs2!

  auto DoJointOpt = [&]<class JO>(const uint64_t OPT_NAME) {
    if (OPTIMIZATIONS_ENABLED & OPT_NAME) {
      // on basis8, 5 min takes 163 minutes.
      constexpr double JOINT_OPT_SEC = 60 * 60.0; // 60.0 * 15.0; // * 30.0;
      uint64_t seed = Rand64(&rc);
      const Exp *joint_exp =
        JointOpt<JO>(db, key, exp, JOINT_OPT_SEC, seed);
      if (StillWorks(joint_exp)) {
        exp = joint_exp;
      } else {
        printf(ARED("JointOpt did not perserve behavior!") "\n");
        auto chopo = Choppy::GetChoppy(exp);
        printf("Old key: %s\n", DB::KeyString(key).c_str());
        printf("New key: ");
        if (!chopo.has_value()) printf(" Not choppy!");
        else printf("%s\n", DB::KeyString(chopo.value()).c_str());

        CHECK(false) <<
        "Was: " << Exp::Serialize(exp) << "\n"
        "Now: " << Exp::Serialize(joint_exp);
      }

      if (VERBOSE) printf("JointOpt done.\n");

      // JointOpt can create opportunities for cleanup (e.g. iters = 0).
      {
        const Exp *clean_exp = CleanRec(alloc, exp);
        if (StillWorks(exp)) {
          exp = clean_exp;
        } else {
          CPrintf(ANSI_RED "(2) Clean did not preserve behavior!"
                  ANSI_RESET "\n");
          CHECK(false);
        }
      }
    }
  };

  // Call the templated lambda with an explicit arg.
  DoJointOpt.template operator()<JointOptArgs1>(OPT_JOINT1);
  DoJointOpt.template operator()<JointOptArgs2>(OPT_JOINT2);

  if (State::CanBeLinearized(exp)) {
    CPrintf(ANSI_GREEN "Linearizable." ANSI_RESET "\n");
    vector<Step> steps = State::Linearize(exp);


    {
      CPrintf("Running ChopSection on expression of size " ANSI_YELLOW
              "%d" ANSI_RESET "\n", (int)steps.size());

      int64 tries = 0;
      Timer loop_timer;
      // average one per second across all threads.
      Periodically status_per((double)MAX_THREADS);
      // Try to skip the first report, though.
      (void)status_per.ShouldRun();
      // Save occasionally so that we don't lose too much
      // progress if we stop early.
      Periodically checkpoint_per(SAVE_EVERY);
      (void)checkpoint_per.ShouldRun();

      constexpr int MAX_CHOP = 16;

      for (int start_idx = 0; start_idx < steps.size(); start_idx++) {
        // PERF: At the end, this tries (harmlessly) chopping non-existent
        // steps.
        for (int chop_size = MAX_CHOP; chop_size > 0; chop_size--) {
          if (status_per.ShouldRun()) {
            int64 step_size = 0;
            for (const Step &step : steps)
              step_size += step.iters;

            CPrintf(ANSI_GREY "[%d/%d %s] " ANSI_RESET
                    "Tries " ANSI_YELLOW "%lld" ANSI_RESET " ("
                    ANSI_CYAN "%.3f " ANSI_RESET "/s) size "
                    ANSI_PURPLE "%lld" ANSI_RESET
                    " depth " ANSI_RED "%d" ANSI_RESET "/"
                    ANSI_YELLOW "%d" ANSI_RESET "\n",
                    idx + 1, num,
                    "ChopSection",
                    tries, (tries / loop_timer.Seconds()), step_size,
                    start_idx, (int)steps.size());
          }

          // Try chopping.
          vector<Step> chopped;
          chopped.reserve(steps.size());
          for (int i = 0; i < steps.size(); i++) {
            if (i >= start_idx && i < start_idx + chop_size) {
              // skip it.
            } else {
              chopped.push_back(steps[i]);
            }
          }

          tries++;
          if (StillWorksLinear(chopped)) {
            steps = std::move(chopped);
            CPrintf(ANSI_GREY "[%d/%d %s] " ANSI_RESET
                    "Chopped " ANSI_BLUE "%d" ANSI_RESET " steps at "
                    ANSI_PURPLE "%d" ANSI_RESET "! Now "
                    ANSI_YELLOW "%d" ANSI_RESET " steps.\n",
                    idx + 1, num,
                    "ChopSection", chop_size, start_idx,
                    (int)steps.size());

            exp = State::GetExpressionFromSteps(alloc, steps);
            if (checkpoint_per.ShouldRun()) {
              db->Add(exp);
              MaybeSaveDB(db);
            }

            // Reset chop size so that we keep trying to chop at
            // this position (the steps have been replaced).
            chop_size = MAX_CHOP + 1;
          }
        }
      }
    }

    {
      CPrintf("Running Meld on expression of size " ANSI_YELLOW
              "%d" ANSI_RESET "\n", (int)steps.size());

      int64 tries = 0;
      Timer loop_timer;
      // average one per second across all threads.
      Periodically status_per((double)MAX_THREADS);
      // Try to skip the first report, though.
      (void)status_per.ShouldRun();
      // Save occasionally so that we don't lose too much
      // progress if we stop early.
      Periodically checkpoint_per(SAVE_EVERY);
      (void)checkpoint_per.ShouldRun();

      constexpr int MAX_CHOP = 16;

      for (int start_idx = 0; start_idx < steps.size(); start_idx++) {
        // PERF: At the end, this tries (harmlessly) melding non-existent
        // steps.
        for (int meld_size = MAX_CHOP; meld_size > 1; meld_size--) {
          if (status_per.ShouldRun()) {
            int64 step_size = 0;
            for (const Step &step : steps)
              step_size += step.iters;

            CPrintf(ANSI_GREY "[%s] " ANSI_RESET
                    "Tries " ANSI_YELLOW "%lld" ANSI_RESET " ("
                    ANSI_CYAN "%.3f " ANSI_RESET "/s) size "
                    ANSI_PURPLE "%lld" ANSI_RESET
                    " depth " ANSI_RED "%d" ANSI_RESET "/"
                    ANSI_YELLOW "%d" ANSI_RESET "\n",
                    "Meld",
                    tries, (tries / loop_timer.Seconds()), step_size,
                    start_idx, (int)steps.size());
          }

          // Try chopping.
          vector<Step> chopped;
          chopped.reserve(steps.size());
          double sum = 0.0;
          double product = 1.0;
          for (int i = 0; i < steps.size(); i++) {
            if (i >= start_idx && i < start_idx + meld_size) {
              const Step &step = steps[i];
              double c = (double)Exp::GetHalf(step.c);

              // This kind of doesn't make sense if there are
              // a mix of plus/times, but we try anyway.
              if (step.type == STEP_PLUS) {
                // Or just multiply?
                for (int z = 0; z < step.iters; z++)
                  sum += c;
              } else {
                CHECK(step.type == STEP_TIMES);
                for (int z = 0; z < step.iters; z++)
                  product *= c;
              }

            } else  {
              if (i == start_idx + meld_size) {
                if (sum != 0.0)
                  chopped.push_back(
                      Step(STEP_PLUS, Exp::GetU16((half)sum), 1));
                if (product != 1.0)
                  chopped.push_back(
                      Step(STEP_TIMES, Exp::GetU16((half)product), 1));
              }
              chopped.push_back(steps[i]);
            }
          }

          tries++;
          if (chopped.size() < steps.size() &&
              StillWorksLinear(chopped)) {
            steps = std::move(chopped);
            CPrintf(ANSI_GREY "[%s] " ANSI_RESET
                    "Melded " ANSI_BLUE "%d" ANSI_RESET " steps at "
                    ANSI_PURPLE "%d" ANSI_RESET "! Now "
                    ANSI_YELLOW "%d" ANSI_RESET " steps.\n",
                    "Meld", meld_size, start_idx,
                    (int)steps.size());

            exp = State::GetExpressionFromSteps(alloc, steps);
            if (checkpoint_per.ShouldRun()) {
              db->Add(exp);
              MaybeSaveDB(db);
            }

            // Reset chop size so that we keep trying to chop at
            // this position (the steps have been replaced).
            meld_size = MAX_CHOP + 1;
          }
        }
      }
    }


    {
      CPrintf("Running BinaryIters on expression of size " ANSI_YELLOW
              "%d" ANSI_RESET "\n", (int)steps.size());

      int64 tries = 0;
      Timer loop_timer;
      // average one per second across all threads.
      Periodically status_per((double)MAX_THREADS);
      // Try to skip the first report, though.
      (void)status_per.ShouldRun();
      // Save occasionally so that we don't lose too much
      // progress if we stop early.
      Periodically checkpoint_per(SAVE_EVERY);
      (void)checkpoint_per.ShouldRun();

      for (int start_idx = 0; start_idx < steps.size(); start_idx++) {
        if (status_per.ShouldRun()) {
          int64 step_size = 0;
          for (const Step &step : steps)
            step_size += step.iters;

          CPrintf(ANSI_GREY "[%s] " ANSI_RESET
                  "Tries " ANSI_YELLOW "%lld" ANSI_RESET " ("
                  ANSI_CYAN "%.3f " ANSI_RESET "/s) size "
                  ANSI_PURPLE "%lld" ANSI_RESET
                  " depth " ANSI_RED "%d" ANSI_RESET "/"
                  ANSI_YELLOW "%d" ANSI_RESET "\n",
                  "BinaryIters",
                  tries, (tries / loop_timer.Seconds()), step_size,
                  start_idx, (int)steps.size());
        }

        // Binary search for minimal iters.
        {
          CHECK(start_idx >= 0 && start_idx < steps.size());
          const int original_iters = steps[start_idx].iters;
          int search_steps = 0;

          // iters does not work for values < lower_bound,
          // so minimal iters is >= this value.
          int lower_bound = 1;
          // iters does work at this value, so minimal iters is
          // <= this value.
          int upper_bound = steps[start_idx].iters;

          // Note that in the common case that iters == 1, we
          // are already done.
          while (lower_bound != upper_bound) {
            CHECK(lower_bound < upper_bound);
            // Rounding down, since we already know the result for upper_bound.
            const int next_iters =
              lower_bound + ((upper_bound - lower_bound) >> 1);

            search_steps++;
            steps[start_idx].iters = next_iters;
            tries++;
            if (StillWorksLinear(steps)) {
              CHECK(next_iters < upper_bound);
              upper_bound = next_iters;
              CPrintf(ANSI_GREY "[%s] " ANSI_RESET
                      "lb " ANSI_BLUE "%d" ANSI_RESET " ub "
                      ANSI_PURPLE "%d" ANSI_RESET " ("
                      ANSI_YELLOW "%d" ANSI_RESET " steps).\n",
                      "BinaryIters",
                      lower_bound, upper_bound, search_steps);
            } else {
              lower_bound = next_iters + 1;
            }
          }

          steps[start_idx].iters = upper_bound;
          if (upper_bound < original_iters) {

            CPrintf(ANSI_GREY "[%s] " ANSI_RESET
                    ANSI_GREEN "Reduced!" ANSI_RESET
                    " Iters " ANSI_BLUE "%d" ANSI_RESET " -> "
                    ANSI_PURPLE "%d" ANSI_RESET
                    " in " ANSI_CYAN "%d" ANSI_RESET " steps.\n",
                    "BinaryIters",
                    original_iters, upper_bound,
                    search_steps);
            exp = State::GetExpressionFromSteps(alloc, steps);
            if (checkpoint_per.ShouldRun()) {
              db->Add(exp);
              MaybeSaveDB(db);
            }
          }
        }
      }
    }
  }

  // For each expression, see if we can remove it (or reduce
  // its iterations) but retain the property.

  auto DoPhase = [db, alloc, &exp, idx, num,
                  start_size, &StillWorks]<typename Phase>(
      Phase phase) {
    const int total_depth = TotalDepth(exp);
    int64 tries = 0;
    Timer loop_timer;
    // average one per second across all threads.
    Periodically status_per((double)MAX_THREADS);
    // Try to skip the first report, though.
    (void)status_per.ShouldRun();
    // Save occasionally so that we don't lose too much
    // progress if we stop early.
    Periodically checkpoint_per(SAVE_EVERY);
    (void)checkpoint_per.ShouldRun();

    int exp_size = Exp::ExpSize(exp);
    for (;;) {
      Allocator local_alloc;
      tries++;
      if (status_per.ShouldRun()) {
        CPrintf(ANSI_GREY "[%d/%d %s] " ANSI_RESET
                "Tries " ANSI_YELLOW "%lld" ANSI_RESET " ("
                ANSI_CYAN "%.3f " ANSI_RESET "/s) size "
                ANSI_PURPLE "%d" ANSI_RESET
                " depth " ANSI_RED "%s" ANSI_RESET "/"
                ANSI_YELLOW "%d" ANSI_RESET "\n",
                idx + 1, num,
                phase.Name().c_str(),
                tries, (tries / loop_timer.Seconds()), exp_size,
                phase.CurrentDepth().c_str(), total_depth);
      }

      const Exp *e = phase.Run(&local_alloc, exp);

      // Nothing changed.
      if (phase.Done())
        break;

      // Does the trimmed expression still work?
      if (StillWorks(e)) {
        int new_size = Exp::ExpSize(e);
        if (new_size < exp_size) {
          CPrintf(ANSI_GREY "[%d/%d %s] " ANSI_RESET
                  ANSI_GREEN "Reduced!" ANSI_RESET
                  " Start " ANSI_BLUE "%d" ANSI_RESET " now "
                  ANSI_PURPLE "%d" ANSI_RESET "\n",
                  idx + 1, num,
                  phase.Name().c_str(),
                  start_size, new_size);
          exp = alloc->Copy(e);
          exp_size = new_size;
          if (checkpoint_per.ShouldRun()) {
            db->Add(exp);
            MaybeSaveDB(db);
          }
          // Keep the same stop depth, as we may be able to apply
          // another improvement at the same position.
        } else {
          // This can happen because we don't actually drop VAR nodes,
          // but we pretend we did (for uniformity).
          phase.NextDepth();
        }
      } else {
        // Eventually this gets larger than the expression and
        // we will be Done().
        phase.NextDepth();
      }
    }
  };

  if ((OPTIMIZATIONS_ENABLED & OPT_REDUCEITERS) &&
      !State::CanBeLinearized(exp)) {
    if (start_size > 1000) {
      CPrintf(ANSI_GREY "[%d/%d] " ANSI_RESET
              ANSI_RED "Slow iter reduction: can't be linearized."
              ANSI_RESET "\n",
              idx + 1, num);
    }
    DoPhase(ReduceIters());
  }

  if (Exp::ExpSize(exp) < start_size) {
    db->Add(exp);
    MaybeSaveDB(db);
  }

  if (OPTIMIZATIONS_ENABLED & OPT_DROPNODE) {
    DoPhase(DropNode());
  }

  const int end_size = Exp::ExpSize(exp);
  if (end_size < start_size) {
    CPrintf(AWHITE("[%d/%d] ")
            " Reduced from " ABLUE("%d") " to "
            APURPLE("%d") "\n",
            idx + 1, num,
            start_size, end_size);

    const auto [before, after] = ColorDiff(start_exp, exp);
    printf(AWHITE("Was") ": %s" ANSI_RESET "\n", before.c_str());
    printf(AWHITE("Now") ": %s" ANSI_RESET "\n", after.c_str());

    *size_in += start_size;
    *size_out += end_size;

    db->Add(exp);
    MaybeSaveDB(db);
  } else {
    CPrintf(ANSI_GREY "[%d/%d] No reduction (still %d)" ANSI_RESET "\n",
            idx + 1, num, start_size);

    *size_in += start_size;
    *size_out += start_size;
  }
}

int main(int argc, char **argv) {
  AnsiInit();
  CHECK(argc == 2) << "Give a database file on the command line.";
  Timer run_timer;

  DB db;
  printf("Load database:\n");
  db.LoadFile(argv[1]);

  std::vector<std::pair<const DB::key_type &,
                        const Exp *>> all;
  for (const auto &[k, v] : db.fns) {
    if (REQUIRE_BASIS) {
      for (const int z : k)
        CHECK(z == 0 || z == 1) << DB::KeyString(k) << ":\n"
                                << Exp::Serialize(v);
    }
    all.emplace_back(k, v);
  }

  std::mutex total_m;
  int64_t total_in = 0, total_out = 0;
  ParallelAppi(all,
               [&](int idx, const std::pair<
                   const DB::key_type &, const Exp *> &arg) {
                 int size_in = 0, size_out = 0;
                 OptimizeOne(&db, idx, (int)all.size(),
                             arg.first, arg.second,
                             &size_in, &size_out);
                 {
                   MutexLock ml(&total_m);
                   total_in += size_in;
                   total_out += size_out;
                 }
               },
               MAX_THREADS);

  Util::WriteFile("optimized.txt", db.Dump());


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

  printf(AGREEN("OK") "\n");
  return 0;
}
