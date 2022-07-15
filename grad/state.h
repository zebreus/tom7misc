
#ifndef _GRAD_STATE_H
#define _GRAD_STATE_H

#include <vector>

#include "grad-util.h"
#include "expression.h"

#include "half.h"

// Linearized representation.
//
// Not the same as GradUtil::Step, but similar idea.
enum StepType : uint8_t {
  STEP_PLUS,
  STEP_TIMES,
};
struct Step {
  Step(StepType type, uint16_t c, uint16_t iters) :
    type(type), c(c), iters(iters) {}
  StepType type = STEP_PLUS;
  uint16_t c = 0;
  uint16_t iters = 0;
};

// Also not the same as GradUtil::State.
struct State {
  using Table = GradUtil::Table;
  using Allocator = Exp::Allocator;

  State(const Exp *e) {
    InitRec(e);
  }
  State() : table(GradUtil::IdentityTable()) {}

  // Do the step (and add it).
  void DoStep(Step s) {
    steps.push_back(s);
    switch (s.type) {
    case STEP_PLUS: {
      half c = Exp::GetHalf(s.c);
      // PERF: Compare other nesting of loops,
      // especially if the iteration count is high?
      for (int i = 0; i < s.iters; i++) {
        GradUtil::ForPosNeg1([&](uint16_t x) {
            half y = Exp::GetHalf(table[x]);
            table[x] = Exp::GetU16(y + c);
          });
      }
      break;
    }
    case STEP_TIMES: {
      half c = Exp::GetHalf(s.c);
      // PERF: as above.
      for (int i = 0; i < s.iters; i++) {
        GradUtil::ForPosNeg1([&](uint16_t x) {
            half y = Exp::GetHalf(table[x]);
            table[x] = Exp::GetU16(y * c);
          });
      }
      break;
    }
    default:
      CHECK(false) << "unknown step type";
    }
  }

  static bool CanBeLinearized(const Exp *exp) {
    while (exp->type == PLUS_C ||
           exp->type == TIMES_C) exp = exp->a;
    if (exp->type == VAR) return true;
    if (exp->type == PLUS_E) return false;
    return false;
  }

  static const Exp *GetExpressionFromSteps(
      Allocator *alloc,
      const std::vector<Step> &steps) {
    const Exp *e = alloc->Var();
    for (const Step &s : steps) {
      switch (s.type) {
      case STEP_PLUS:
        e = alloc->PlusC(e, s.c, s.iters);
        break;
      case STEP_TIMES:
        e = alloc->TimesC(e, s.c, s.iters);
        break;
      default:
        CHECK(false) << "bad step type";
      }
    }
    return e;
  }

  const Exp *GetExpression(Allocator *alloc) {
    return GetExpressionFromSteps(alloc, steps);
  }

  static std::vector<Step> Linearize(const Exp *e) {
    std::vector<Step> steps;
    while (e->type == PLUS_C || e->type == TIMES_C) {
      steps.push_back(Step(e->type == PLUS_C ? STEP_PLUS :
                           STEP_TIMES,
                           e->c, e->iters));
      e = e->a;
    }
    CHECK(e->type == VAR) << "must pass CanBeLinearized.";

    std::vector<Step> rev;
    rev.reserve(steps.size());
    for (int i = steps.size() - 1; i >= 0; i--)
      rev.push_back(steps[i]);
    return rev;
  }

  // Only the region in [-1, 1] is computed.
  Table table;
  std::vector<Step> steps;

private:
  void InitRec(const Exp *e) {
    switch (e->type) {
    case VAR:
      table = GradUtil::IdentityTable();
      break;
    case PLUS_C:
      InitRec(e->a);
      DoStep(Step(STEP_PLUS, e->c, e->iters));
      break;
    case TIMES_C:
      InitRec(e->a);
      DoStep(Step(STEP_TIMES, e->c, e->iters));
      break;
    case PLUS_E:
      LOG(FATAL) << "PLUS_E expressions are not supported "
        "in the linearized State representation.";
      break;
    }
  }
};

#endif
