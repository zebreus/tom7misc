
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

using DB = Choppy::DB;
using Allocator = Exp::Allocator;
using Table = Exp::Table;

constexpr int MAX_THREADS = 4;

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

struct CopyAndReduce {

  const Exp *Run(Allocator *alloc_, const Exp *e) {
    alloc = alloc_;
    did_drop = false;
    depth = 0;
    return Rec(e);
  }

  bool Done() { return !did_drop; }

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

  Allocator *alloc = nullptr;
  int64 depth = 0;
  int64 stop_depth = 0;
  bool did_drop = false;
};

static int64 TotalDepth(const Exp *e) {
  int64 depth = 0;
  std::function<void(const Exp *)> Rec = [&Rec, &depth](const Exp *e) {
      // Go deeper.
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
    // Multiplying by one does nothing.
    if (exp->c == 0x3c00)
      return CleanRec(alloc, exp->a);

    // TODO: squash multiply by -1 with another multiplication.

    const Exp *ea = CleanRec(alloc, exp->a);
    if (ea->type == TIMES_C &&
        ea->c == exp->c) {
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

static void OptimizeOne(DB *db,
                        const DB::key_type &key,
                        const Exp *exp) {
  Allocator *alloc = &db->alloc;

  auto StillWorks = [&](const Exp *exp) {
      auto chopo = Choppy::GetChoppy(exp);
      if (!chopo.has_value()) return false;
      if (chopo.value() != key) return false;

      return true;
    };

  const int start_size = Exp::ExpSize(exp);

  // Make sure the entry has the key it purports to.
  CHECK(StillWorks(exp));

  // First, delete expressions that do nothing.
  const Exp *clean_exp = CleanRec(alloc, exp);
  if (StillWorks(exp)) {
    exp = clean_exp;
  } else {
    CPrintf(ANSI_RED "Clean did not preserve behavior!"
            ANSI_RESET "\n");
  }

  // For each expression, see if we can remove it (or reduce
  // its iterations) but retain the property.

  CopyAndReduce car;
  int64 tries = 0;
  Timer loop_timer;
  // average one per second across all threads.
  Periodically status_per((double)MAX_THREADS);
  // Try to skip the first report, though.
  (void)status_per.ShouldRun();
  // Save occasionally so that we don't lose too much
  // progress if we stop early.
  Periodically checkpoint_per(60.0 * 10.0);
  (void)checkpoint_per.ShouldRun();

  int exp_size = Exp::ExpSize(exp);
  const int total_depth = TotalDepth(exp);
  for (;;) {
    Allocator local_alloc;
    tries++;
    if (status_per.ShouldRun()) {
      CPrintf("Tries " ANSI_YELLOW "%lld" ANSI_RESET " ("
              ANSI_CYAN "%.3f " ANSI_RESET "/s) size "
              ANSI_PURPLE "%d" ANSI_RESET
              " depth " ANSI_RED "%d" ANSI_RESET "/"
              ANSI_YELLOW "%d" ANSI_RESET "\n",
              tries, (tries / loop_timer.Seconds()), exp_size,
              car.stop_depth, total_depth);
    }

    const Exp *e = car.Run(&local_alloc, exp);

    // Nothing changed.
    if (car.Done())
      break;

    // Does the trimmed expression still work?
    if (StillWorks(e)) {
      int new_size = Exp::ExpSize(e);
      if (new_size < exp_size) {
        CPrintf(ANSI_GREEN "Reduced!" ANSI_RESET
                " Start " ANSI_BLUE "%d" ANSI_RESET " now "
                ANSI_PURPLE "%d" ANSI_RESET "\n",
                start_size, new_size);
        exp = alloc->Copy(e);
        exp_size = new_size;
        if (checkpoint_per.ShouldRun()) {
          db->Add(exp);
          MaybeSaveDB(db);
        }
        // Keep the same stop depth, since there's a new node
        // (or lower iteration) at this position now.
      } else {
        // This can happen because we don't actually drop VAR nodes,
        // but we pretend we did (for uniformity).
        car.stop_depth++;
      }
    } else {
      // Eventually this gets larger than the expression and
      // we will be Done().
      car.stop_depth++;
    }
  }

  const int end_size = Exp::ExpSize(exp);
  if (end_size < start_size) {
    CPrintf("Reduced from " ANSI_BLUE "%d" ANSI_RESET " to "
            ANSI_PURPLE "%d" ANSI_RESET "\n", start_size, end_size);
    db->Add(exp);
    MaybeSaveDB(db);
  } else {
    CPrintf(ANSI_GREY "No reduction (still %d)" ANSI_RESET "\n",
            start_size);
  }
}

int main(int argc, char **argv) {
  AnsiInit();
  CHECK(argc == 2) << "Give a database file on the command line.";

  DB db;
  printf("Load database:\n");
  db.LoadFile(argv[1]);

  std::vector<std::pair<const DB::key_type &,
                        const Exp *>> all;
  for (const auto &[k, v] : db.fns)
    all.emplace_back(k, v);

  ParallelApp(all,
              [&](const std::pair<
                  const DB::key_type &, const Exp *> &arg) {
                OptimizeOne(&db, arg.first, arg.second);
              },
              MAX_THREADS);

  Util::WriteFile("optimized.txt", db.Dump());

  printf("OK\n");
  return 0;
}
