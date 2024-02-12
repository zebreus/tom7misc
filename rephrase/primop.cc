
#include "primop.h"

#include <tuple>

#include "base/logging.h"

const char *PrimopString(Primop p) {
  switch (p) {
  case Primop::REF: return "REF";
  case Primop::GET: return "GET";
  case Primop::SET: return "SET";
  case Primop::INT_EQ: return "INT_EQ";
  case Primop::INT_NEQ: return "INT_NEQ";
  case Primop::INT_LESS: return "INT_LESS";
  case Primop::INT_LESSEQ: return "INT_LESSEQ";
  case Primop::INT_GREATER: return "INT_GREATER";
  case Primop::INT_GREATEREQ: return "INT_GREATEREQ";
  case Primop::INT_TIMES: return "INT_TIMES";
  case Primop::INT_PLUS: return "INT_PLUS";
  case Primop::INT_MINUS: return "INT_MINUS";
  case Primop::INT_DIV: return "INT_DIV";
  case Primop::INT_MOD: return "INT_MOD";
  case Primop::INT_NEG: return "INT_NEG";
  case Primop::STRING_EQ: return "STRING_EQ";
  case Primop::INT_DIV_TO_FLOAT: return "INT_DIV_TO_FLOAT";
  case Primop::FLOAT_TIMES: return "FLOAT_TIMES";
  case Primop::FLOAT_PLUS: return "FLOAT_PLUS";
  case Primop::FLOAT_MINUS: return "FLOAT_MINUS";
  case Primop::FLOAT_DIV: return "FLOAT_DIV";
  default: return "?? UNKNOWN PRIMOP ??";
  }
}

std::tuple<int, int> PrimopArity(Primop po) {
  switch(po) {
  case Primop::REF: return std::make_tuple(1, 1);
  case Primop::GET: return std::make_tuple(1, 1);
  case Primop::SET: return std::make_tuple(1, 2);
  case Primop::INT_EQ: return std::make_tuple(0, 2);
  case Primop::INT_NEQ: return std::make_tuple(0, 2);
  case Primop::INT_LESS: return std::make_tuple(0, 2);
  case Primop::INT_LESSEQ: return std::make_tuple(0, 2);
  case Primop::INT_GREATER: return std::make_tuple(0, 2);
  case Primop::INT_GREATEREQ: return std::make_tuple(0, 2);
  case Primop::INT_TIMES: return std::make_tuple(0, 2);
  case Primop::INT_PLUS: return std::make_tuple(0, 2);
  case Primop::INT_MINUS: return std::make_tuple(0, 2);
  case Primop::INT_DIV: return std::make_tuple(0, 2);
  case Primop::INT_MOD: return std::make_tuple(0, 2);
  case Primop::INT_NEG: return std::make_tuple(0, 1);
  case Primop::STRING_EQ: return std::make_tuple(0, 2);
  case Primop::INT_DIV_TO_FLOAT: return std::make_tuple(0, 2);
  case Primop::FLOAT_TIMES: return std::make_tuple(0, 2);
  case Primop::FLOAT_PLUS: return std::make_tuple(0, 2);
  case Primop::FLOAT_MINUS: return std::make_tuple(0, 2);
  case Primop::FLOAT_DIV: return std::make_tuple(0, 2);
  default:
    LOG(FATAL) << "Unknown primop: " << PrimopString(po);
    return std::make_tuple(0, 0);
  }
};

bool IsPrimopTotal(Primop p) {
  switch (p) {
  case Primop::REF: return false;
  case Primop::GET: return false;
  case Primop::SET: return false;
  // Since we use BigInt, integer arithmetic cannot overflow.
  case Primop::INT_EQ: return true;
  case Primop::INT_NEQ: return true;
  case Primop::INT_LESS: return true;
  case Primop::INT_LESSEQ: return true;
  case Primop::INT_GREATER: return true;
  case Primop::INT_GREATEREQ: return true;
  case Primop::INT_TIMES: return true;
  case Primop::INT_PLUS: return true;
  case Primop::INT_MINUS: return true;
  case Primop::INT_DIV:
    // Because of divide by zero.
    return false;
  case Primop::INT_MOD:
    // Because of divide by zero.
    return false;
  case Primop::INT_NEG:
    return true;
  case Primop::STRING_EQ:
    return true;
  case Primop::INT_DIV_TO_FLOAT:
    // Here, division by zero produces nan or +/- inf.
    return true;
  case Primop::FLOAT_TIMES: return true;
  case Primop::FLOAT_PLUS: return true;
  case Primop::FLOAT_MINUS: return true;
  case Primop::FLOAT_DIV: return true;
  default:
    printf("Uknown primop in IsPrimopTotal");
    return false;
  }
}

bool IsPrimopDiscardable(Primop p) {
  switch (p) {
  case Primop::REF:
    // Creating a reference is not itself observable.
    return true;
  case Primop::GET: return true;
  case Primop::SET: return false;

  default:
    return IsPrimopTotal(p);
  }
}

