
#include "flatten-globals.h"

#include <string>
#include <utility>
#include <vector>


#include "bignum/big.h"
#include "il.h"
#include "base/logging.h"
#include "base/stringprintf.h"

namespace il {
namespace {

struct FlattenPass {
  FlattenPass(AstPool *pool) : pool(pool) {}

  const Exp *DoExp(const Exp *e,
                   const Type *t,
                   const std::vector<std::string> &tyvars,
                   const std::vector<const Type *> &tys,
                   const std::string &hint,
                   bool toplevel) {
#   define ARGS t, tyvars, tys, hint, toplevel
    switch (e->type) {
    case ExpType::STRING: return DoString(e->String(), e, ARGS);
    case ExpType::FLOAT: return DoFloat(e->Float(), e, ARGS);
    case ExpType::RECORD: return DoRecord(e->Record(), e, ARGS);
    case ExpType::INT: return DoInt(e->Int(), e, ARGS);
    case ExpType::BOOL: return DoBool(e->Bool(), e, ARGS);
    case ExpType::GLOBAL_SYM: return e;

    case ExpType::INJECT: {
      const auto &[lab, t, exp] = e->Inject();
      return DoInject(lab, t, exp, e, ARGS);
    }
    case ExpType::ROLL: {
      const auto &[type, exp] = e->Roll();
      return DoRoll(type, exp, e, ARGS);
    }
    case ExpType::PACK: {
      const auto &[t_hidden, alpha, t_packed, exp] = e->Pack();
      return DoPack(t_hidden, alpha, t_packed, exp, e, ARGS);
    }
    default:
      LOG(FATAL) << "In FlattenGlobals, encountered a global that is "
        "not expected (only expect values here) or not handled (yet?): " <<
        ExpString(e);
      return nullptr;
    }
#   undef ARGS
  }

  const Exp *DoRoll(const Type *t, const Exp *e,
                    const Exp *guess,
                    const Type *type_of_guess,
                    const std::vector<std::string> &tyvars,
                    const std::vector<const Type *> &tys,
                    const std::string &hint,
                    bool toplevel) {
    // DCHECK(TypeEq(t, type_of_guess));
    const Type *unrolled = pool->UnrollType(t);
    return pool->Roll(t, DoExp(e, unrolled, tyvars, tys, hint, toplevel),
                      guess);
  }

  const Exp *DoPack(const Type *t_hidden,
                    const std::string &alpha,
                    const Type *t_packed,
                    const Exp *body,
                    const Exp *guess,
                    const Type *type_of_guess,
                    const std::vector<std::string> &tyvars,
                    const std::vector<const Type *> &tys,
                    const std::string &hint, bool toplevel) {
    // DCHECK(TypeEq(pool->Exists(alpha, t_packed), type_of_guess));
    const Type *body_type = pool->SubstType(t_hidden, alpha, t_packed);

    return pool->Pack(t_hidden, alpha, t_packed,
                      DoExp(body, body_type, tyvars, tys, hint, toplevel),
                      guess);
  }

  const Exp *DoRecord(
      const std::vector<std::pair<std::string, const Exp *>> &fields,
      const Exp *guess,
      const Type *t,
      const std::vector<std::string> &tyvars,
      const std::vector<const Type *> &tys,
      const std::string &hint,
      bool toplevel) {
    CHECK(t->type == TypeType::RECORD);
    auto FieldType = [&](const std::string &lab) -> const Type * {
        for (const auto &[l, ft] : t->Record()) {
          if (l == lab) return ft;
        }
        LOG(FATAL) << "Type error: (" << hint <<
          ") Record had field " << lab <<
          " but its alleged type did not. Type:\n" <<
          TypeString(t) << "Record exp:\n" <<
          ExpString(guess);

        return nullptr;
      };

    std::vector<std::pair<std::string, const Exp *>> new_fields;
    new_fields.reserve(fields.size());
    for (const auto &[l, e] : fields) {
      new_fields.emplace_back(
          l,
          DoExp(e, FieldType(l), tyvars, tys, hint, false));
    }

    if (toplevel) {
      return pool->Record(new_fields, guess);
    } else {
      Global global;
      global.tyvars = tyvars;
      global.sym = pool->NewVar(hint);
      global.exp = pool->Record(new_fields, guess);
      global.type = t;
      globals_added.emplace_back(global);
      return pool->GlobalSym(tys, global.sym);
    }
  }

  const Exp *DoInject(const std::string &s,
                      const Type *t,
                      const Exp *e,
                      const Exp *guess,
                      const Type *type_of_guess,
                      const std::vector<std::string> &tyvars,
                      const std::vector<const Type *> &tys,
                      const std::string &hint,
                      bool toplevel) {
    // DCHECK(TypeEq(t, type_of_guess));
    LOG(FATAL) << "Unimplemented: Global inject: " << ExpString(e);
    // return pool->Inject(s, DoType(t, args...), DoExp(e, args...), guess);
  }

  const Exp *AddValue(const Exp *guess,
                      const Type *t,
                      const std::vector<std::string> &tyvars,
                      const std::vector<const Type *> &tys,
                      const std::string &hint) {
    Global global;
    global.tyvars = tyvars;
    global.sym = pool->NewVar(hint);
    global.exp = guess;
    global.type = t;
    globals_added.emplace_back(global);
    return pool->GlobalSym(tys, global.sym);
  }

  // If values are found inside the record, make them into globals
  // and return a global reference to them.
  const Exp *DoString(const std::string &s,
                      const Exp *guess,
                      const Type *t,
                      const std::vector<std::string> &tyvars,
                      const std::vector<const Type *> &tys,
                      const std::string &hint,
                      bool toplevel) {
    return AddValue(guess, t, tyvars, tys, hint);
  }

  const Exp *DoFloat(double d, const Exp *guess,
                     const Type *t,
                     const std::vector<std::string> &tyvars,
                     const std::vector<const Type *> &tys,
                     const std::string &hint,
                     bool toplevel) {
    return AddValue(guess, t, tyvars, tys, hint);
  }

  const Exp *DoInt(BigInt i, const Exp *guess,
                   const Type *t,
                   const std::vector<std::string> &tyvars,
                   const std::vector<const Type *> &tys,
                   const std::string &hint,
                   bool toplevel) {
    return AddValue(guess, t, tyvars, tys, hint);
  }

  const Exp *DoBool(bool b, const Exp *guess,
                    const Type *t,
                    const std::vector<std::string> &tyvars,
                    const std::vector<const Type *> &tys,
                    const std::string &hint,
                    bool toplevel) {
    return AddValue(guess, t, tyvars, tys, hint);
  }

  std::vector<Global> globals_added;
 private:
  AstPool *pool;
};
}  // namespace

FlattenGlobals::FlattenGlobals(AstPool *pool) : pool(pool) {

}

void FlattenGlobals::SetVerbose(int v) {
  verbose = v;
}

Program FlattenGlobals::Flatten(const Program &pgm) {
  FlattenPass pass(pool);
  Program flat_pgm;
  flat_pgm.body = pgm.body;
  for (const Global &global : pgm.globals) {
    Global gg;
    gg.sym = global.sym;

    // If the global is polymorphic, every global
    // generated while flattening it will be polymorphic
    // with the same type variables, and instantiated
    // at those same (as type variable occurrences).
    gg.tyvars = global.tyvars;
    std::vector<const Type *> tys;
    tys.reserve(gg.tyvars.size());
    for (const std::string &v : gg.tyvars)
      tys.push_back(pool->VarType(v, {}));

    // And therefore the type is unchanged.
    gg.type = global.type;

    switch (global.exp->type) {
    case ExpType::STRING:
    case ExpType::FLOAT:
    case ExpType::INT:
    case ExpType::BOOL:
    case ExpType::FN:
      gg.exp = global.exp;
      break;
    default:
      gg.exp = pass.DoExp(global.exp, global.type, gg.tyvars, tys, gg.sym,
                          // At the toplevel. This means we will emit
                          // an actual record the first time, but then
                          // nested records get spilled as globals.
                          true);
    }

    flat_pgm.globals.push_back(std::move(gg));
  }

  // And emit any globals that we generated for subcomponents.
  for (Global &global : pass.globals_added) {
    CHECK(!global.sym.empty());
    flat_pgm.globals.push_back(std::move(global));
  }
  return flat_pgm;
}

}  // namespace il
