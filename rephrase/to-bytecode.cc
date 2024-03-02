
#include "to-bytecode.h"

#include "il.h"
#include "bytecode.h"

#include "functional-map.h"
#include "base/logging.h"
#include "base/stringprintf.h"

namespace bc {
namespace {

struct Converter {

  // We actually keep code, data, and local symbols disjoint (even
  // though this is not required) to prevent conclusion.
  std::string NewSymbol(const std::string &hint) {
    std::string base = hint.substr(0, hint.find('$'));
    if (base.empty()) base = "l";

    int &counter = labels[base];
    counter++;
    if (counter == 1) {
      // The first time we can skip the prefix, which makes code
      // look nicer.
      return base;
    } else {
      return StringPrintf("%s$%d", base.c_str(), counter);
    }
  }

  // Maps to the next used counter.
  std::unordered_map<std::string, int> labels;

  // Maps from IL global name to bytecode global name.
  std::unordered_map<std::string, std::string> from_il_label;
  const std::string &GetLabel(const std::string &il_label) {
    const auto it = from_il_label.find(il_label);
    CHECK(it != from_il_label.end()) << il_label;
    return it->second;
  }

  // Since the globals can all refer to each other, first we
  // translate each label to its bytecode label. These can
  // be looked up label with GetLabel.
  void SetLabel(const std::string &il_global_sym) {
    std::string out_label = NewSymbol(il_global_sym);
    CHECK(!from_il_label.contains(il_global_sym)) << il_global_sym;
    from_il_label[il_global_sym] = out_label;
  }

  void ConvertGlobal(const il::Global &global) {
    const std::string label = GetLabel(global.sym);

    std::vector<Inst> insts;

    // XXX!
  }

  // Several constructs are just for typing, and have no runtime
  // effect. So they can be completely ignored here.
  static const il::Exp *EraseTypes(const il::Exp *exp) {
    for (;;) {
      switch (exp->type) {
      case il::ExpType::PACK: {
        const auto &[ta, alpha, tb, e] = exp->Pack();
        exp = e;
        break;
      }
      // (Unpack still has to do its expression-level "let" behavior.)

      case il::ExpType::ROLL: {
        const auto &[t, e] = exp->Roll();
        exp = e;
        break;
      }
      case il::ExpType::UNROLL:
        exp = exp->Unroll();
        break;
      default:
        return exp;
      }
    }
  }

  Program program;

  using VarLocalMap = FunctionalMap<std::string, std::string>;

  // Reserve the next instruction slot to be overwritten later. Returns
  // the instruction index.
  int ReserveInstruction(std::vector<Inst> *insts) {
    size_t slot = insts->size();
    insts->emplace_back(inst::Fail{.arg = "Bug: Reserved"});
    return (int)slot;
  }

  void ConvertFn(const std::string &label,
                 // fn x => body
                 const std::string &x,
                 const il::Exp *body) {
    const std::string code_lab = GetLabel(label);
    std::vector<Inst> insts;
    VarLocalMap G;
    std::string arg_lab = NewSymbol(x);

    G = G.Insert(x, arg_lab);

    // Convert the body to instructions.
    const std::string res = ConvertExp(G, label + "_ret", body, &insts);
    // And return the result.
    insts.emplace_back(inst::Ret{.arg = res});

    CHECK(!program.code.contains(code_lab));
    program.code[code_lab] =
      std::make_pair(std::move(arg_lab), std::move(insts));
  }

  // Convert the expression by adding instructions at the end
  // of the instruction stream. Returns the local that contains
  // the expression's value.
  std::string ConvertExp(
      VarLocalMap G,
      const std::string &hint,
      const il::Exp *exp,
      std::vector<Inst> *insts) {

    // This is a loop so that we can get cheap tail calls for
    // common constructs like let and seq.
    for (;;) {
      exp = EraseTypes(exp);

      switch (exp->type) {
      case il::ExpType::STRING: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::FLOAT: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::JOIN: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::RECORD: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::INT: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::BOOL: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::VAR: {
        const auto &[tvs_, x] = exp->Var();
        const std::string *lab = G.FindPtr(x);
        CHECK(lab != nullptr) << "Bug: When converting to bytecode, "
          "the variable " << x << " was not computed in any local.";
        return *lab;
      }

      case il::ExpType::GLOBAL_SYM: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::LAYOUT: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::LET: {
        const auto &[tyvars_, x, rhs, body] = exp->Let();
        const std::string v = ConvertExp(G, x, rhs, insts);
        G = G.Insert(x, v);
        exp = body;
        continue;
      }

      case il::ExpType::UNPACK: {
        // This acts just like Let when we erase types.
        const auto &[alpha_, x, rhs, body] = exp->Unpack();
        const std::string v = ConvertExp(G, x, rhs, insts);
        G = G.Insert(x, v);
        exp = body;
        continue;
      }

      case il::ExpType::IF: {
        const auto &[cond, texp, fexp] = exp->If();
        const std::string condv = ConvertExp(G, "cond", cond, insts);
        const int if_idx = ReserveInstruction(insts);
        // We branch on true, so translate the false case here.
        const std::string res = NewSymbol("ifres");
        const std::string fres = ConvertExp(G, "f", fexp, insts);
        insts->emplace_back(inst::Bind{.out = res, .arg = fres});
        const int f_join_idx = ReserveInstruction(insts);
        // Now this is the location of the true branch.
        const int true_idx = (int)insts->size();
        (*insts)[if_idx] =
          Inst(inst::If({.cond = condv, .true_idx = true_idx}));
        // Emit the true branch.
        const std::string tres = ConvertExp(G, "t", texp, insts);
        insts->emplace_back(inst::Bind{.out = res, .arg = tres});
        // This is the join location. Both branches set the
        // local "res".
        const int ret_idx = (int)insts->size();
        (*insts)[f_join_idx] = Inst(inst::Jump{.idx = ret_idx});
        return res;
      }

      case il::ExpType::APP: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::PROJECT: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::INJECT: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::PRIMOP: {
        const auto &[po, ts_, es] = exp->Primop();
        const auto &[num_type_args, num_exp_args] = PrimopArity(po);
        CHECK(num_exp_args == (int)es.size());

        std::vector<std::string> ls;
        ls.reserve(es.size());
        for (const il::Exp *e : es) {
          ls.push_back(ConvertExp(G, "p", e, insts));
        }

        std::string out = NewSymbol("po");

        switch (num_exp_args) {
        case 1:
          insts->emplace_back(
              inst::Unop{.primop = po, .out = out, .arg = ls[0]});
          return out;
        case 2:
          insts->emplace_back(
              inst::Binop{
                .primop = po, .out = out,
                .arg1 = ls[0], .arg2 = ls[1]});
          return out;
        default:
          LOG(FATAL) << "Unimplemented primop arity " << num_exp_args;
        }
        return "ERROR";
      }

      case il::ExpType::FAIL: {
        const auto &[msg, type_] = exp->Fail();
        const std::string v = ConvertExp(G, "msg", msg, insts);
        insts->emplace_back(inst::Fail{.arg = v});
        // Should have a way to indicate that the expression
        // can't return?
        //
        // Something valid, at least...
        return v;
      }

      case il::ExpType::SEQ: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::INTCASE: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::STRINGCASE: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::SUMCASE: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }


      case il::ExpType::FN:
        LOG(FATAL) << "Function expressions should have been eliminated by "
          "closure conversion, except for the top-level globals (which are "
          "handled separately.";
        return "ERROR";
        break;

      case il::ExpType::PACK:
      case il::ExpType::ROLL:
      case il::ExpType::UNROLL:
        LOG(FATAL) << "Should have been handled by EraseTypes.";
        return "ERROR";
        break;

      default:
        LOG(FATAL) << "Unhandled expression type in ToBytecode::ConvertExp.";
        return "ERROR";
        break;
      }
    }
  }
};

}  // namespace

ToBytecode::ToBytecode() {}

void ToBytecode::SetVerbose(int verbose_in) {
  verbose = verbose_in;
}

// To convert an IL program, we

Program ToBytecode::Convert(const il::Program &pgm) {
  Converter conv;
  il::AstPool tmp_pool;

  for (const il::Global &global : pgm.globals) {
    CHECK(global.sym != "main") << "The symbol 'main' is special "
      "so there cannot be a global called exactly that. Some upstream "
      "code should just avoid generating it.";
  }

  #if 0
  // For uniformity, we translate every code global as a function.
  il::Global main;
  main.tyvars = {};
  main.sym = "main";
  main.type =
    tmp_pool.Arrow(tmp_pool.RecordType({}),
                   tmp_pool.RecordType({}));
  main.exp =
    tmp_pool.Fn("", "arg$",
                main.type,
                // Wrap so that the body is ignored, to match the
                // type {} -> {} that we just assigned it. This probably
                // isn't necessary.
                tmp_pool.Seq({pgm.body},
                             tmp_pool.Record({})));
  #endif

  // Translate labels first, and main first of those, since we want
  // co claim that exact label.
  conv.SetLabel("main");
  CHECK(conv.from_il_label["main"] == "main");
  for (const il::Global &global : pgm.globals)
    conv.SetLabel(global.sym);

  conv.ConvertFn("main", "unused", pgm.body);

  for (const il::Global &global : pgm.globals) {
    conv.ConvertGlobal(global);
  }

  return {};
}

}  // namespace bc
