
#include "parsing.h"

#include <cstdio>
#include <format>
#include <variant>
#include <vector>

#include "ansi.h"
#include "formula.h"

static constexpr bool VERBOSE = false;

#define PARSE(str) []() {                                     \
    if (VERBOSE)                                              \
      fprintf(stderr, "PARSE: [%s]\n", "" str "");            \
    std::vector<Token> tokens = Tokenize(0, "" str "");       \
    return ParseLine(tokens, []() {                           \
        return std::format("Test at {}:{}\nInput line: {}\n", \
                           __FILE__, __LINE__, "" str "");    \
      });                                                     \
  }();

static void TestParseLine() {
  {
    Line line = PARSE(".org 100");
    CHECK(line.type == Line::Type::DIRECTIVE_ORG);
    CHECK(line.num == 100);
  }

  {
    Line line = PARSE(".mem %10101010");
    CHECK(line.type == Line::Type::DIRECTIVE_MEM);
    CHECK(line.num == 0xAA);
  }

  {
    Line line = PARSE(".index $2a");
    CHECK(line.type == Line::Type::DIRECTIVE_INDEX);
    CHECK(line.num == 0x2A);
  }

  {
    Line line = PARSE(".db 1, 2, 3+4, >label, <label");
    CHECK(line.type == Line::Type::DIRECTIVE_DB);
    CHECK(line.exps.size() == 5);

    const auto &exp3 = line.exps[2];
    CHECK(exp3->type == ExpType::PLUS);
    CHECK(exp3->a->type == ExpType::NUMBER && exp3->a->number == 3);
    CHECK(exp3->b->type == ExpType::NUMBER && exp3->b->number == 4);

    const auto &exp4 = line.exps[3];
    CHECK(exp4->type == ExpType::HIGH_BYTE);
    CHECK(exp4->a->type == ExpType::LABEL && exp4->a->label == "label");
  }

  {
    Line line = PARSE("abc = xyz - 1");
    CHECK(line.type == Line::Type::CONSTANT_DECL);
    CHECK(line.symbol == "abc");
    CHECK(line.exps.size() == 1);
    CHECK(line.exps[0]->type == ExpType::MINUS);
    CHECK(line.exps[0]->a->type == ExpType::LABEL &&
          line.exps[0]->a->label == "xyz");
    CHECK(line.exps[0]->b->type == ExpType::NUMBER &&
          line.exps[0]->b->number == 1);
  }

  {
    Line line = PARSE("lda (ptr,x)");
    CHECK(line.type == Line::Type::INSTRUCTION);
    CHECK(line.symbol == "lda");
    CHECK(line.addressing.type == Addressing::INDIRECT_X);
    CHECK(line.addressing.exp->type == ExpType::LABEL);
    CHECK(line.addressing.exp->label == "ptr");
  }

  {
    Line line = PARSE("lda (ptr),y");
    CHECK(line.type == Line::Type::INSTRUCTION);
    CHECK(line.symbol == "lda");
    CHECK(line.addressing.type == Addressing::INDIRECT_Y);
    CHECK(line.addressing.exp->type == ExpType::LABEL);
    CHECK(line.addressing.exp->label == "ptr");
  }
}

static void TestParseFormulas() {
  // .always with a simple equality test.
  {
    Line line = PARSE(".always P = 10");
    CHECK(line.type == Line::Type::DIRECTIVE_ALWAYS);
    CHECK(line.formula != nullptr);
    const BinForm *bf = std::get_if<BinForm>(line.formula.get());
    CHECK(bf != nullptr);
    CHECK(bf->op == Binop::EQ);
    const VarForm *vf = std::get_if<VarForm>(bf->lhs.get());
    CHECK(vf != nullptr && vf->name == "P");
    const IntForm *itf = std::get_if<IntForm>(bf->rhs.get());
    CHECK(itf != nullptr && itf->value == 10);
  }

  {
    Line line = PARSE(".here ram[X] in {1, 2, 99}");
    CHECK(line.type == Line::Type::DIRECTIVE_HERE);
    CHECK(line.formula != nullptr);
    // top level is 'in'
    const BinForm *in_bf = std::get_if<BinForm>(line.formula.get());
    CHECK(in_bf != nullptr && in_bf->op == Binop::IN);

    // lhs is ram[X]
    const UnForm *ram_uf = std::get_if<UnForm>(in_bf->lhs.get());
    CHECK(ram_uf != nullptr && ram_uf->op == Unop::RAM);
    const VarForm *vf = std::get_if<VarForm>(ram_uf->arg.get());
    CHECK(vf != nullptr && vf->name == "X");

    // rhs is {1, 2, 99}
    const NaryForm *set_nf = std::get_if<NaryForm>(in_bf->rhs.get());
    CHECK(set_nf != nullptr && set_nf->op == Naryop::SET);
    CHECK(set_nf->v.size() == 3);
    const IntForm *i0 = std::get_if<IntForm>(set_nf->v[0].get());
    const IntForm *i1 = std::get_if<IntForm>(set_nf->v[1].get());
    const IntForm *i2 = std::get_if<IntForm>(set_nf->v[2].get());
    CHECK(i0 && i0->value == 1);
    CHECK(i1 && i1->value == 2);
    CHECK(i2 && i2->value == 99);
  }

  {
    Line line = PARSE(".always (u8)ram[X] <= 200");
    CHECK(line.type == Line::Type::DIRECTIVE_ALWAYS);
    CHECK(line.formula != nullptr);
    // top is <=
    const BinForm *bf = std::get_if<BinForm>(line.formula.get());
    CHECK(bf != nullptr && bf->op == Binop::LESSEQ);

    // lhs is (u8)ram[X]
    const UnForm *cast_uf = std::get_if<UnForm>(bf->lhs.get());
    CHECK(cast_uf != nullptr && cast_uf->op == Unop::AS_WORD8);
    const UnForm *ram_uf = std::get_if<UnForm>(cast_uf->arg.get());
    CHECK(ram_uf != nullptr && ram_uf->op == Unop::RAM);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestParseLine();
  TestParseFormulas();

  printf("OK\n");
  return 0;
}
