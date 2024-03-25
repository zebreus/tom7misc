
#include "simplification.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "functional-map.h"
#include "il-pass.h"
#include "il-util.h"
#include "il.h"
#include "primop.h"
#include "util.h"

static constexpr bool VERBOSE = false;

// TODO: Can do some typed simplification, like:
//   - unit erasure
//   - transform sums that are just enums into ints
//   - flatten records

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
  verbose = VERBOSE ? std::max(v, 2) : v;
}

// A brief (suitable for one-liner), color version of an expression.
static std::string ExpStringShort(const Exp *e) {
  switch (e->type) {
  default:
    return StringPrintf(ABLUE("%s"), ExpTypeString(e->type));
  }
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
    return e->Record().empty();

  case ExpType::OBJECT:
    return e->Object().empty();

  // TODO: Functions that are primops or constructors.
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

  case ExpType::OBJECT: {
    for (const auto &[lab, oft, child] : e->Object()) {
      if (!IsEffectless(child)) return false;
    }
    return true;
  }

  case ExpType::PROJECT:
    return IsEffectless(std::get<1>(e->Project()));
  case ExpType::INJECT:
    return IsEffectless(std::get<2>(e->Inject()));

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
    return PushSeqs(std::get<2>(e->Inject()), vflat);

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
  PeepholePass(uint64_t opts, AstPool *p, Progress *progress) :
    Pass(p),
    opts(opts),
    progress(progress) {}

  const Exp *DoUnroll(const Exp *e, const Exp *guess) override {
    e = DoExp(e);
    if ((opts & Simplification::O_REDUCE) && e->type == ExpType::ROLL) {
          const auto &[tt, ee] = e->Roll();
      Simplified("reduce unroll");
      return ee;
    }
    return pool->Unroll(e, guess);
  }

  const Exp *DoProject(const std::string &label,
                       const Exp *arg,
                       const Exp *guess) override {
    arg = DoExp(arg);
    if ((opts & Simplification::O_REDUCE) &&
        arg->type == ExpType::RECORD) {
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

  const Exp *DoPrimop(Primop po,
                      const std::vector<const Type *> &ts,
                      const std::vector<const Exp *> &es,
                      const Exp *guess) override {
    std::vector<const Type *> tts;
    tts.reserve(ts.size());
    for (const Type *t : ts) tts.push_back(DoType(t));

    std::vector<const Exp *> ees;
    ees.reserve(es.size());
    for (const Exp *e : es) ees.push_back(DoExp(e));

    if (opts & Simplification::O_REDUCE) {
      const auto &[num_types, num_args] = PrimopArity(po);
      CHECK((int)ees.size() == num_args &&
            (int)tts.size() == num_types) << "Internal type error: Wrong "
        "number of args to primop " << PrimopString(po);

      switch (po) {
      case Primop::STRING_CONCAT:
        if (ees[0]->type == ExpType::STRING &&
            ees[1]->type == ExpType::STRING) {
          Simplified("string-concat primop");
          return pool->String(ees[0]->String() + ees[1]->String());
        }
        break;

      case Primop::STRING_EMPTY:
        if (ees[0]->type == ExpType::STRING) {
          Simplified("string-empty primop");
          return pool->Bool(ees[0]->String().empty());
        }
        break;

      case Primop::STRING_SIZE:
        if (ees[0]->type == ExpType::STRING) {
          Simplified("string-size primop");
          return pool->Int(ees[0]->String().size());
        }
        break;

        // TODO: So many more primops can be reduced!

      default:
        break;
      }
    }

    return pool->Primop(po, tts, ees, guess);
  }

  // For fn expressions, if the function's self variable is not used,
  // it is not actually recursive.
  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Type *arrow_type,
                  const Exp *body,
                  const Exp *guess) override {
    if ((opts & Simplification::O_MAKE_NONRECURSIVE) &&
        !self.empty() && !ILUtil::IsExpVarFree(body, self)) {
      Simplified("remove recursive fn var");
      if (VERBOSE) {
        printf("Removed var is " APURPLE("%s") "\n", self.c_str());
      }
      return pool->Fn("", x, DoType(arrow_type), DoExp(body), guess);
    }

    return pool->Fn(self, x, DoType(arrow_type), DoExp(body), guess);
  }

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess) override {
    if ((opts & Simplification::O_ETA_CONTRACT) &&
        body->type == ExpType::VAR) {
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

    if ((opts & Simplification::O_DEAD_VARS) && count == 0) {
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

    if ((opts & Simplification::O_INLINE_EXP) && count <= 1 && effectless) {
      // Inline any effectless expression that occurs just once,
      // regardless of its size.
      Simplified("inlined single-use binding");
      if (VERBOSE) {
        std::string ts = tyvars.empty() ? "" :
          StringPrintf(", tyvars (" ABLUE("%s") ")",
                       Util::Join(tyvars, ",").c_str());
        printf("  Inlined var is " APURPLE("%s") "%s\n",
               x.c_str(), ts.c_str());
      }
      return ILUtil::SubstPolyExp(pool, tyvars, DoExp(rhs), x, DoExp(body));
    }

    // TODO: support inlining of polymorphic values.
    if ((opts & Simplification::O_INLINE_EXP) &&
        small_value && tyvars.empty()) {
      Simplified("inlined small value");
      const Exp *value = DoExp(rhs);
      if (VERBOSE) {
        printf("  Inlined var is " APURPLE("%s") " = %s\n",
               x.c_str(), ExpStringShort(value).c_str());
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
    if ((opts & Simplification::O_REDUCE) && cond->type == ExpType::BOOL) {
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
    if ((opts & Simplification::O_REDUCE) &&
        obj->type == ExpType::INT) {
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
    if ((opts & Simplification::O_REDUCE) &&
        obj->type == ExpType::STRING) {
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
    if ((opts & Simplification::O_REDUCE) &&
        obj->type == ExpType::INJECT) {
      Simplified("reduce sumcase");
      const auto &[label, sum_type, e] = obj->Inject();
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
      // TODO: If it's exhaustive, we can replace the default with
      // one of the arms (unless we need to bind an inner object).
      return Pass::DoSumCase(obj, arms, def, guess);
    }
  }

  // If we have App(fn x => body, arg), with the function not recursive,
  // then this is equivalent to
  // let x = arg in body
  // The let sometimes allows for futher simplification.
  const Exp *DoApp(const Exp *f, const Exp *arg,
                   const Exp *guess) override {
    f = DoExp(f);
    arg = DoExp(arg);

    if ((opts & Simplification::O_REDUCE) &&
        f->type == ExpType::FN) {
      const auto &[self, x, t, body] = f->Fn();
      if (self.empty()) {
        Simplified("reduce app");
        return pool->Let({}, x, arg, DoExp(body));
      }
    }
    return pool->App(f, arg, guess);
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
      if ((opts & Simplification::O_DEAD_CODE) &&
          IsDiscardable(c)) {
        Simplified("dropped effectless seq");
        if (VERBOSE) {
          printf("  Exp was %s\n", ExpStringShort(c).c_str());
        }
      } else {
        if ((opts & Simplification::O_FLATTEN) &&
            c->type == ExpType::SEQ) {
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

    if ((opts & Simplification::O_FLATTEN) &&
        vflat.empty()) {
      Simplified("empty seq");
      return DoExp(body);
    } else {
      // The body could also be a Seq; then append the
      // sequences.
      const Exp *bbody = DoExp(body);
      if ((opts & Simplification::O_FLATTEN) &&
          bbody->type == ExpType::SEQ) {
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
  const uint64_t opts = 0;
  Progress *progress = nullptr;
};

struct Knowledge {
  const Exp *value = nullptr;
  // In some cases (records) we need to do setup work to know the
  // value, so we only want to do that work if the value will be
  // used. This bit is modified in place when the knowledge is used.
  bool was_used = false;
};
using Known = FunctionalMap<std::string, std::shared_ptr<Knowledge>>;

// TODO: This could be a general 'known' pass.
struct KnownPass : public il::Pass<Known> {
  KnownPass(uint64_t opts, AstPool *p, Progress *progress) :
    Pass(p),
    opts(opts),
    progress(progress) {}

  // This pass is designed to find code like this:
  // let val r = {lab1 = exp1, lab2 = exp2, ...}
  // in
  //   ... #lab1 r ...   ... #lab2 r ...
  // end
  //
  // and replace it with
  //
  // let
  //    val r_lab1 = exp1
  //    val r_lab2 = exp2
  // in
  //     r_lab1     ...   r_lab2
  // end
  //
  // This is particularly common with binops whose wrapper functions
  // have been inlined.

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs_in,
                   const Exp *body,
                   const Exp *guess,
                   Known known) override {
    // Is this a tuple binding?
    const Exp *rhs = DoExp(rhs_in, known);
    if ((opts & Simplification::O_EXPLODE_RECORDS) &&
        tyvars.empty() && rhs->type == ExpType::RECORD) {
      const auto &fields = rhs->Record();
      // If any of the record's fields are not valuable, pull them
      // out as intermediate variables. That way when processing the
      // body we can reduce #lab x without moving any effects.
      //
      // However, we need to avoid doing this transformation if
      // we're not actually going to use the record's fields (e.g.
      // if it only escapes). Otherwise we can get into infinite
      // loops where inlining will try to put them back. So we
      // provisionally create the knowledge, translate the body,
      // and then only actually do this if the knowledge was marked
      // as was_used.
      std::vector<std::pair<std::string, const Exp *>> bindings;
      std::vector<std::pair<std::string, const Exp *>> new_fields;
      for (const auto &[lab, exp] : fields) {
        if (IsSmallValue(exp)) {
          // Already okay
          new_fields.emplace_back(lab, exp);
        } else {
          std::string tmp = pool->NewVar(pool->BaseVar(x) + "_" +
                                         pool->BaseVar(lab));
          bindings.emplace_back(tmp, exp);
          new_fields.emplace_back(lab, pool->Var({}, tmp));
        }
      }

      const Exp *new_rhs = pool->Record(new_fields, rhs);

      auto knowledge = std::make_shared<Knowledge>(Knowledge{
          .value = new_rhs, .was_used = false});
      known = known.Insert(x, knowledge);

      // This sets knowledge->was_used if we need the fields to be
      // pulled out. Otherwise, the original rhs will suffice.
      const Exp *new_body = DoExp(body, known);

      if (knowledge->was_used) {
        // Here we use the new exploded record.
        const Exp *ret =
          pool->Let(tyvars, x, new_rhs, DoExp(body, known), guess);

        // And wrap with bindings, preserving evaluation order.
        for (int i = bindings.size() - 1; i >= 0; i--) {
          const auto &[var, arm_exp] = bindings[i];
          if (VERBOSE) {
            printf("  serialized record field " APURPLE("%s") " = ...\n",
                   var.c_str());
          }
          CHECK(tyvars.empty());
          ret = pool->Let({}, var, arm_exp, ret);
        }
        return ret;
      } else {
        // Don't use new_rhs, and so the bindings are also not
        // to be generated.
        return pool->Let(tyvars, x, rhs, new_body, guess);
      }

    } else {
      return pool->Let(tyvars, x, rhs, DoExp(body, known), guess);
    }
  }

  const Exp *DoProject(const std::string &s,
                       const Exp *e_in,
                       const Exp *guess,
                       Known known) override {
    const Exp *e = DoExp(e_in, known);
    if ((opts & Simplification::O_EXPLODE_RECORDS) &&
        e->type == ExpType::VAR) {
      const auto &[tyvars, x] = e->Var();
      if (tyvars.empty()) {
        /*
        printf("Check #" ABLUE("%s") " " ACYAN("%s") "?\n",
               s.c_str(),
               x.c_str());
        */
        if (std::shared_ptr<Knowledge> *kv = known.FindPtr(x)) {
          const Exp *rec = (*kv)->value;
          CHECK(rec->type == ExpType::RECORD) << "Projection from a "
            "known value, but it's not a record! Field " << s <<
            " projected from var " << x << " but known exp:\n" <<
            ExpString(rec);

          // Always use the known value instead.
          // There may be cases where it's better to just
          // project from the record (this is cheap), but
          // it's probably a wash anyway. The hope here is
          // that we replace all of the uses of the record
          // and we don't have to allocate it at all.
          for (const auto &[lab, ee] : rec->Record()) {
            if (lab == s) {
              // Must mark it so that the requisite bindings are
              // generated at the site of the let!
              (*kv)->was_used = true;
              progress->Simplified("known record field");
              return ee;
            }
          }
          LOG(FATAL) << "Projection of field " << s << " from var " <<
            x << ". The value of x is known, and it is "
            "a record, but it doesn't have that field. Exp:\n" <<
            ExpString(rec);
          return nullptr;
        }
      }
    }
    return pool->Project(s, e, guess);
  }

 private:
  const uint64_t opts = 0;
  Progress *progress = nullptr;
};

// Inlines globals, or drops unused ones.
struct GlobalInlining {
  GlobalInlining(uint64_t opts, AstPool *pool, Progress *progress) :
    opts(opts),
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

      // Note: O_GLOBAL_INLINING is not enabled after closure conversion.
      // We could support it, but we need to avoid substituting functions
      // since CALL takes a literal label.

      // TODO: Or if a small value?
      if (((opts & Simplification::O_GLOBAL_INLINING) && total_count == 1) ||
          ((opts & Simplification::O_GLOBAL_DEAD) && total_count == 0)) {
        progress->Simplified("drop/inline global");
        if (VERBOSE) {
          printf("  There were " AYELLOW("%d") " occurrences.\n",
                 total_count);
          printf("  Inlined sym is " APURPLE("%s") " = %s\n",
                 global.sym.c_str(), ExpStringShort(global.exp).c_str());
        }

        if (total_count > 0) {
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
        }

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
  const uint64_t opts = 0;
  AstPool *pool = nullptr;
  Progress *progress = nullptr;
};

struct FlattenLetPass : public il::Pass<> {
  FlattenLetPass(uint64_t opts, AstPool *p, Progress *progress) :
    Pass(p),
    opts(opts),
    progress(progress) {
  }

  virtual const Exp *DoLet(const std::vector<std::string> &tyvars,
                           const std::string &x,
                           const Exp *rhs_in,
                           const Exp *body_in,
                           const Exp *guess) {
    const Exp *rhs = DoExp(rhs_in);
    const Exp *body = DoExp(body_in);

    // Transform
    //   let val x = let val y = e1 in e2 end
    //   in body
    //   end
    // into
    //   let val y' = e1
    //   in let val x = [y'/y]e2
    //      in body
    //      end
    //   end
    // This is more idiomatic and better because we can apply
    // optimizations to the binding of x now (like known-value).
    if ((opts & Simplification::O_FLATTEN_LET) &&
        tyvars.empty() &&
        rhs->type == ExpType::LET) {
      const auto &[ytyvars, y, e1, e2] = rhs->Let();
      // Need to alpha-vary y in e2, in case the name y is also used
      // in 'body'.
      const auto &[new_y, new_e2] =
          ILUtil::AlphaVaryExp(pool, (int)ytyvars.size(), y, e2);
      progress->Simplified("flatten let");
      if (VERBOSE) {
        printf("  on " APURPLE("%s") " and "
               APURPLE("%s") " -> " APURPLE("%s") "\n",
               x.c_str(), y.c_str(), new_y.c_str());
      }
      return pool->Let(ytyvars, new_y, e1,
                       pool->Let(tyvars, x, new_e2, body));
    }
    return pool->Let(tyvars, x, rhs, body, guess);
  }

 private:
  const uint64_t opts = 0;
  Progress *progress = nullptr;
};

struct DecomposePass : public il::Pass<> {
  DecomposePass(uint64_t opts, AstPool *p, Progress *progress) :
    Pass(p),
    opts(opts),
    progress(progress) {
  }

  // Decompose an intcase into a series of primop tests.
  const Exp *DoIntCase(
      const Exp *obj,
      const std::vector<std::pair<BigInt, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if (!(opts & Simplification::O_DECOMPOSE_INTCASE))
      return Pass::DoIntCase(obj, arms, def, guess);

    std::string objvar = pool->NewVar("intcase_obj");
    const Exp *objvarexp = pool->Var({}, objvar);

    const Exp *body = DoExp(def);
    // PERF: If there are many cases, we should at least do
    // binary search.
    for (const auto &[bi, e] : arms) {
      body = pool->If(pool->Primop(Primop::INT_EQ,
                                   {}, {pool->Int(bi), objvarexp}),
                      DoExp(e),
                      body);
    }

    progress->Simplified("decompose intcase");
    return pool->Let({}, objvar, DoExp(obj),
                     body);
  }

  // Same, for StringCase.
  const Exp *DoStringCase(
      const Exp *obj,
      const std::vector<std::pair<std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if (!(opts & Simplification::O_DECOMPOSE_STRINGCASE))
      return Pass::DoStringCase(obj, arms, def, guess);

    std::string objvar = pool->NewVar("strcase_obj");
    const Exp *objvarexp = pool->Var({}, objvar);

    const Exp *body = DoExp(def);
    // PERF: If there are many cases, we should at least do
    // binary search. For stringcase there could be many tricks,
    // like checking specific informative characters in the
    // strings.
    for (const auto &[str, e] : arms) {
      body = pool->If(pool->Primop(Primop::STRING_EQ,
                                   {}, {pool->String(str), objvarexp}),
                      DoExp(e),
                      body);
    }

    progress->Simplified("decompose stringcase");
    return pool->Let({}, objvar, DoExp(obj),
                     body);
  }


 private:
  const uint64_t opts = 0;
  Progress *progress = nullptr;
};

}  // namespace

Program Simplification::Simplify(const Program &program_in,
                                 uint64_t opts) {
  Progress progress;
  Program program = program_in;
  DecomposePass decompose(opts, pool, &progress);
  PeepholePass peephole(opts, pool, &progress);
  KnownPass known(opts, pool, &progress);
  FlattenLetPass flatten_let(opts, pool, &progress);
  GlobalInlining global_inlining(opts, pool, &progress);


  // Do decomposition first if enabled. This only needs
  // to be done once, since other passes should not reintroduce
  // these (we might want to be explicit about that?).
  constexpr uint64_t ANY_DECOMPOSE =
    O_DECOMPOSE_INTCASE;

  if (opts & ANY_DECOMPOSE) {
    program = decompose.DoProgram(program);
  }

  do {
    progress.Reset();
    if (VERBOSE) printf(AWHITE("Peephole") ".\n");
    program = peephole.DoProgram(program);

    constexpr uint64_t ANY_KNOWN =
      O_EXPLODE_RECORDS;

    if (opts & ANY_KNOWN) {
      if (VERBOSE) printf(AWHITE("Known") ".\n");
      program = known.DoProgram(program, Known());
    }

    if (opts & O_FLATTEN_LET) {
      if (VERBOSE) printf(AWHITE("Flatten let") ".\n");
      program = flatten_let.DoProgram(program);
    }

    constexpr uint64_t ANY_GLOBAL =
      O_GLOBAL_INLINING |
      O_GLOBAL_DEAD;

    if (opts & ANY_GLOBAL) {
      if (VERBOSE) printf(AWHITE("Global") ".\n");
      program = global_inlining.Run(program);
    }

    if (VERBOSE) {
      printf("\n" AYELLOW("After simplification:\n"));
      printf("%s\n", ProgramString(program).c_str());
    }

  } while (progress.MadeProgress());

  return program;
}

}  // namespace il
