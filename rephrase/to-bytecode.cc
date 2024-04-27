
#include "to-bytecode.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <tuple>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdint>

#include "bignum/big.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bc.h"
#include "functional-map.h"
#include "il.h"
#include "primop.h"

namespace bc {
namespace {

static inline bc::ObjectFieldType ILOftToBC(il::ObjFieldType oft) {
  switch (oft) {
  case il::ObjFieldType::STRING: return bc::ObjectFieldType::STRING;
  case il::ObjFieldType::FLOAT: return bc::ObjectFieldType::FLOAT;
  case il::ObjFieldType::INT: return bc::ObjectFieldType::INT;
  case il::ObjFieldType::BOOL: return bc::ObjectFieldType::BOOL;
  case il::ObjFieldType::OBJ: return bc::ObjectFieldType::OBJ;
  case il::ObjFieldType::LAYOUT: return bc::ObjectFieldType::LAYOUT;
  }
  LOG(FATAL) << "Need to update bc::ObjectFieldType and conversion from IL";
  return bc::ObjectFieldType::STRING;
}

struct Converter {

  static constexpr const char *INJ_LABEL = "#";
  static constexpr const char *INJ_VALUE = "$";

  static constexpr const char *REF_LABEL = "r";

  // Object fields are distinguished by their types. We just encode this in
  // the field name. We could use anything here, but we use something that's
  // illegal to begin a user field name and reminiscent of the type.
  static std::string ObjFieldName(const std::string &f, il::ObjFieldType oft) {
    const char c = ObjectFieldTypeTag(ILOftToBC(oft));
    return StringPrintf("%c%s", c, f.c_str());
  };

  // We actually keep code, data, and local symbols disjoint (even
  // though this is not required) to prevent any confusion.
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
              AddAndLoadValueToInsts(other_label,
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

      case il::ExpType::TYPEFN: {
        const auto &[alpha, body] = exp->TypeFn();
        exp = body;
        break;
      }

      case il::ExpType::TYPEAPP: {
        const auto &[e, t] = exp->TypeApp();
        exp = e;
        break;
      }

      default:
        return exp;
      }
    }
  }

  SymbolicProgram program;

  using VarLocalMap = FunctionalMap<std::string, std::string>;

  // Reserve the next instruction slot to be overwritten later. Returns
  // the instruction index.
  // XXX should be unnecessary for symbolic code
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
    // The label for the "function"
    const std::string code_lab = GetLabel(label);

    std::unordered_map<std::string, Block> blocks;

    VarLocalMap G;
    std::string arg_lab = NewSymbol(x);

    G = G.Insert(x, arg_lab);

    // The label for the first block.
    const std::string body_label = NewSymbol(label);
    blocks[body_label] = Block{};

    Block *out_block = &blocks[body_label];

    // Convert the body to instructions.
    const std::string res =
      ConvertExp(G, label + "_ret", body, &blocks, out_block);
    // And return the result.
    out_block->insts.emplace_back(inst::Ret{.arg = res});

    // Maybe insert initialization code ahead of the first block.
    std::string start_label = body_label;
    if (is_main) {
      CHECK(!emitted_main);
      Block init_block;

      init_block.insts.emplace_back(inst::Note{.msg = "Initialization"});
      // emit initialization instructions.
      for (Inst &inst : init_alloc_code)
        init_block.insts.push_back(std::move(inst));
      init_alloc_code.clear();
      for (Inst &inst : init_write_code)
        init_block.insts.push_back(std::move(inst));
      init_write_code.clear();
      emitted_main = true;

      init_block.insts.emplace_back(inst::Note{.msg = "End initialization"});
      init_block.insts.emplace_back(inst::SymbolicJump{.lab = body_label});

      const std::string init_label = NewSymbol("init");
      start_label = init_label;
      blocks[init_label] = std::move(init_block);
    }

    CHECK(!program.code.contains(code_lab));
    SymbolicFn fn;
    fn.arg = std::move(arg_lab);
    fn.initial = start_label;
    fn.blocks = std::move(blocks);
    program.code[code_lab] = std::move(fn);
  }

  // Returns the name of the data global. Maybe reuses one that's already
  // there.
  std::string AddValue(const std::string &hint,
                       const Value &value) {
    std::string lab = NewSymbol(hint);
    // TODO: Coalesce if already present!
    CHECK(!program.data.contains(lab)) << lab;
    program.data[lab] = value;
    return lab;
  }

  // Returns the new local which has been loaded with the value.
  std::string AddAndLoadValueToInsts(const std::string &hint,
                                     const Value &value,
                                     std::vector<Inst> *insts) {
    const std::string lab = AddValue(hint, value);
    std::string local = NewSymbol(hint);
    insts->emplace_back(inst::Load{.out = local, .global = lab});
    return local;
  }

  std::string AddAndLoadValue(const std::string &hint,
                              const Value &value,
                              Block *current_block) {
    return AddAndLoadValueToInsts(hint, value, &current_block->insts);
  }


  // Convert the expression by adding instructions at the end
  // of the instruction stream inside the argument block. Can
  // add blocks to the map of basic blocks for this function.
  // Returns the local that contains the expression's value.
  //
  // Note: The current_block argument is by reference; conversion of
  // something like an IF requires branching to new blocks and then
  // joining to another new block for the result.
  std::string ConvertExp(
      VarLocalMap G,
      const std::string &hint,
      const il::Exp *exp,
      std::unordered_map<std::string, Block> *blocks,
      Block *&current_block) {

    // This is a loop so that we can get cheap tail calls for
    // common constructs like let and seq.
    for (;;) {
      exp = EraseTypes(exp);

      switch (exp->type) {
      case il::ExpType::STRING: {
        const std::string &s = exp->String();
        return AddAndLoadValue("str", Value{.v = Value::t(s)}, current_block);
      }

      case il::ExpType::FLOAT: {
        const double d = exp->Float();
        return AddAndLoadValue("b", Value{.v = Value::t(d)}, current_block);
      }

      case il::ExpType::NODE: {
        const auto &[attrs, children] = exp->Node();
        const std::string lattrs = ConvertExp(G, "at", attrs, blocks,
                                              current_block);

        // Node automatically flattens children (i.e., makes their
        // children part of the vector) if they have no attributes.
        // We only need to do this once, since nodes within will
        // also be in this normal form.

        // Build up the children vector.
        std::string v = NewSymbol("ch");
        current_block->insts.emplace_back(inst::AllocVec{.out = v});

        // Current child index as we write elements into the vector.
        std::string zero = AddValue("zero", Value({.v = Value::t(BigInt(0))}));
        std::string one = AddValue("one", Value({.v = Value::t(BigInt(1))}));
        std::string idx = NewSymbol("idx");
        current_block->insts.emplace_back(
            inst::Load{.out = idx, .global = zero});
        // Increment. This stays constant and is used for the outer and
        // inner loops.
        std::string inc = NewSymbol("inc");
        current_block->insts.emplace_back(
            inst::Load{.out = inc, .global = one});

        // Index within the child's children when we flatten elements.
        // We keep reusing this one, so it doesn't get loaded until we
        // start an inner loop.
        const std::string num_subchildren = NewSymbol("sn");
        const std::string subidx = NewSymbol("si");
        const std::string subcond = NewSymbol("sc");
        const std::string subchild = NewSymbol("cc");
        const std::string child_vec = NewSymbol("cv");

        const std::string child_is_text = NewSymbol("ctext");
        const std::string child_is_blank = NewSymbol("cblank");
        const std::string child_attrs = NewSymbol("cattrs");
        const std::string child_attrs_empty = NewSymbol("caempty");

        int child_idx = 0;
        for (const il::Exp *child : children) {
          const std::string local =
            ConvertExp(G, StringPrintf("c%d", child_idx), child,
                       blocks, current_block);
          // Several possibilities:
          // 1. A text node. Ideally we would merge this into the previous
          // node if it is a text node, but I'm not doing this yet.
          //
          // 2. A node with attributes. This is just copied in as the next
          // child.
          //
          // 3. A trivial node (no attributes). Its children are appended.

          std::string text_lab = NewSymbol("text");
          Block *text_block = &(*blocks)[text_lab];

          std::string trivial_lab = NewSymbol("trivial");
          Block *trivial_block = &(*blocks)[trivial_lab];

          // Like any other conditional, we join up to a single block.
          std::string join_lab = NewSymbol("join_join");
          Block *join_block = &(*blocks)[join_lab];

          current_block->insts.emplace_back(
              inst::Unop{
                .primop = Primop::IS_TEXT,
                .out = child_is_text,
                .arg = local});

          current_block->insts.emplace_back(
              inst::SymbolicIf{.cond = child_is_text, .true_lab = text_lab});

          // Get the attributes object.
          current_block->insts.emplace_back(
              inst::Unop{
                .primop = Primop::GET_ATTRS,
                .out = child_attrs,
                .arg = local});
          current_block->insts.emplace_back(
              inst::Unop{
                .primop = Primop::OBJ_EMPTY,
                .out = child_attrs_empty,
                .arg = child_attrs});

          current_block->insts.emplace_back(
              inst::SymbolicIf{
                .cond = child_attrs_empty,
                .true_lab = trivial_lab
              });

          // If we get here, then it's a normal node. Insert it.
          current_block->insts.emplace_back(
              inst::SetVec{.vec = v, .idx = idx, .arg = local});
          // PERF: We'll increment the index on multiple branches. Maybe
          // there's a way to share code. But probably better to spend
          // the time writing an optimizer?
          current_block->insts.emplace_back(
              inst::Binop{
                  .primop = Primop::INT_PLUS,
                  .out = idx,
                  .arg1 = idx,
                  .arg2 = inc,
                });

          current_block->insts.emplace_back(
              inst::SymbolicJump({.lab = join_lab}));

          // std::vector<int> join_jmps = {ReserveInstruction(insts)};

          // Now emit the code for text nodes.

          // An empty text node is an empty node. These are dropped.
          text_block->insts.emplace_back(
              inst::Unop{
                .primop = Primop::STRING_EMPTY,
                .out = child_is_blank,
                .arg = local,
              });
          // if child_is_blank continue
          text_block->insts.emplace_back(
              inst::SymbolicIf{
                .cond = child_is_blank,
                .true_lab = join_lab,
              });

          // TODO: If the previous node was text, append this text
          // to it instead of pushing a whole new node. Now that we
          // have symbolic labels, this is no longer horrible to do...

          text_block->insts.emplace_back(
              inst::SetVec{.vec = v, .idx = idx, .arg = local});
          // And increment.
          text_block->insts.emplace_back(
              inst::Binop{
                  .primop = Primop::INT_PLUS,
                  .out = idx,
                  .arg1 = idx,
                  .arg2 = inc,
                });
          text_block->insts.emplace_back(
              inst::SymbolicJump{
                .lab = join_lab,
              });


          // Now the code for trivial nodes.

          // Awkwardly, the vector size comes from the layout object
          // (the pair of attributes and child vector), not the vector
          // itself.
          trivial_block->insts.emplace_back(
              inst::Unop{
                .primop = Primop::LAYOUT_VEC_SIZE,
                .out = num_subchildren,
                .arg = local,
              });

          // But anyway, we need the vector to subscript from it.
          trivial_block->insts.emplace_back(
              inst::GetLabel{
                .out = child_vec,
                .obj = local,
                .lab = NODE_CHILDREN_LABEL,
              });

          // idx = 0
          trivial_block->insts.emplace_back(
              inst::Load{
                .out = subidx,
                .global = zero,
              });

          std::string while_label = NewSymbol("join_while");
          Block *while_block = &(*blocks)[while_label];

          trivial_block->insts.emplace_back(
              inst::SymbolicJump{
                .lab = while_label,
              });

          // We want while (idx < num_subchildren) but
          // the if jumps when true, so negate:
          // cond = idx >= num_subchildren
          while_block->insts.emplace_back(
              inst::Binop{
                .primop = Primop::INT_GREATEREQ,
                .out = subcond,
                .arg1 = subidx,
                .arg2 = num_subchildren,
              });

          // if !cond goto join_lab
          while_block->insts.emplace_back(
              inst::SymbolicIf{
                  .cond = subcond,
                  .true_lab = join_lab,
              });

          // Read the child.
          while_block->insts.emplace_back(
              inst::GetVec{
                .out = subchild,
                .vec = child_vec,
                .idx = subidx,
              });
          // And write it.
          while_block->insts.emplace_back(
              inst::SetVec{
                .vec = v,
                .idx = idx,
                .arg = subchild,
              });

          // Increment each index.
          while_block->insts.emplace_back(
              inst::Binop{
                  .primop = Primop::INT_PLUS,
                  .out = idx,
                  .arg1 = idx,
                  .arg2 = inc,
                });
          while_block->insts.emplace_back(
              inst::Binop{
                  .primop = Primop::INT_PLUS,
                  .out = subidx,
                  .arg1 = subidx,
                  .arg2 = inc,
                });

          // Now loop, finishing the while block.
          while_block->insts.emplace_back(
              inst::SymbolicJump{.lab = while_label});

          // Now the join block.
          // As usual, there's nothing to do except mark
          // this as the current block.

          current_block = join_block;
          child_idx++;
        }

        // Then, once we've built up the children vector, make the
        // node.

        // The node is a pair.
        std::string r = NewSymbol("node");
        current_block->insts.emplace_back(inst::Alloc{.out = r});
        current_block->insts.emplace_back(inst::SetLabel{
            .obj = r,
            .lab = NODE_ATTRS_LABEL,
            .arg = lattrs,
          });
        current_block->insts.emplace_back(inst::SetLabel{
            .obj = r,
            .lab = NODE_CHILDREN_LABEL,
            .arg = v,
          });
        return r;
      }

      case il::ExpType::RECORD: {
        const std::vector<std::pair<std::string, const il::Exp *>> &fields =
          exp->Record();

        // PERF: Should represent empty record as just 0.

        // Evaluate components.
        std::vector<std::string> locals;
        locals.reserve(fields.size());
        for (const auto &[f, e] : fields) {
          locals.push_back(ConvertExp(G, f, e, blocks, current_block));
        }

        // Now allocate the record.
        std::string r = NewSymbol("rec");
        current_block->insts.emplace_back(inst::Alloc{.out = r});

        // And set fields within it.
        for (int i = 0; i < (int)locals.size(); i++) {
          current_block->insts.emplace_back(inst::SetLabel{
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
          locals.push_back(ConvertExp(G, f, e, blocks, current_block));

          typed_field_names.push_back(ObjFieldName(f, oft));
        }

        // Now allocate the obj.
        std::string r = NewSymbol("obj");
        current_block->insts.emplace_back(inst::Alloc{.out = r});

        // And set fields within it.
        CHECK(typed_field_names.size() == locals.size());
        for (int i = 0; i < (int)locals.size(); i++) {
          current_block->insts.emplace_back(inst::SetLabel{
              .obj = r,
              .lab = typed_field_names[i],
              .arg = locals[i],
            });
        }

        return r;
      }

      case il::ExpType::WITH: {
        const auto &[e, field, oft, rhs] = exp->With();
        const std::string obj =
          ConvertExp(G, "obj", e, blocks, current_block);
        const std::string rlocal =
          ConvertExp(G, field, rhs, blocks, current_block);
        std::string out = NewSymbol("w_" + field);
        current_block->insts.emplace_back(inst::Copy{
            .out = out,
            .obj = obj,
          });
        current_block->insts.emplace_back(inst::SetLabel{
            .obj = out,
            .lab = ObjFieldName(field, oft),
            .arg = rlocal,
          });
        return out;
      }

      case il::ExpType::WITHOUT: {
        const auto &[e, field, oft] = exp->Without();
        const std::string obj = ConvertExp(G, "obj", e, blocks, current_block);
        std::string out = NewSymbol("wo_" + field);
        current_block->insts.emplace_back(inst::Copy{
            .out = out,
            .obj = obj,
          });
        current_block->insts.emplace_back(inst::DeleteLabel{
            .obj = out,
            .lab = ObjFieldName(field, oft),
          });
        return out;
      }

      case il::ExpType::HAS: {
        const auto &[e, field, oft] = exp->Has();
        const std::string obj = ConvertExp(G, "obj", e, blocks, current_block);
        std::string out = NewSymbol(field);
        current_block->insts.emplace_back(inst::HasLabel{
            .out = out,
            .obj = obj,
            .lab = ObjFieldName(field, oft),
          });
        return out;
      }

      case il::ExpType::GET: {
        // If this isn't protected by a HAS, then we might want
        // to insert instructions to test, and fail gracefully?
        // Or do that in the elaboration of a bare "get" construct?
        const auto &[e, field, oft] = exp->Get();
        const std::string obj = ConvertExp(G, "obj", e, blocks, current_block);
        std::string out = NewSymbol(field);
        current_block->insts.emplace_back(inst::GetLabel{
            .out = out,
            .obj = obj,
            .lab = ObjFieldName(field, oft),
          });
        return out;
      }

      case il::ExpType::INT: {
        const auto &bi = exp->Int();
        return AddAndLoadValue("i", Value{.v = Value::t(bi)}, current_block);
      }

      case il::ExpType::BOOL: {
        uint64_t u = exp->Bool() ? 1 : 0;
        return AddAndLoadValue("b", Value{.v = Value::t(u)}, current_block);
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
          return AddAndLoadValue(sym, Value{.v = Value::t(label)},
                                 current_block);
        } else {
          const std::string out = NewSymbol(sym);
          current_block->insts.emplace_back(
              inst::Load{.out = out, .global = label});
          return out;
        }
      }

      case il::ExpType::LAYOUT: {
        // XXX It's unused?
        LOG(FATAL) << "Unimplemented";
        return "ERROR";
      }

      case il::ExpType::LET: {
        const auto &[tyvars_, x, rhs, body] = exp->Let();
        const std::string v = ConvertExp(G, x, rhs, blocks, current_block);
        G = G.Insert(x, v);
        exp = body;
        continue;
      }

      case il::ExpType::UNPACK: {
        // This acts just like Let when we erase types.
        const auto &[alpha_, x, rhs, body] = exp->Unpack();
        const std::string v = ConvertExp(G, x, rhs, blocks, current_block);
        G = G.Insert(x, v);
        exp = body;
        continue;
      }

      case il::ExpType::IF: {
        const auto &[cond, texp, fexp] = exp->If();
        const std::string condv = ConvertExp(G, "cond", cond, blocks,
                                             current_block);

        std::string true_label = NewSymbol("true");
        std::string false_label = NewSymbol("false");
        std::string join_label = NewSymbol("ifjoin");

        CHECK(!blocks->contains(true_label));
        CHECK(!blocks->contains(false_label));
        CHECK(!blocks->contains(join_label));
        Block *true_block = &(*blocks)[true_label];
        Block *false_block = &(*blocks)[false_label];
        Block *join_block = &(*blocks)[join_label];

        // Finish the block with a conditional jump (true) and then a
        // unconditional one. When assembling, the false branch
        // will typically be fused onto this one, removing the
        // unconditional branch.
        current_block->insts.emplace_back(
            inst::SymbolicIf({.cond = condv, .true_lab = true_label}));
        current_block->insts.emplace_back(
            inst::SymbolicJump({.lab = false_label}));

        // Translate each block's expression, and put the result
        // in 'ifres'.
        const std::string res = NewSymbol("ifres");

        const std::string fres = ConvertExp(G, "f", fexp, blocks, false_block);
        false_block->insts.emplace_back(inst::Bind{.out = res, .arg = fres});
        false_block->insts.emplace_back(inst::SymbolicJump{.lab = join_label});

        const std::string tres = ConvertExp(G, "t", texp, blocks, true_block);
        true_block->insts.emplace_back(inst::Bind{.out = res, .arg = tres});
        true_block->insts.emplace_back(inst::SymbolicJump{.lab = join_label});

        // The join block is actually empty, but the caller may
        // add to it, etc.

        current_block = join_block;
        return res;
      }

      case il::ExpType::APP: {
        const auto &[fn, arg] = exp->App();
        const std::string f = ConvertExp(G, "f", fn, blocks, current_block);
        const std::string x = ConvertExp(G, "arg", arg, blocks, current_block);
        const std::string res = NewSymbol("app");
        current_block->insts.emplace_back(
            inst::Call{.out = res, .f = f, .arg = x});
        return res;
      }

      case il::ExpType::PROJECT: {
        const auto &[lab, e] = exp->Project();
        const std::string r = ConvertExp(G, "rec", e, blocks, current_block);
        const std::string res = NewSymbol(lab);
        current_block->insts.emplace_back(
            inst::GetLabel{.out = res, .obj = r, .lab = lab});
        return res;
      }

      case il::ExpType::INJECT: {
        const auto &[lab, t, e] = exp->Inject();
        // Sum is a represented as a map with the label and boxed value.
        const std::string elocal =
          ConvertExp(G, "inj", e, blocks, current_block);
        const std::string rec = NewSymbol(lab);
        current_block->insts.emplace_back(inst::Alloc{.out = rec});
        const std::string lab_value =
          AddAndLoadValue(lab, Value{.v = Value::t(lab)}, current_block);
        current_block->insts.emplace_back(inst::SetLabel({
              .obj = rec, .lab = INJ_LABEL, .arg = lab_value}));
        current_block->insts.emplace_back(inst::SetLabel({
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
          ls.push_back(ConvertExp(G, "p", e, blocks, current_block));
        }

        std::string out = NewSymbol("po");

        // Some primops are translated away natively.
        switch (po) {
        case Primop::STRING_TO_LAYOUT:
          // a layout value is either a map (pair of attributes and children)
          // or a string, so this is transparent. We just return the string.
          CHECK(ls.size() == 1);
          return ls[0];

        case Primop::REF: {
          CHECK(ls.size() == 1);
          current_block->insts.emplace_back(inst::Alloc{.out = out});
          current_block->insts.emplace_back(
              inst::SetLabel{.obj = out, .lab = REF_LABEL, .arg = ls[0]});
          return out;
        }

        case Primop::REF_GET: {
          CHECK(ls.size() == 1);
          current_block->insts.emplace_back(
              inst::GetLabel{.out = out, .obj = ls[0], .lab = REF_LABEL});
          return out;
        }

        case Primop::REF_SET: {
          CHECK(ls.size() == 2);
          current_block->insts.emplace_back(
              inst::SetLabel{.obj = ls[0], .lab = REF_LABEL, .arg = ls[1]});
          // Need to allocate a unit for the return.
          // PERF: Don't allocate empty records; represent it as 0 or
          // something!
          current_block->insts.emplace_back(inst::Alloc{.out = out});
          return out;
        }

        default:
          // Handled as a generic primop, then.
          break;
        }

        switch (num_exp_args) {
        case 1:
          current_block->insts.emplace_back(
              inst::Unop{.primop = po, .out = out, .arg = ls[0]});
          return out;
        case 2:
          current_block->insts.emplace_back(
              inst::Binop{
                .primop = po, .out = out,
                .arg1 = ls[0], .arg2 = ls[1]});
          return out;
        case 3:
          current_block->insts.emplace_back(
              inst::Triop{
                .primop = po, .out = out,
                .arg1 = ls[0], .arg2 = ls[1], .arg3 = ls[2]});
          return out;

        default:
          LOG(FATAL) << "Unimplemented primop arity " << num_exp_args;
        }
        return "ERROR";
      }

      case il::ExpType::FAIL: {
        const auto &[msg, type_] = exp->Fail();
        const std::string v = ConvertExp(G, "msg", msg, blocks, current_block);
        current_block->insts.emplace_back(inst::Fail{.arg = v});
        // Should have a way to indicate that the expression
        // can't return?
        //
        // Something valid, at least...
        return v;
      }

      case il::ExpType::SEQ: {
        const auto &[es, e] = exp->Seq();
        for (const il::Exp *ee : es)
          (void)ConvertExp(G, "ignore", ee, blocks, current_block);
        exp = e;
        continue;
      }

      case il::ExpType::SUMCASE: {
        // Alas, we cannot decompose this one without adding some
        // unsafe or ugly primitives to IL, so we have to implement
        // it here.

        const auto &[obj, arms, def] = exp->SumCase();

        const std::string obj_local =
          ConvertExp(G, "sc_obj", obj, blocks, current_block);

        const std::string res = NewSymbol("sc_res");

        // Read the actual label just once.
        const std::string actual = NewSymbol("lab");
        current_block->insts.emplace_back(inst::GetLabel{
            .out = actual, .obj = obj_local, .lab = INJ_LABEL});
        // We could also read the contents once, but that seems
        // uglier since it would have different types on each branch.
        // We might do a future optimization where nullary constructors
        // don't even store data, for example. We also don't need to
        // load it in the default or (in principle) in arms where
        // it isn't used.


        // Like the IF case, we have a block for each branch (and
        // for the default) as well as the join block.

        std::vector<std::string> case_labels;
        std::vector<Block *> case_blocks;
        for (const auto &[lab, v_, arm_] : arms) {
          std::string case_label = NewSymbol(lab);
          CHECK(!blocks->contains(case_label));
          case_blocks.push_back(&(*blocks)[case_label]);
          case_labels.push_back(std::move(case_label));
        }

        std::string def_label = NewSymbol("case_default");
        Block *def_block = &(*blocks)[def_label];

        std::string join_label = NewSymbol("case_join");
        Block *join_block = &(*blocks)[join_label];

        // Generate the series of tests into the starting block.
        const std::string test = NewSymbol("test");
        for (int i = 0; i < (int)arms.size(); i++) {
          CHECK(i < (int)case_labels.size());
          CHECK(current_block != nullptr);
          const auto &[lab, x, e] = arms[i];
          const std::string lab_value =
            AddAndLoadValue(lab, Value{.v = Value::t(lab)}, current_block);
          // compare labels
          // PERF: We should be representing these as words, not strings
          current_block->insts.emplace_back(inst::Binop{
              .primop = Primop::STRING_EQ,
              .out = test,
              .arg1 = actual,
              .arg2 = lab_value});
          current_block->insts.emplace_back(inst::SymbolicIf{
              .cond = test,
              .true_lab = case_labels[i]});
        }

        // If we got here, all the cases failed, so jump to the default.
        // The default block typically gets fused here.
        current_block->insts.emplace_back(inst::SymbolicJump{
            .lab = def_label});

        // Now process each block.
        // The default:
        const std::string def_local = ConvertExp(G, "sc_def", def,
                                                 blocks, def_block);
        def_block->insts.emplace_back(
            inst::Bind({.out = res, .arg = def_local}));
        def_block->insts.emplace_back(
            inst::SymbolicJump({.lab = join_label}));

        // Code for each arm.
        for (int i = 0; i < (int)arms.size(); i++) {
          const auto &[lab, x, e] = arms[i];

          // Unpack contained value.
          const std::string x_local = NewSymbol(x);
          case_blocks[i]->insts.emplace_back(inst::GetLabel{
              .out = x_local,
              .obj = obj_local,
              .lab = INJ_VALUE,
            });
          // Convert arm's expression, with x bound to the contents.
          const std::string arm_local =
            ConvertExp(G.Insert(x, x_local), lab + "_arm", e,
                       blocks, case_blocks[i]);
          case_blocks[i]->insts.emplace_back(
              inst::Bind({.out = res, .arg = arm_local}));
          // Join.
          case_blocks[i]->insts.emplace_back(
              inst::SymbolicJump{.lab = join_label});
        }

        // Join block starts empty.
        current_block = join_block;
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
        LOG(FATAL) << "Unhandled expression type in ToBytecode::ConvertExp: "
                   << il::ExpTypeString(exp->type);
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

SymbolicProgram ToBytecode::Convert(const il::Program &pgm) {
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
