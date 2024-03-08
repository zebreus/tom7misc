
#include "to-bytecode.h"

#include <cctype>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "il.h"
#include "bytecode.h"

#include "functional-map.h"
#include "base/logging.h"
#include "base/stringprintf.h"

namespace bc {
namespace {

struct Converter {

  static constexpr const char *INJ_LABEL = "#";
  static constexpr const char *INJ_VALUE = "$";

  static constexpr const char *REF_LABEL = "r";

  // Object fields are distinguished by their types. We just encode this in
  // the field name. We could use anything here, but we use something that's
  // illegal to begin a user field name and reminiscent of the type.
  static std::string ObjFieldName(const std::string &f, il::ObjFieldType oft) {
    const char c = [oft](){
        switch (oft) {
        case il::ObjFieldType::STRING: return '\"';
        case il::ObjFieldType::FLOAT: return '.';
        case il::ObjFieldType::INT: return '0';
        case il::ObjFieldType::BOOL: return '?';
        case il::ObjFieldType::OBJ: return '=';
        }
      }();
    return StringPrintf("%c%s", c, f.c_str());
  };

  // We actually keep code, data, and local symbols disjoint (even
  // though this is not required) to prevent conclusion.
  std::string NewSymbol(const std::string &hint) {
    std::string base = hint.substr(0, hint.find('$'));
    if (base.empty()) base = "l";
    if (!std::isalpha(base[0])) base = "l" + base;

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

  std::unordered_set<std::string> il_fn_labels;
  void MarkGlobalIfFn(const il::Global &global) {
    if (global.exp->type == il::ExpType::FN) {
      il_fn_labels.insert(global.sym);
    }
  }

  void AddData(const std::string &lab,
               Value value) {
    CHECK(!program.data.contains(lab)) << lab;
    program.data[lab] = std::move(value);
  }

  void ConvertGlobal(const il::Global &global) {
    CHECK(!global.sym.empty());
    const il::Exp *exp = EraseTypes(global.exp);
    if (il_fn_labels.contains(global.sym)) {
      const auto &[self, x, arrow_type, body] = exp->Fn();
      ConvertFn(global.sym, x, body, false);
    } else {
      CHECK(!emitted_main) << "Can't convert a data global after "
        "emitting main, since we need to insert initialization "
        "instructions.";
      const std::string label = GetLabel(global.sym);
      switch (exp->type) {
      case il::ExpType::STRING:
        AddData(label, Value{.v = {exp->String()}});
        break;
      case il::ExpType::FLOAT:
        AddData(label, Value{.v = {exp->Float()}});
        break;
      case il::ExpType::INT:
        AddData(label, Value{.v = {exp->Int()}});
        break;
      case il::ExpType::BOOL: {
        const uint64_t u = exp->Bool() ? 1 : 0;
        AddData(label, Value{.v = {u}});
        break;
      }
      case il::ExpType::RECORD: {
        std::string tmp = NewSymbol(label);
        // Allocate an empty map in the heap. Store it in globals.
        init_alloc_code.emplace_back(inst::Alloc{.out = tmp});
        init_alloc_code.emplace_back(
            inst::Save{.global = label, .arg = tmp});
        // After everything is allocated, write the fields; these
        // can form cycles.
        const std::vector<std::pair<std::string, const il::Exp *>> &fields =
          exp->Record();
        std::string tmp_load = NewSymbol(label);
        for (const auto &[lab, e] : fields) {
          CHECK(e->type == il::ExpType::GLOBAL_SYM) << "A global record "
            "may only have global_sym fields. A previous pass is supposed "
            "to ensure this: " << global.sym;
          const auto &[ts, other_label] = e->GlobalSym();
          const std::string other_init_global = GetLabel(other_label);
          if (il_fn_labels.contains(other_label)) {
            // (load "function pointer")
            std::string tmp_fn =
              AddValue(other_label,
                       Value{.v = Value::t(other_init_global)},
                       &init_write_code);
            init_write_code.emplace_back(
                inst::SetLabel{.obj = tmp, .lab = lab, .arg = tmp_fn});

          } else {
            // PERF: This value will already be loaded in a local somewhere,
            // since we do all the initialization in the same straight-line
            // code. So we could skip this and reference the local if we
            // just knew what it was called.
            init_write_code.emplace_back(
                inst::Load{.out = tmp_load, .global = other_init_global});
            init_write_code.emplace_back(
                inst::SetLabel{.obj = tmp, .lab = lab, .arg = tmp_load});

          }

        }

        // This does not add anything to the data segment.
        break;
      }

      default:
        LOG(FATAL) << "Unimplemented type of global: "
                   << global.sym << " = "
                   << ExpString(exp);
      }
    }
  }

  std::vector<Inst> init_alloc_code;
  std::vector<Inst> init_write_code;

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

  // Convert the function to code, storing it in the program at the
  // given label. The function is (fn x => body). If is_main is true,
  // then we insert the one-time initialization code at the front of
  // the instruction stream.
  bool emitted_main = false;
  void ConvertFn(const std::string &label,
                 // fn x => body
                 const std::string &x,
                 const il::Exp *body,
                 bool is_main) {
    const std::string code_lab = GetLabel(label);
    std::vector<Inst> insts;

    if (is_main) {
      CHECK(!emitted_main);
      // emit initialization instructions.
      for (Inst &inst : init_alloc_code)
        insts.push_back(std::move(inst));
      init_alloc_code.clear();
      for (Inst &inst : init_write_code)
        insts.push_back(std::move(inst));
      init_write_code.clear();
      emitted_main = true;

      insts.emplace_back(inst::Note{.msg = "End initialization"});
    }

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

  std::string AddValue(const std::string &hint,
                       const Value &value,
                       std::vector<Inst> *insts) {
    std::string lab = NewSymbol(hint);
    // TODO: Coalesce if already present!
    CHECK(!program.data.contains(lab)) << lab;
    program.data[lab] = value;
    std::string local = NewSymbol(hint);
    insts->emplace_back(inst::Load{.out = local, .global = lab});
    return local;
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
        const std::string &s = exp->String();
        return AddValue("str", Value{.v = Value::t(s)}, insts);
      }

      case il::ExpType::FLOAT: {
        const double d = exp->Float();
        return AddValue("b", Value{.v = Value::t(d)}, insts);
      }

      case il::ExpType::JOIN: {
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::RECORD: {
        const std::vector<std::pair<std::string, const il::Exp *>> &fields =
          exp->Record();

        // PERF: Should represent empty record as just 0.

        // Evaluate components.
        std::vector<std::string> locals;
        locals.reserve(fields.size());
        for (const auto &[f, e] : fields) {
          locals.push_back(ConvertExp(G, f, e, insts));
        }

        // Now allocate the record.
        std::string r = NewSymbol("rec");
        insts->emplace_back(inst::Alloc{.out = r});

        // And set fields within it.
        for (int i = 0; i < (int)locals.size(); i++) {
          insts->emplace_back(inst::SetLabel{
              .obj = r,
              .lab = fields[i].first,
              .arg = locals[i]
            });
        }

        return r;
      }

      case il::ExpType::OBJECT: {
        const std::vector<std::tuple<std::string,
                                     il::ObjFieldType,
                                     const il::Exp *>> &fields = exp->Object();

        // Evaluate components.
        std::vector<std::string> locals;
        std::vector<std::string> typed_field_names;
        locals.reserve(fields.size());
        typed_field_names.reserve(fields.size());
        for (const auto &[f, oft, e] : fields) {
          locals.push_back(ConvertExp(G, f, e, insts));

          typed_field_names.push_back(ObjFieldName(f, oft));
        }

        // Now allocate the obj.
        std::string r = NewSymbol("obj");
        insts->emplace_back(inst::Alloc{.out = r});

        // And set fields within it.
        CHECK(typed_field_names.size() == locals.size());
        for (int i = 0; i < (int)locals.size(); i++) {
          insts->emplace_back(inst::SetLabel{
              .obj = r,
              .lab = typed_field_names[i],
              .arg = locals[i],
            });
        }

        return r;
      }

      case il::ExpType::HAS: {
        const auto &[e, field, oft] = exp->Has();
        LOG(FATAL) << "unimplemented!";
      }

      case il::ExpType::GET: {
        const auto &[e, field, oft] = exp->Get();
        LOG(FATAL) << "unimplemented!";
      }

      case il::ExpType::INT: {
        const auto &bi = exp->Int();
        return AddValue("i", Value{.v = Value::t(bi)}, insts);
      }

      case il::ExpType::BOOL: {
        uint64_t u = exp->Bool() ? 1 : 0;
        return AddValue("b", Value{.v = Value::t(u)}, insts);
      }

      case il::ExpType::VAR: {
        const auto &[tvs_, x] = exp->Var();
        const std::string *lab = G.FindPtr(x);
        CHECK(lab != nullptr) << "Bug: When converting to bytecode, "
          "the variable " << x << " was not computed in any local.";
        return *lab;
      }

      case il::ExpType::GLOBAL_SYM: {
        const auto &[ts, sym] = exp->GlobalSym();
        std::string label = GetLabel(sym);

        if (il_fn_labels.contains(sym)) {
          // Code labels are treated differently. We load the name of
          // the function ("function pointer") as the value.
          return AddValue(sym, Value{.v = Value::t(label)}, insts);
        } else {
          const std::string out = NewSymbol(sym);
          insts->emplace_back(inst::Load{.out = out, .global = label});
          return out;
        }
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
        const auto &[fn, arg] = exp->App();
        const std::string f = ConvertExp(G, "f", fn, insts);
        const std::string x = ConvertExp(G, "arg", arg, insts);
        const std::string res = NewSymbol("app");
        insts->emplace_back(inst::Call{.out = res, .f = f, .arg = x});
        return res;
      }

      case il::ExpType::PROJECT: {
        const auto &[lab, e] = exp->Project();
        const std::string r = ConvertExp(G, "rec", e, insts);
        const std::string res = NewSymbol(lab);
        insts->emplace_back(inst::GetLabel{.out = res, .obj = r, .lab = lab});
        return res;
      }

      case il::ExpType::INJECT: {
        const auto &[lab, t, e] = exp->Inject();
        // Sum is a represented as a map with the label and boxed value.
        const std::string elocal = ConvertExp(G, "inj", e, insts);
        const std::string rec = NewSymbol(lab);
        insts->emplace_back(inst::Alloc{.out = rec});
        const std::string lab_value =
          AddValue(lab, Value{.v = Value::t(lab)}, insts);
        insts->emplace_back(inst::SetLabel({
              .obj = rec, .lab = INJ_LABEL, .arg = lab_value}));
        insts->emplace_back(inst::SetLabel({
              .obj = rec, .lab = INJ_VALUE, .arg = elocal}));
        return rec;
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

        // Some primops are translated away natively.
        switch (po) {
        case Primop::REF: {
          CHECK(ls.size() == 1);
          insts->emplace_back(inst::Alloc{.out = out});
          insts->emplace_back(
              inst::SetLabel{.obj = out, .lab = REF_LABEL, .arg = ls[0]});
          return out;
        }

        case Primop::REF_GET: {
          CHECK(ls.size() == 1);
          insts->emplace_back(
              inst::GetLabel{.out = out, .obj = ls[0], .lab = REF_LABEL});
          return out;
        }

        case Primop::REF_SET: {
          CHECK(ls.size() == 2);
          insts->emplace_back(
              inst::SetLabel{.obj = ls[0], .lab = REF_LABEL, .arg = ls[1]});
          return out;
        }

        default:
          // Handled as a generic primop, then.
          break;
        }

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
        const auto &[es, e] = exp->Seq();
        for (const il::Exp *ee : es)
          (void)ConvertExp(G, "ignore", ee, insts);
        exp = e;
        continue;
      }

      case il::ExpType::SUMCASE: {
        // Alas, we cannot decompose this one without adding some
        // unsafe or ugly primitives to IL, so we have to implement
        // it here.

        const auto &[obj, arms, def] = exp->SumCase();
        // std::vector<std::tuple<std::string, std::string, const Exp *>>

        const std::string obj_local = ConvertExp(G, "sc_obj", obj, insts);

        const std::string res = NewSymbol("sc_res");

        // Read the actual label just once.
        const std::string actual = NewSymbol("lab");
        insts->emplace_back(inst::GetLabel{
            .out = actual, .obj = obj_local, .lab = INJ_LABEL});
        // We could also read the contents once, but that seems
        // uglier since it would have different types on each branch.
        // We might do a future optimization where nullary constructors
        // don't even store data, for example. We also don't need to
        // load it in the default or (in principle) in arms where
        // it isn't used.

        // Each arm needs to jump to the code when there's a successful
        // match, and then back to the join point.
        std::vector<int> jump_match, jump_joins;
        jump_match.reserve(arms.size());
        jump_joins.reserve(arms.size());

        // We keep writing to this same temporary, a bool.
        const std::string test = NewSymbol("test");
        for (const auto &[lab, x, e] : arms) {
          const std::string lab_value =
            AddValue(lab, Value{.v = Value::t(lab)}, insts);
          // compare labels
          insts->emplace_back(inst::Binop{
              .primop = Primop::STRING_EQ,
              .out = test,
              .arg1 = actual,
              .arg2 = lab_value});
          // the match jump
          jump_match.push_back(ReserveInstruction(insts));
        }

        // If we got here, then this is the default.
        const std::string def_local = ConvertExp(G, "sc_def", def, insts);
        insts->emplace_back(inst::Bind({.out = res, .arg = def_local}));
        const int jump_def_join = ReserveInstruction(insts);

        // Code for each arm.
        for (int i = 0; i < (int)arms.size(); i++) {
          const auto &[lab, x, e] = arms[i];
          // Patch in the jump here.
          const int match_idx = insts->size();
          (*insts)[jump_match[i]] =
            Inst(inst::If{.cond = test, .true_idx = match_idx});

          // Unpack contained value.
          const std::string x_local = NewSymbol(x);
          insts->emplace_back(inst::GetLabel{
              .out = x_local,
              .obj = obj_local,
              .lab = INJ_VALUE,
            });
          // Convert arm's expression, with x bound to the contents.
          const std::string arm_local =
            ConvertExp(G.Insert(x, x_local), lab + "_arm", e, insts);
          insts->emplace_back(inst::Bind({.out = res, .arg = arm_local}));
          // Reserve slot to jump to join.
          CHECK((int)jump_joins.size() == i);
          jump_joins.push_back(ReserveInstruction(insts));
        }

        // Now the location of the join.
        const int join_idx = (int)insts->size();

        // So patch in the jumps.
        for (int i = 0; i < (int)jump_joins.size(); i++)
          (*insts)[jump_joins[i]] = Inst(inst::Jump({.idx = join_idx}));
        // including the default.
        (*insts)[jump_def_join] = Inst(inst::Jump({.idx = join_idx}));

        return res;
      }


      case il::ExpType::INTCASE: {
        LOG(FATAL) << "Expecting intcase to be compiled away by now.";
        return "ERROR";
      }

      case il::ExpType::STRINGCASE: {
        LOG(FATAL) << "Expecting stringcase to be compiled away by now.";
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

  // Translate labels first, and main first of those, since we want
  // co claim that exact label.
  conv.SetLabel("main");
  CHECK(conv.from_il_label["main"] == "main");
  for (const il::Global &global : pgm.globals)
    conv.SetLabel(global.sym);

  // We don't mark "main" as a function, as there is no corresponding
  // il symbol, and it is not legal to call it.
  for (const il::Global &global : pgm.globals) {
    conv.MarkGlobalIfFn(global);
  }

  for (const il::Global &global : pgm.globals) {
    conv.ConvertGlobal(global);
  }

  // Main has to be last so that it can consume the initialization code.
  conv.ConvertFn("main", "unused", pgm.body, true);

  return std::move(conv.program);
}

}  // namespace bc
