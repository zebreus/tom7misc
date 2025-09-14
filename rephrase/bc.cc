
#include "bc.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "bignum/big.h"
#include "primop.h"

namespace bc {

#define AGLOBAL_LAB(s) AFGCOLOR(200, 160, 40, s)
#define AMAP_LAB(s) AFGCOLOR(160, 200, 40, s)
#define AOUT(s) AFGCOLOR(160, 160, 220, s)
#define AARG(s) AFGCOLOR(200, 120, 220, s)
#define AOP(s) AWHITE(s)
#define AIDX(s) AFGCOLOR(200, 220, 220, s)
#define ALINE(s) AGREY(s)
#define ALINE_USED(s) AFGCOLOR(120, 120, 140, s)
#define ABLOCK_LABEL(s) AYELLOW(s)

#define ASTRLIT(s) AFGCOLOR(153, 187, 119, s)

std::string ColorValueString(const Value &value) {
  if (const BigInt *bi = std::get_if<BigInt>(&value.v)) {
    return bi->ToString();
  } else if (const std::string *s = std::get_if<std::string>(&value.v)) {
    return std::format(ASTRLIT("\"{}\""), *s);
  } else if (const uint64_t *u = std::get_if<uint64_t>(&value.v)) {
    return std::format("{}LLU", *u);
  } else if (const double *d = std::get_if<double>(&value.v)) {
    return std::format("{:.17g}", *d);
  } else if (const std::unordered_map<std::string, Value *> *m =
             std::get_if<std::unordered_map<std::string, Value *>>(&value.v)) {
    std::string ret = AWHITE("{");
    std::vector<std::string> labs;
    for (const auto &[lab, v_] : *m) {
      labs.push_back(lab);
    }
    std::sort(labs.begin(), labs.end());
    for (int i = 0; i < (int)labs.size(); i++) {
      if (i != 0) ret.append(", ");
      AppendFormat(&ret, AMAP_LAB("{}"), labs[i]);
    }
    return ret + AWHITE("}");
  } else if (const std::vector<Value *> *v =
             std::get_if<std::vector<Value *>>(&value.v)) {
    return std::format("vector, size {}", v->size());
  } else {
    return "!!invalid!!";
  }
}


std::string ColorInstString(const Inst &inst) {
  if (const inst::Triop *triop = std::get_if<inst::Triop>(&inst)) {
    return std::format("BINOP " AOUT("{}") " <- " AOP("{}") "("
                       AARG("{}") ", " AARG("{}") ", " AARG("{}") ")",
                       triop->out,
                       PrimopString(triop->primop),
                       triop->arg1,
                       triop->arg2,
                       triop->arg3);

  } else if (const inst::Binop *binop = std::get_if<inst::Binop>(&inst)) {
    return std::format("BINOP " AOUT("{}") " <- "
                       AARG("{}") " " AOP("{}") " " AARG("{}"),
                       binop->out,
                       binop->arg1,
                       PrimopString(binop->primop),
                       binop->arg2);

  } else if (const inst::Unop *unop = std::get_if<inst::Unop>(&inst)) {
    return std::format("UNOP " AOUT("{}") " <- " AOP("{}") " " AARG("{}"),
                       unop->out,
                       PrimopString(unop->primop),
                       unop->arg);

  } else if (const inst::Call *call = std::get_if<inst::Call>(&inst)) {
    return std::format("CALL " AOUT("{}") " <- " AOP("{}") "(" AOP("{}") ")",
                        call->out, call->f, call->arg);

  } else if (const inst::TailCall *tail_call =
             std::get_if<inst::TailCall>(&inst)) {
    return std::format("TAIL_CALL " AOP("{}") "(" AOP("{}") ")",
                       tail_call->f, tail_call->arg);

  } else if (const inst::Ret *ret = std::get_if<inst::Ret>(&inst)) {
    return std::format("RET " AARG("{}"), ret->arg);

  } else if (const inst::If *iff = std::get_if<inst::If>(&inst)) {
    // Look at all the "if"s in that line ^_^
    return std::format("IF " AARG("{}") " " ALINE_USED("{:05d}"),
                        iff->cond, iff->true_idx);

  } else if (const inst::AllocVec *allocvec =
             std::get_if<inst::AllocVec>(&inst)) {
    return std::format("ALLOCVEC " AOUT("{}"),
                       allocvec->out);

  } else if (const inst::SetVec *setvec = std::get_if<inst::SetVec>(&inst)) {
    return std::format("SET " AARG("{}") "[" AIDX("{}") "] <- " AARG("{}"),
                       setvec->vec,
                       setvec->idx,
                       setvec->arg);

  } else if (const inst::GetVec *getvec = std::get_if<inst::GetVec>(&inst)) {
    return std::format("GET " AOUT("{}") " <- " AARG("{}") "[" AIDX("{}") "]",
                       getvec->out,
                       getvec->vec,
                       getvec->idx);

  } else if (const inst::Alloc *alloc = std::get_if<inst::Alloc>(&inst)) {
    return std::format("ALLOC " AOUT("{}"),
                       alloc->out);

  } else if (const inst::Copy *copy = std::get_if<inst::Copy>(&inst)) {
    return std::format("COPY " AOUT("{}") " <- " AARG("{}"),
                       copy->out, copy->obj);

  } else if (const inst::SetLabel *setlabel =
             std::get_if<inst::SetLabel>(&inst)) {
    return std::format("SET " AARG("{}") "." AMAP_LAB("{}") " <- " AARG("{}"),
                        setlabel->obj, setlabel->lab,
                        setlabel->arg);

  } else if (const inst::GetLabel *getlabel =
             std::get_if<inst::GetLabel>(&inst)) {
    return std::format("GET " AOUT("{}") " <- " AARG("{}") "." AMAP_LAB("{}"),
                        getlabel->out,
                        getlabel->obj,
                        getlabel->lab);

  } else if (const inst::DeleteLabel *deletelabel =
             std::get_if<inst::DeleteLabel>(&inst)) {
    return std::format("DELETE " AARG("{}") "." AMAP_LAB("{}"),
                       deletelabel->obj,
                       deletelabel->lab);

  } else if (const inst::HasLabel *haslabel =
             std::get_if<inst::HasLabel>(&inst)) {
    return std::format("HAS " AOUT("{}") " <- " AARG("{}") "." AMAP_LAB("{}"),
                       haslabel->out,
                       haslabel->obj,
                       haslabel->lab);

  } else if (const inst::Bind *bind = std::get_if<inst::Bind>(&inst)) {
    return std::format("BIND " AOUT("{}") " <- " AARG("{}"),
                       bind->out, bind->arg);

  } else if (const inst::Load *load = std::get_if<inst::Load>(&inst)) {
    return std::format("LOAD " AOUT("{}") " <- " AGLOBAL_LAB("{}"),
                       load->out, load->global);

  } else if (const inst::Save *save = std::get_if<inst::Save>(&inst)) {
    return std::format("SAVE " AGLOBAL_LAB("{}") " <- " AARG("{}"),
                       save->global, save->arg);

  } else if (const inst::Jump *jump = std::get_if<inst::Jump>(&inst)) {
    return std::format("JUMP " ALINE_USED("{:05d}"),
                       jump->idx);

  } else if (const inst::Fail *fail = std::get_if<inst::Fail>(&inst)) {
    return std::format("FAIL " AARG("{}"), fail->arg);

  } else if (const inst::Note *note = std::get_if<inst::Note>(&inst)) {
    return std::format("NOTE " AGREY("{}"), note->msg);

  } else if (const inst::SymbolicIf *iff =
             std::get_if<inst::SymbolicIf>(&inst)) {
    return std::format("IF " AARG("{}") " " ABLOCK_LABEL("{}"),
                       iff->cond, iff->true_lab);

  } else if (const inst::SymbolicJump *jmp =
             std::get_if<inst::SymbolicJump>(&inst)) {
    return std::format("JUMP " ABLOCK_LABEL("{}"),
                       jmp->lab);

  } else {
    return ARED("!!INVALID!!");
  }
}

void PrintProgram(const Program &pgm) {
  std::map<std::string, Value> data(pgm.data.begin(), pgm.data.end());
  std::map<std::string, std::pair<std::string, std::vector<Inst>>>
    code(pgm.code.begin(), pgm.code.end());

  Print(ABGCOLOR(255, 255, 255,
                 AFGCOLOR(0, 0, 0,
                          " == DATA == ")) "\n");
  for (const auto &[lab, value] : data) {
    Print(" " AGLOBAL_LAB("{}") ": {}\n",
          lab, ColorValueString(value));
  }

  Print(ABGCOLOR(255, 255, 255,
                 AFGCOLOR(0, 0, 0,
                          " == CODE == ")) "\n");
  for (const auto &[lab, cc] : code) {
    const auto &[arg, insts] = cc;

    // Could give each of these a different color?
    std::unordered_set<int> jump_targets;
    for (const Inst &inst : insts) {
      if (const inst::Jump *jump = std::get_if<inst::Jump>(&inst)) {
        jump_targets.insert(jump->idx);
      } else if (const inst::If *iff = std::get_if<inst::If>(&inst)) {
        jump_targets.insert(iff->true_idx);
      }
    }

    Print(" " AYELLOW("{}") "(" ABLUE("{}") "):\n",
          lab, arg);
    for (int i = 0; i < (int)insts.size(); i++) {
      // XXX compute how many digits are needed
      if (jump_targets.contains(i)) {
        Print("  " ALINE_USED("{:05d}") " ", i);
      } else {
        Print("  " ALINE("{:05d}") " ", i);
      }
      Print("{}\n", ColorInstString(insts[i]));
    }
    Print("\n");
  }
}

void PrintBlock(const Block &block) {
  for (const auto &inst : block.insts) {
    Print("  {}\n", ColorInstString(inst));
  }
}

void PrintSymbolicProgram(const SymbolicProgram &pgm) {
  std::map<std::string, Value> data(pgm.data.begin(), pgm.data.end());
  std::map<std::string, SymbolicFn> code(pgm.code.begin(), pgm.code.end());

  Print(ABGCOLOR(255, 255, 255,
                 AFGCOLOR(0, 0, 0,
                          " == DATA == ")) "\n");
  for (const auto &[lab, value] : data) {
    Print(" " AGLOBAL_LAB("{}") ": {}\n",
          lab, ColorValueString(value));
  }

  Print(ABGCOLOR(255, 255, 255,
                 AFGCOLOR(0, 0, 0,
                          " == CODE == ")) "\n");
  for (const auto &[name, fn] : code) {
    Print(AWHITE("function") " " APURPLE("{}")
          "(" ABLUE("{}") ") ==> " AYELLOW("{}") "\n",
          name, fn.arg,
          fn.initial);

    for (const auto &[lab, block] : fn.blocks) {
      Print(" " AYELLOW("{}") ":\n", lab);

      PrintBlock(block);
    }
  }
  Print("\n");
}


std::pair<int64_t, int64_t> ProgramSize(const Program &pgm) {
  static constexpr int64_t DATA_NAME_SIZE = sizeof (std::string);
  int64_t total_insts = 0;
  int64_t data_bytes = 0;
  for (const auto &[k, val] : pgm.data) {
    data_bytes += DATA_NAME_SIZE;
    if (const auto *x = std::get_if<BigInt>(&val.v)) {
      // XXX measure actual size of bigint
      (void)x;
      data_bytes += 8;
    } else if (const auto *x = std::get_if<std::string>(&val.v)) {
      // XXX and overhead?
      data_bytes += x->size();
    } else if (const auto *x = std::get_if<uint64_t>(&val.v)) {
      data_bytes += sizeof (*x);
    } else if (const auto *x = std::get_if<double>(&val.v)) {
      data_bytes += sizeof (*x);
    } else {
      LOG(FATAL) << "Unhandled or illegal data in ProgramSize";
    }
  }

  for (const auto &[f, fn] : pgm.code) {
    const auto &[arg, insts] = fn;
    total_insts += insts.size();
  }

  return {data_bytes, total_insts};
}

char ObjectFieldTypeTag(ObjectFieldType oft) {
  switch (oft) {
  case ObjectFieldType::STRING: return '\"';
  case ObjectFieldType::FLOAT: return '.';
  case ObjectFieldType::INT: return '0';
  case ObjectFieldType::BOOL: return '?';
  case ObjectFieldType::U64: return '6';
  case ObjectFieldType::OBJ: return '=';
  case ObjectFieldType::LAYOUT: return '[';
  }

  LOG(FATAL) << "Bad bc::ObjectFieldType?";
  return 0;
};


}  // namespace bc
