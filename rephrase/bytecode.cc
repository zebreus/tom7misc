
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
#define AIDX(s) AFGCOLOR(200, 220, 220, s)
#define ALINE(s) AGREY(s)
#define ALINE_USED(s) AFGCOLOR(120, 120, 140, s)

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
  } else if (const std::vector<Value *> *v =
             std::get_if<std::vector<Value *>>(&value.v)) {
    return StringPrintf("vector, size %d", (int)v->size());
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
    return StringPrintf("IF " AARG("%s") " " ALINE_USED("%05d"),
                        iff->cond.c_str(), iff->true_idx);

  } else if (const inst::AllocVec *allocvec = std::get_if<inst::AllocVec>(&inst)) {
    return StringPrintf("ALLOCVEC " AOUT("%s"),
                        allocvec->out.c_str());

  } else if (const inst::SetVec *setvec = std::get_if<inst::SetVec>(&inst)) {
    return StringPrintf("SET " AARG("%s") "[" AIDX("%s") "] <- " AARG("%s"),
                        setvec->vec.c_str(),
                        setvec->idx.c_str(),
                        setvec->arg.c_str());

  } else if (const inst::GetVec *getvec = std::get_if<inst::GetVec>(&inst)) {
    return StringPrintf("GET " AOUT("%s") " <- " AARG("%s") "[" AIDX("%s") "]",
                        getvec->out.c_str(),
                        getvec->vec.c_str(),
                        getvec->idx.c_str());

  } else if (const inst::Alloc *alloc = std::get_if<inst::Alloc>(&inst)) {
    return StringPrintf("ALLOC " AOUT("%s"),
                        alloc->out.c_str());

  } else if (const inst::Copy *copy = std::get_if<inst::Copy>(&inst)) {
    return StringPrintf("COPY " AOUT("%s") " <- " AARG("%s"),
                        copy->out.c_str(), copy->obj.c_str());

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

  } else if (const inst::DeleteLabel *deletelabel =
             std::get_if<inst::DeleteLabel>(&inst)) {
    return StringPrintf("DELETE " AARG("%s") "." AMAP_LAB("%s"),
                        deletelabel->obj.c_str(),
                        deletelabel->lab.c_str());

  } else if (const inst::HasLabel *haslabel =
             std::get_if<inst::HasLabel>(&inst)) {
    return StringPrintf("HAS " AOUT("%s") " <- " AARG("%s") "." AMAP_LAB("%s"),
                        haslabel->out.c_str(),
                        haslabel->obj.c_str(),
                        haslabel->lab.c_str());

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
    return StringPrintf("JUMP " ALINE_USED("%05d"),
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
        printf("  " ALINE_USED("%05d") " ", i);
      } else {
        printf("  " ALINE("%05d") " ", i);
      }
      printf("%s\n", ColorInstString(insts[i]).c_str());
    }
    printf("\n");
  }
}

std::pair<int64_t, int64_t> ProgramSize(const Program &pgm) {
  static constexpr int64_t DATA_NAME_SIZE = sizeof (std::string);
  int64_t total_insts = 0;
  int64_t data_bytes = 0;
  for (const auto &[k, val] : pgm.data) {
    data_bytes += DATA_NAME_SIZE;
    if (const auto *x = std::get_if<BigInt>(&val.v)) {
      // XXX measure actual size of bigint
      data_bytes += 8;
    } else if (const auto *x = std::get_if<std::string>(&val.v)) {
      // XXX and overhead?
      data_bytes += x->size();
    } else if (const auto *x = std::get_if<uint64_t>(&val.v)) {
      data_bytes += 8;
    } else if (const auto *x = std::get_if<double>(&val.v)) {
      data_bytes += 8;
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
