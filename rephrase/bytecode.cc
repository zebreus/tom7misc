
#include "bytecode.h"

#include <unordered_set>
#include <map>
#include <string>
#include <variant>

#include "bignum/big.h"
#include "ansi.h"
#include "base/stringprintf.h"
#include "base/logging.h"

namespace bc {

#define AGLOBAL_LAB(s) AFGCOLOR(200, 160, 40, s)
#define AMAP_LAB(s) AFGCOLOR(160, 200, 40, s)
#define AOUT(s) AFGCOLOR(160, 160, 220, s)
#define AARG(s) AFGCOLOR(200, 120, 220, s)
#define AOP(s) AWHITE(s)
#define AINDEX(s) AGREY(s)
#define AINDEX_USED(s) AFGCOLOR(120, 120, 140, s)

#define ASTRLIT(s) AFGCOLOR(153, 187, 119, s)

std::string ColorValueString(const Value &value) {
  if (const BigInt *bi = std::get_if<BigInt>(&value.v)) {
    return bi->ToString();
  } else if (const std::string *s = std::get_if<std::string>(&value.v)) {
    return StringPrintf(ASTRLIT("\"%s\""), s->c_str());
  } else if (const uint64_t *u = std::get_if<uint64_t>(&value.v)) {
    return StringPrintf("%lluLLU", *u);
  } else if (const double *d = std::get_if<double>(&value.v)) {
    return StringPrintf("%.17g", d);
  } else if (const std::unordered_map<std::string, Value *> *m =
             std::get_if<std::unordered_map<std::string, Value *>>(&value.v)) {
    std::string ret = AWHITE("{");
    std::vector<std::string> labs;
    for (const auto &[lab, v_] : *m) {
      labs.push_back(lab);
    }
    std::sort(labs.begin(), labs.end());
    for (int i = 0; i < (int)labs.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      StringAppendF(&ret, AMAP_LAB("%s"), labs[i].c_str());
    }
    return ret + AWHITE("}");
  } else {
    return "!!invalid!!";
  }
}


std::string ColorInstString(const Inst &inst) {
  if (const inst::Binop *binop = std::get_if<inst::Binop>(&inst)) {
    return StringPrintf("BINOP " AOUT("%s") " <- "
                        AARG("%s") " " AOP("%s") " " AARG("%s"),
                        binop->out.c_str(),
                        binop->arg1.c_str(),
                        PrimopString(binop->primop),
                        binop->arg2.c_str());
  } else if (const inst::Unop *unop = std::get_if<inst::Unop>(&inst)) {
    return StringPrintf("UNOP " AOUT("%s") " <- " AOP("%s") " " AARG("%s"),
                        unop->out.c_str(),
                        PrimopString(unop->primop),
                        unop->arg.c_str());
  } else if (const inst::Call *call = std::get_if<inst::Call>(&inst)) {
    return StringPrintf("CALL " AOUT("%s") " <- " AOP("%s") "(" AOP("%s") ")",
                        call->out.c_str(), call->f.c_str(), call->arg.c_str());
  } else if (const inst::Ret *ret = std::get_if<inst::Ret>(&inst)) {
    return StringPrintf("RET " AARG("%s"), ret->arg.c_str());
  } else if (const inst::If *iff = std::get_if<inst::If>(&inst)) {
    // Look at all the "if"s in that line ^_^
    return StringPrintf("IF " AARG("%s") " " AINDEX_USED("%05d"),
                        iff->cond.c_str(), iff->true_idx);
  } else if (const inst::Alloc *alloc = std::get_if<inst::Alloc>(&inst)) {
    return StringPrintf("ALLOC " AOUT("%s"),
                        alloc->out.c_str());
  } else if (const inst::SetLabel *setlabel =
             std::get_if<inst::SetLabel>(&inst)) {
    return StringPrintf("SET " AARG("%s") "." AMAP_LAB("%s") " <- " AARG("%s"),
                        setlabel->obj.c_str(), setlabel->lab.c_str(),
                        setlabel->arg.c_str());
  } else if (const inst::GetLabel *getlabel =
             std::get_if<inst::GetLabel>(&inst)) {
    return StringPrintf("GET " AOUT("%s") " <- " AARG("%s") "." AMAP_LAB("%s"),
                        getlabel->out.c_str(),
                        getlabel->obj.c_str(),
                        getlabel->lab.c_str());
  } else if (const inst::Bind *bind = std::get_if<inst::Bind>(&inst)) {
    return StringPrintf("BIND " AOUT("%s") " <- " AARG("%s"),
                        bind->out.c_str(), bind->arg.c_str());
  } else if (const inst::Load *load = std::get_if<inst::Load>(&inst)) {
    return StringPrintf("LOAD " AOUT("%s") " <- " AGLOBAL_LAB("%s"),
                        load->out.c_str(), load->global.c_str());
  } else if (const inst::Save *save = std::get_if<inst::Save>(&inst)) {
    return StringPrintf("SAVE " AGLOBAL_LAB("%s") " <- " AARG("%s"),
                        save->global.c_str(), save->arg.c_str());
  } else if (const inst::Jump *jump = std::get_if<inst::Jump>(&inst)) {
    return StringPrintf("JUMP " AINDEX_USED("%05d"),
                        jump->idx);
  } else if (const inst::Fail *fail = std::get_if<inst::Fail>(&inst)) {
    return StringPrintf("FAIL " AARG("%s"), fail->arg.c_str());
  } else if (const inst::Note *note = std::get_if<inst::Note>(&inst)) {
    return StringPrintf("NOTE " AGREY("%s"), note->msg.c_str());
  } else {
    return ARED("!!INVALID!!");
  }
}

void PrintProgram(const Program &pgm) {
  std::map<std::string, Value> data(pgm.data.begin(), pgm.data.end());
  std::map<std::string, std::pair<std::string, std::vector<Inst>>>
    code(pgm.code.begin(), pgm.code.end());

  printf(ABGCOLOR(255, 255, 255,
                  AFGCOLOR(0, 0, 0,
                           " == DATA == ")) "\n");
  for (const auto &[lab, value] : data) {
    printf(" " AGLOBAL_LAB("%s") ": %s\n",
           lab.c_str(), ColorValueString(value).c_str());
  }

  printf(ABGCOLOR(255, 255, 255,
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

    printf(" " AYELLOW("%s") "(" ABLUE("%s") "):\n",
           lab.c_str(), arg.c_str());
    for (int i = 0; i < (int)insts.size(); i++) {
      // XXX compute how many digits are needed
      if (jump_targets.contains(i)) {
        printf("  " AINDEX_USED("%05d") " ", i);
      } else {
        printf("  " AINDEX("%05d") " ", i);
      }
      printf("%s\n", ColorInstString(insts[i]).c_str());
    }
    printf("\n");
  }
}

}  // namespace bc
