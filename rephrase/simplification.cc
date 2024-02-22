
#include "simplification.h"

#include "il.h"

#include "il-pass.h"
#include "il-util.h"
#include "bignum/big-overloads.h"
#include "ansi.h"

static constexpr bool VERBOSE = true;

namespace {
struct Progress {
  // Call this whenever the expression definitely got smaller.
  void Simplified(const char *msg) {
    if (VERBOSE) {
      printf(AWHITE("S %d") " " AGREEN("%s") "\n",
             simplified, msg);
    }
    simplified++;
  }

  void Reset() {
    simplified = 0;
  }

  bool MadeProgress() const { return simplified > 0; }

private:
  int simplified = 0;
};
}  // namespace

namespace il {

Simplification::Simplification(AstPool *pool) : pool(pool) {}

void Simplification::SetVerbose(int v) {
  verbose = v;
}

// True if the expression is a value and cheaper/smaller than
// a variable lookup, and so it should always be inlined.
static bool IsSmallValue(const Exp *e) {
  switch (e->type) {
  case ExpType::FLOAT:
    return true;
  case ExpType::BOOL:
    return true;
  case ExpType::INT:
    // Since we use bigint, avoid substituting huge numbers.
    // (This could probably be increased a lot without problems!)
    return e->Int() < 4'000'000ULL;
  case ExpType::STRING:
    // PERF: Should consider inlining small strings by other means?
    return e->String().empty();
  case ExpType::VAR:
    return true;
  case ExpType::RECORD:
    return e->Record().size() == 0;
  default:
    return false;
  }
}

static bool IsEffectless(const Exp *e) {
  switch (e->type) {
  case ExpType::FLOAT: return true;
  case ExpType::BOOL: return true;
  case ExpType::INT: return true;
  case ExpType::STRING: return true;
  case ExpType::VAR: return true;
  case ExpType::FN: return true;

  case ExpType::RECORD: {
    for (const auto &[lab, child] : e->Record()) {
      if (!IsEffectless(child)) return false;
    }
    return true;
  }

  case ExpType::PROJECT:
    return IsEffectless(std::get<1>(e->Project()));
  case ExpType::INJECT:
    return IsEffectless(std::get<1>(e->Inject()));

  case ExpType::ROLL:
    return IsEffectless(std::get<1>(e->Roll()));

  case ExpType::PRIMOP: {
    const auto &[po, ts, es] = e->Primop();
    if (IsPrimopTotal(po)) {
      for (const Exp *child : es) {
        if (!IsEffectless(child)) {
          return false;
        }
      }
      return true;
    }

    return false;
  }

  default:
    return false;
  }
}

static void PushSeqs(const Exp *e, std::vector<const Exp *> *vflat) {
  switch (e->type) {
  case ExpType::FAIL:
    vflat->push_back(e);
    return;
  case ExpType::APP:
    // TODO: Maybe constructor applications?
    vflat->push_back(e);
    return;

  case ExpType::FLOAT: return;
  case ExpType::BOOL: return;
  case ExpType::INT: return;
  case ExpType::STRING: return;
  case ExpType::VAR: return;
  case ExpType::FN: return;

  case ExpType::RECORD: {
    for (const auto &[lab, child] : e->Record()) {
      PushSeqs(child, vflat);
    }
    return;
  }

  case ExpType::PROJECT:
    return PushSeqs(std::get<1>(e->Project()), vflat);
  case ExpType::INJECT:
    return PushSeqs(std::get<1>(e->Inject()), vflat);

  case ExpType::ROLL:
    return PushSeqs(std::get<1>(e->Roll()), vflat);
  case ExpType::UNROLL:
    return PushSeqs(e->Unroll(), vflat);

  case ExpType::PRIMOP: {
    const auto &[po, ts, es] = e->Primop();
    if (IsPrimopDiscardable(po)) {
      for (const Exp *child : es) {
        PushSeqs(child, vflat);
      }
    } else {
      vflat->push_back(e);
    }
    return;
  }

    // TODO: Several more here.
  case ExpType::INTCASE: {
    // const auto &[obj, arms, def] = e->IntCase();
    // Actually, can we ever throw this away? We'd have to
    // know whether an arm is going to match. If all of them
    // are discardable (and the default), then ok.
    //
  }

  default:
    vflat->push_back(e);
    break;
  }
}

// This is almost the same as effectless except that some primops can
// be dropped (e.g. GET) despite not being formally total. We have to
// do the whole thing instead of appealing to IsEffectless for other
// cases, since we want something like (GET, GET) to be considered
// discardable.
//
// TODO: This is probably inferior to PushSeqs, which lets us drop
// parts of the expression that are not effectful. We defer to that
// function now anyway.
static bool IsDiscardable(const Exp *e) {
  std::vector<const Exp *> tmp;
  PushSeqs(e, &tmp);
  return tmp.empty();
}

namespace {
struct PeepholePass : public il::Pass<> {
  PeepholePass(AstPool *pool, Progress *progress) :
    Pass(pool),
    progress(progress) {}

  const Exp *DoUnroll(const Exp *e, const Exp *guess) override {
    e = DoExp(e);
    if (e->type == ExpType::ROLL) {
      const auto &[tt, ee] = e->Roll();
      return ee;
    }
    return pool->Unroll(e, guess);
  }

  const Exp *DoProject(const std::string &label,
                       const Exp *arg,
                       const Exp *guess) override {
    arg = DoExp(arg);
    if (arg->type == ExpType::RECORD) {
      // Evaluate all the elements in order, so that
      // we can preserve evaluation order. We just make a
      // binding for each and then let other simplifications
      // throw them away.
      const auto &le = arg->Record();
      std::vector<std::string> vars;
      const Exp *body = nullptr;
      for (const auto &[lab, exp] : le) {
        vars.push_back(pool->NewVar(lab));
        if (lab == label) {
          CHECK(body == nullptr) << "Duplicate label " << lab;
          body = pool->Var({}, vars.back());
        }
      }
      CHECK(body != nullptr) << "Bug? Label missing when simplifying "
        "projection expression. Label: " << label;

      // Now wrap the body.
      for (int i = le.size() - 1; i >= 0; i--) {
        const auto &[lab_, exp] = le[i];
        body = pool->Let({}, vars[i], exp, body);
      }

      Simplified("reduce project(record)");
      return body;
    } else {
      return pool->Project(label, arg, guess);
    }
  }

  // For fn expressions, if the function's self variable is not used,
  // it is not actually recursive.
  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Exp *body,
                  const Exp *guess) override {
    if (!self.empty() && !ILUtil::IsExpVarFree(body, self)) {
      Simplified("remove recursive fn var");
      if (VERBOSE) {
        printf("Removed var is " APURPLE("%s") "\n", self.c_str());
      }
      return pool->Fn("", x, DoExp(body), guess);
    }

    return pool->Fn(self, x, DoExp(body), guess);
  }

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess) override {
    if (body->type == ExpType::VAR) {
      const auto &[vtv, xx] = body->Var();
      if (x == xx) {
        // let (a, b, ...) x = rhs in x<t1, t2, ...> end -->
        // [t1/a][t2/b]rhs
        CHECK(tyvars.size() == vtv.size());

        for (int i = 0; i < (int)tyvars.size(); i++) {
          // make sure the bound tyvar is fresh (not appearing in any
          // t1..tn).
          const auto &[a, nrhs] =
            ILUtil::AlphaVaryTypeInExp(pool, tyvars[i], rhs);
          rhs = nrhs;
          // and substitute for it
          rhs = ILUtil::SubstTypeInExp(pool, vtv[i], a, rhs);
        }

        Simplified("eta-contracted let x = e in x");
        return DoExp(rhs);
      } else {
        // This is handled by the below since x is not free in xx.
      }
    }

    int count = ILUtil::ExpVarCount(body, x);

    if (count == 0) {
      Simplified("remove unused binding");
      if (VERBOSE) {
        printf("  Unused var is " APURPLE("%s") "\n", x.c_str());
      }

      // Substitute away any tyvars, since they will no longer be
      // bound. We can use anything since the generalized symbol
      // was never used.
      if (!tyvars.empty()) {
        const Type *ovoid = pool->SumType({});
        for (const std::string &alpha : tyvars) {
          rhs = ILUtil::SubstTypeInExp(pool, ovoid, alpha, rhs);
        }
      }

      return pool->Seq({DoExp(rhs)}, DoExp(body));
    }

    const bool small_value = IsSmallValue(rhs);
    const bool effectless = small_value || IsEffectless(rhs);

    if (count <= 1 && effectless) {
      // Inline any effectless expression that occurs just once,
      // regardless of its size.
      Simplified("inlined single-use binding");
      if (VERBOSE) {
        printf("  Inlined var is " APURPLE("%s") "\n", x.c_str());
      }
      return ILUtil::SubstPolyExp(pool, tyvars, DoExp(rhs), x, DoExp(body));
    }

    // TODO: support inlining of polymorphic values.
    if (small_value && tyvars.empty()) {
      Simplified("inlined small value");
      const Exp *value = DoExp(rhs);
      if (VERBOSE) {
        printf("  Inlined var is " APURPLE("%s") " = " ABLUE("%s") "\n",
               x.c_str(), ExpString(value).c_str());
      }
      return ILUtil::SubstExp(pool, value, x, DoExp(body));
    }

    return pool->Let(tyvars, x, DoExp(rhs), DoExp(body), guess);
  }

  const Exp *DoIf(
      const Exp *cond,
      const Exp *true_branch,
      const Exp *false_branch,
      const Exp *guess) override {
    if (cond->type == ExpType::BOOL) {
      Simplified("reduced if");
      return DoExp(cond->Bool() ? true_branch : false_branch);
    } else {
      // TODO: if true and false branches are syntactically equal
      // TODO: if condition is a negation
      return Pass::DoIf(cond, true_branch, false_branch, guess);
    }
  }

  const Exp *DoIntCase(
      const Exp *obj,
      const std::vector<std::pair<BigInt, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if (obj->type == ExpType::INT) {
      Simplified("reduce intcase");
      for (const auto &[bi, arm] : arms) {
        if (bi == obj->Int()) {
          return DoExp(arm);
        }
      }
      // None matched, so it's the default.
      return DoExp(def);
    } else {
      return Pass::DoIntCase(obj, arms, def, guess);
    }
  }

  const Exp *DoStringCase(
      const Exp *obj,
      const std::vector<std::pair<std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if (obj->type == ExpType::STRING) {
      Simplified("reduce stringcase");
      for (const auto &[s, arm] : arms) {
        if (s == obj->String()) {
          return DoExp(arm);
        }
      }
      // None matched, so it's the default.
      return DoExp(def);
    } else {
      return Pass::DoStringCase(obj, arms, def, guess);
    }
  }

  const Exp *DoSumCase(
      const Exp *obj,
      const std::vector<
          std::tuple<std::string, std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if (obj->type == ExpType::INJECT) {
      Simplified("reduce sumcase");
      const auto &[label, e] = obj->Inject();
      for (const auto &[lab, v, arm] : arms) {
        if (label == lab) {
          return pool->Let({}, v, DoExp(e), arm);
        }
      }
      // No match, so it is the default.
      // Here the body of the inject could still have effects,
      // so sequence that.
      return pool->Seq({DoExp(e)}, DoExp(def));
    } else {
      return Pass::DoSumCase(obj, arms, def, guess);
    }
  }

  // If we have App(fn x => body, arg), with the function not recursive,
  // then this is equivalent to
  // let x = arg in body
  // The let sometimes allows for futher simplification.
  const Exp *DoApp(const Exp *f, const Exp *arg,
                   const Exp *guess) override {
    arg = DoExp(arg);

    if (f->type == ExpType::FN) {
      const auto &[self, x, body] = f->Fn();
      if (self.empty()) {
        Simplified("reduce app");
        return pool->Let({}, x, arg, DoExp(body));
      }
    }
    return pool->App(DoExp(f), arg, guess);
  }

  const Exp *DoSeq(const std::vector<const Exp *> &v,
                   const Exp *body,
                   const Exp *guess) override {
    // First process them all recursively, so that they are flat.
    std::vector<const Exp *> vv;
    vv.reserve(v.size());
    for (const Exp *c : v) vv.push_back(DoExp(c));
    std::vector<const Exp *> vflat;
    for (const Exp *c : vv) {
      // printf("IsEffectless %s?\n", ExpString(c).c_str());
      if (IsDiscardable(c)) {
        Simplified("dropped effectless seq");
      } else {
        if (c->type == ExpType::SEQ) {
          Simplified("flattened nested seq");
          const auto &[ces, cbody] = c->Seq();
          for (const Exp *cc : ces) {
            PushSeqs(cc, &vflat);
          }
          PushSeqs(cbody, &vflat);
        } else {
          PushSeqs(c, &vflat);
        }
      }
    }

    if (vflat.empty()) {
      Simplified("empty seq");
      return DoExp(body);
    } else {
      // The body could also be a Seq; then append the
      // sequences.
      const Exp *bbody = DoExp(body);
      if (bbody->type == ExpType::SEQ) {
        const auto &[fff, bbb] = bbody->Seq();
        for (const Exp *f : fff) {
          vflat.push_back(f);
        }
        return pool->Seq(vflat, bbb, guess);
      } else {
        return pool->Seq(vflat, bbody, guess);
      }
    }
  }

  void Simplified(const char *msg) {
    progress->Simplified(msg);
  }

private:
  Progress *progress = nullptr;
};

// Inlines globals, or drops unused ones.
struct GlobalInlining {
  GlobalInlining(AstPool *pool, Progress *progress) :
    pool(pool),
    progress(progress) {}

  Program Run(const Program &program) {
    Program out = program;
    // one for each global in the original program, and then one for
    // the body (empty key).
    std::unordered_map<std::string,
                       std::unordered_map<std::string, int>> mentioned;
    for (const Global &global : program.globals) {
      mentioned[global.sym] = ILUtil::LabelCounts(global.exp);
    }
    mentioned[""] = ILUtil::LabelCounts(program.body);

    for (int global_idx = 0;
         global_idx < (int)out.globals.size();
         /* in loop */) {
      const Global &global = out.globals[global_idx];
      // Get the total count of occurrences outside the symbol itself.
      int total_count = 0;
      for (const auto &[sym_, sym_count] : mentioned) {
        auto it = sym_count.find(global.sym);
        if (it != sym_count.end()) {
          total_count += it->second;
        }
      }

      // TODO: Or if a small value?
      if (total_count <= 1) {
        progress->Simplified("drop/inline global");
        if (VERBOSE) {
          printf("  There were " AYELLOW("%d") " occurrences.\n",
                 total_count);
          printf("  Inlined sym is " APURPLE("%s") " = " ABLUE("%s") "\n",
                 global.sym.c_str(), ExpString(global.exp).c_str());
        }

        // Update all the globals in place.
        for (int i = 0; i < (int)out.globals.size(); i++) {
          if (i != global_idx) {
            out.globals[i].exp = ILUtil::SubstPolyExpForLabel(
                pool, global.tyvars, global.exp, global.sym,
                out.globals[i].exp);
          }
        }

        // And the body.
        out.body = ILUtil::SubstPolyExpForLabel(
            pool, global.tyvars, global.exp, global.sym,
            out.body);

        // Now this global is unused.
        out.globals.erase(out.globals.begin() + global_idx);
        // And don't increment.
      } else {
        global_idx++;
      }
    }
    return out;
  }

 private:
  AstPool *pool = nullptr;
  Progress *progress = nullptr;
};
}  // namespace

Program Simplification::Simplify(const Program &program_in) {
  Progress progress;
  Program program = program_in;
  PeepholePass peephole(pool, &progress);
  GlobalInlining global_inlining(pool, &progress);

  do {
    progress.Reset();
    program = peephole.DoProgram(program);

    program = global_inlining.Run(program);

    if (VERBOSE) {
      printf("\n" AYELLOW("After simplification:\n"));
      printf("%s\n", ProgramString(program).c_str());
    }

  } while (progress.MadeProgress());

  return program;
}

}  // namespace il
