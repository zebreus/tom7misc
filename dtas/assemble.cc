
#include "assemble.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "formula.h"
#include "parsing.h"
#include "randutil.h"
#include "sourcemap.h"
#include "util.h"
#include "zoning.h"

[[maybe_unused]]
static constexpr int VERBOSE = 1;

void Assembly::Bank::Write(uint16_t machine_addr, uint8_t v) {
  int offset = (int)machine_addr - origin;
  CHECK(offset >= 0 && offset < bytes.size()) <<
    StringPrintf("Bug: Write outside of bank bounds. Addr: %04x",
                 machine_addr);
  bytes[offset] = v;
}

// Evaluate to a number if possible. Note the number may be out of
// range for the target type.
static std::optional<int64_t> Evaluate(
    Assembly *assembly, const Exp *exp,
    const std::function<std::string()> &Error) {
  CHECK(exp != nullptr);
  switch (exp->type) {
  case ExpType::LABEL: {
    auto it = assembly->symbols.find(exp->label);
    if (it == assembly->symbols.end()) return std::nullopt;
    return {it->second};
  }

  case ExpType::NUMBER:
    return {exp->number};

  case ExpType::HIGH_BYTE: {
    if (auto io = Evaluate(assembly, exp->a.get(), Error)) {
      int64_t value = io.value();
      CHECK(value >= 0 && value <= 0xFFFF) << "In a > expression, the "
        "computed value is out of range: " << value << Error();
      return 0xFF & (value >> 8);
    } else {
      return std::nullopt;
    }
  }
  case ExpType::LOW_BYTE: {
    if (auto io = Evaluate(assembly, exp->a.get(), Error)) {
      int64_t value = io.value();
      CHECK(value >= 0 && value <= 0xFFFF) << "In a < expression, the "
        "computed value is out of range: " << value << Error();
      return 0xFF & value;
    } else {
      return std::nullopt;
    }
  }

  case ExpType::PLUS: {
    if (auto ao = Evaluate(assembly, exp->a.get(), Error)) {
      if (auto bo = Evaluate(assembly, exp->b.get(), Error)) {
        int64_t a = ao.value();
        int64_t b = bo.value();
        return {a + b};
      }
    }
    return std::nullopt;
  }
  case ExpType::MINUS: {
    if (auto ao = Evaluate(assembly, exp->a.get(), Error)) {
      if (auto bo = Evaluate(assembly, exp->b.get(), Error)) {
        int64_t a = ao.value();
        int64_t b = bo.value();
        return {a - b};
      }
    }
    return std::nullopt;
  }

  default:
    LOG(FATAL) << "Unknown/invalid expression in Evaluate.\n";
    return std::nullopt;
  }
}

static std::optional<uint16_t> Evaluate16(
    Assembly *assembly, const Exp *exp,
    const std::function<std::string()> &Error) {
  if (auto io = Evaluate(assembly, exp, Error)) {
    int64_t value = io.value();
    CHECK(value >= 0 && value <= 0xFFFF) << "Expected a 16-bit value "
      "but the expression's value was out of range: " << value << Error();
    return {(uint16_t)value};
  } else {
    return std::nullopt;
  }
}

static std::optional<uint8_t> Evaluate8(
    Assembly *assembly, const Exp *exp,
    const std::function<std::string()> &Error) {
  if (auto io = Evaluate(assembly, exp, Error)) {
    int64_t value = io.value();
    CHECK(value >= 0 && value <= 0xFF) << "Expected an 8-bit value "
      "but the expression's value was out of range: " << value << Error();
    return {(uint16_t)value};
  } else {
    return std::nullopt;
  }
}

static std::optional<int8_t> EvaluateSigned8(
    Assembly *assembly, const Exp *exp,
    const std::function<std::string()> &Error) {
  if (auto io = Evaluate(assembly, exp, Error)) {
    int64_t value = io.value();
    CHECK(value >= -128 && value <= 127) << "Expected a signed 8-bit value "
      "but the expression's value was out of range. This likely means that "
      "a relative jump was too far." << value << Error();
    return {(int8_t)value};
  } else {
    return std::nullopt;
  }
}

static std::shared_ptr<Exp> DisplacementExp(
    uint16_t base_address,
    std::shared_ptr<Exp> target_address) {
  return std::make_shared<Exp>(Exp{
      .type = ExpType::MINUS,
      .a = std::move(target_address),
      .b = std::make_shared<Exp>(Exp{
          .type = ExpType::NUMBER,
          .number = base_address,
        })
    });
}

Assembly Assembly::Assemble(const std::string &asm_file) {
  const std::string asm_content = Util::ReadFile(asm_file);
  Assembly assembly;

  // Single-byte opcodes which have implied addressing.
  const std::unordered_map<std::string, uint8_t> mode_implied = {
    {"brk", 0x00},
    {"clc", 0x18},
    {"cld", 0xD8},
    {"clv", 0xB8},
    {"cli", 0x58},
    {"dex", 0xCA},
    {"dey", 0x88},
    {"inx", 0xE8},
    {"iny", 0xC8},
    {"nop", 0xEA},
    {"pha", 0x48},
    {"php", 0x08},
    {"pla", 0x68},
    {"plp", 0x28},
    {"rti", 0x40},
    {"rts", 0x60},
    {"sec", 0x38},
    {"sed", 0xF8},
    {"sei", 0x78},
    {"tax", 0xAA},
    {"tay", 0xA8},
    {"tsx", 0xBA},
    {"txa", 0x8A},
    {"txs", 0x9A},
    {"tya", 0x98},
  };

  // A typical instruction has the form
  // aaabbbcc, where aaa and cc determine the mnemonic.

  // Group 1: c=01
  // The map's values are aaa00001.
  const std::unordered_map<std::string, uint8_t> group1 = {
    {"ora", 0b00000001},
    {"and", 0b00100001},
    {"eor", 0b01000001},
    {"adc", 0b01100001},
    {"sta", 0b10000001},
    {"lda", 0b10100001},
    {"cmp", 0b11000001},
    {"sbc", 0b11100001},
  };

  // Group 2: c=10
  // The map's values are aaa00010.
  const std::unordered_map<std::string, uint8_t> group2 = {
    {"asl", 0b00000010},
    {"rol", 0b00100010},
    {"lsr", 0b01000010},
    {"ror", 0b01100010},
    {"stx", 0b10000010},
    {"ldx", 0b10100010},
    {"dec", 0b11000010},
    {"inc", 0b11100010},
  };

  // Group 3: c=00
  // JMP is not included here since it only has two addressing
  // modes, one of which is a special case.
  // The map's values are aaa00000.
  const std::unordered_map<std::string, uint8_t> group3 = {
    {"bit", 0b00100000},
    {"sty", 0b10000000},
    {"ldy", 0b10100000},
    {"cpy", 0b11000000},
    {"cpx", 0b11100000},
  };

  // Branches are all of the form xxy10000. xx describes a flag
  // and 1 is the comparison value. They take a signed displacement,
  // but the value in the map is the full opcode.
  const std::unordered_map<std::string, uint8_t> branches = {
    {"bpl", 0x10},
    {"bmi", 0x30},
    {"bvc", 0x50},
    {"bvs", 0x70},
    {"bcc", 0x90},
    {"bcs", 0xB0},
    {"bne", 0xD0},
    {"beq", 0xF0},
  };

  const std::vector<std::string> lines = Util::SplitToLines(asm_content);

  for (int line_num = 0; line_num < (int)lines.size(); line_num++) {
    const std::string &line = lines[line_num];
    std::vector<Token> tokens_orig = Tokenize(line_num, line);

    auto Error = [line_num, &line, &tokens_orig]() {
        std::string toks;
        for (int t = 0; t < (int)tokens_orig.size(); t++) {
          if (t > 0) toks.push_back(' ');
          StringAppendF(&toks, "%s",
                        TokenTypeString(tokens_orig[t].type).c_str());
        }
        return StringPrintf("\nLine %d:\n%s\n%s", line_num,
                            line.c_str(), toks.c_str());
      };

    auto CurrentBank = [&assembly, &Error]() -> Bank & {
        CHECK(!assembly.banks.empty()) << "There are no banks yet! "
          "Use .org to start one." << Error();
        return assembly.banks.back();
      };

    auto EmitByte = [&CurrentBank](uint8_t v) {
        CurrentBank().bytes.push_back(v);
      };

    // Write the value of the expression as a little-endian 16-bit
    // word, either now or later.
    auto WriteExp16 =
      [&assembly, &EmitByte, &CurrentBank, &Error, line_num](
          std::shared_ptr<Exp> exp) {
          if (auto bo = Evaluate16(&assembly, exp.get(), Error)) {
            uint16_t v = bo.value();
            // printf(ACYAN("%04x") "\n", v);
            // Write in little-endian order.
            EmitByte(v & 0xFF);
            v >>= 8;
            EmitByte(v & 0xFF);

          } else {
            // printf(APURPLE("delayed") "\n");
            CurrentBank().delayed_16.push_back(Delayed{
                .line_num = line_num,
                .exp = std::move(exp),
                .dest_addr = CurrentBank().NextAddress(),
              });

            EmitByte(0x00);
            EmitByte(0x00);
          }
        };

    // Same, for an 8-bit expression.
    auto WriteExp8 =
      [&assembly, &EmitByte, &CurrentBank, &Error, line_num](
          std::shared_ptr<Exp> exp) {
          if (auto bo = Evaluate8(&assembly, exp.get(), Error)) {
            uint8_t v = bo.value();
            EmitByte(v);
          } else {
            CurrentBank().delayed_8.push_back(Delayed{
                .line_num = line_num,
                .exp = std::move(exp),
                .dest_addr = CurrentBank().NextAddress(),
              });

            EmitByte(0x00);
          }
        };

    auto WriteExpSigned8 =
      [&assembly, &EmitByte, &CurrentBank, &Error, line_num](
          std::shared_ptr<Exp> exp) {
          if (auto bo = EvaluateSigned8(&assembly, exp.get(), Error)) {
            int8_t v = bo.value();
            EmitByte(v);
          } else {
            CurrentBank().delayed_s8.push_back(Delayed{
                .line_num = line_num,
                .exp = std::move(exp),
                .dest_addr = CurrentBank().NextAddress(),
              });

            EmitByte(0x77);
          }
        };

    std::vector<Token> tokens = tokens_orig;

    StripComments(&tokens, Error);

    if (std::optional<std::string> label = ConsumeLabel(&tokens, Error)) {
      const std::string &symbol = label.value();
      CHECK(!assembly.HasSymbol(symbol)) << "Duplicate label: "
                                         << symbol << Error();
      uint16_t addr = CurrentBank().NextAddress();
      assembly.symbols[symbol] = addr;
      CurrentBank().debug_labels[addr] = symbol;
    }

    Line parsed_line = ParseLine(tokens, Error);

    switch (parsed_line.type) {
    case Line::Type::NOTHING:
      // Nothing to do on empty lines (or lines with just a comment or label).
      break;

    case Line::Type::DIRECTIVE_INDEX:
      CHECK(parsed_line.num == 8) << "Only .index 8 is supported, and "
          "I also don't know what this means." << Error();
      break;

    case Line::Type::DIRECTIVE_MEM:
      CHECK(parsed_line.num == 8) << "Only .mem 8 is supported, and "
          "I also don't know what this means." << Error();
      break;

    case Line::Type::DIRECTIVE_ORG:
      CHECK(parsed_line.num >= 0 &&
            parsed_line.num < 0x10000) << "Illegal .org directive."
                                       << Error();
      assembly.banks.emplace_back(
          parsed_line.num,
          SourceMap(asm_file, asm_content));
      break;

    case Line::Type::DIRECTIVE_DB:
      // A series of expressions denoting bytes.
      for (const auto &e : parsed_line.exps) {
        WriteExp8(e);
      }
      break;

    case Line::Type::DIRECTIVE_DW:
      // A series of expressions denoting 16-bit words.

      for (const auto &e : parsed_line.exps) {
        WriteExp16(e);
      }
      break;

    case Line::Type::DIRECTIVE_ALWAYS:

      printf("Ignoring formula: %s\n",
             ColorForm(parsed_line.formula).c_str());
      break;

    case Line::Type::DIRECTIVE_HERE: {
      const uint16_t addr = CurrentBank().NextAddress();
      printf("Ignoring formula (at %04x): %s\n",
             addr,
             ColorForm(parsed_line.formula).c_str());
      break;
    }

    case Line::Type::CONSTANT_DECL: {
      // Symbolic constant, like
      // PPU_CTRL_REG1         = $2000

      const std::string &sym = parsed_line.symbol;
      CHECK(!assembly.HasSymbol(sym)) <<
        "Duplicate symbol " << sym << Error();

      CHECK(parsed_line.exps.size() == 1) << "Bug";

      auto io = Evaluate16(&assembly, parsed_line.exps[0].get(), Error);
      if (io.has_value()) {
        assembly.symbols[sym] = io.value();
      } else {
        assembly.delayed_symbols[sym] = Delayed{
          .line_num = line_num,
          .exp = std::move(parsed_line.exps[0]),
          .dest_addr = 0x0000,
        };
      }
      break;
    }

    case Line::Type::INSTRUCTION: {
      {
        Bank &bank = CurrentBank();
        // Mark that we assembled an actual instruction here (just
        // the first byte) in the zoning file.
        bank.zoning.addr[bank.NextAddress()] |= Zoning::X;
        // Also mark where this instruction is in the source file.
        bank.source_map.code[bank.NextAddress()] = line_num;
      }

      std::string mnemonic = Util::lcase(parsed_line.symbol);
      const Addressing &mode = parsed_line.addressing;

      if (auto it = mode_implied.find(mnemonic); it != mode_implied.end()) {
        EmitByte(it->second);

      } else if (auto it = group1.find(mnemonic); it != group1.end()) {
        uint8_t opcode = it->second;

        // There's only one special case here (for STA, #), which doesn't
        // exist (and would not make sense).
        [&]{
          CHECK(mode.type != Addressing::ACCUMULATOR) <<
            "Illegal addressing mode." << Error();
          if (mode.type == Addressing::IMMEDIATE) {
            CHECK(mnemonic != "sta") << "Illegal addressing mode." << Error();
            EmitByte(opcode | 0b000'010'00);
            WriteExp8(mode.exp);
            return;
          }

          if (mode.type == Addressing::ADDR ||
              mode.type == Addressing::ADDR_X) {
            // For these two addressing modes, we could be using the zero page
            // version or the full 16-bit version. We don't know which one we
            // have without looking at the argument expression.
            //
            // Note this does not include ADDR_Y. There is no "zero page, Y"
            // mode in this group.
            if (auto bo = Evaluate16(&assembly, mode.exp.get(), Error)) {
              uint16_t v = bo.value();

              if (v < 0x100) {
                // Zero page version.
                uint8_t bbb =
                  mode.type == Addressing::ADDR ? 0b000'001'00 : 0b000'101'00;
                EmitByte(opcode | bbb);
                // And the byte.
                EmitByte(v);
                return;
              }
            }
          }

          switch (mode.type) {
          case Addressing::ACCUMULATOR:
          case Addressing::INDIRECT:
            LOG(FATAL) << "Illegal addressing mode."; break;
          case Addressing::INDIRECT_X:
            EmitByte(opcode | 0b000'000'00);
            WriteExp8(mode.exp);
            return;
          case Addressing::INDIRECT_Y:
            EmitByte(opcode | 0b000'100'00);
            WriteExp8(mode.exp);
            return;
          default:
            break;
          }

          // Otherwise, we have a 16 bit version, either because we didn't
          // find an 8-bit value above or the expression's value is not
          // known yet.
          uint8_t bbb = 0;
          switch (mode.type) {
          case Addressing::ADDR: bbb = 0b000'011'00; break;
          case Addressing::ADDR_X: bbb = 0b000'111'00; break;
          case Addressing::ADDR_Y: bbb = 0b000'110'00; break;
          default:
            LOG(FATAL) << "Bug: All cases should be covered by now." << Error();
          }

          EmitByte(opcode | bbb);
          WriteExp16(mode.exp);
        }();

      } else if (auto it = group2.find(mnemonic); it != group2.end()) {
        uint8_t opcode = it->second;

        [&]() {
          if (mode.type == Addressing::IMMEDIATE) {
            CHECK(mnemonic == "ldx") << "Illegal addressing mode." << Error();
            EmitByte(opcode | 0b000'000'00);
            WriteExp8(mode.exp);
            return;
          }

          if (mode.type == Addressing::ACCUMULATOR) {
            CHECK(mnemonic == "asl" ||
                  mnemonic == "rol" ||
                  mnemonic == "lsr" ||
                  mnemonic == "ror") << "Illegal addressing mode." << Error();
            EmitByte(opcode | 0b000'010'00);
            return;
          }

          // Whether ,X or ,Y versions are allowed depends on the
          // mnemonic. Check this for errors.
          if (mode.type == Addressing::ADDR_Y) {
            CHECK(mnemonic == "stx" || mnemonic == "ldx") << Error();
          }

          if (mnemonic == "stx" || mnemonic == "ldx") {
            CHECK(mode.type != Addressing::ADDR_X) << Error();
          }

          if (mode.type == Addressing::ADDR ||
              mode.type == Addressing::ADDR_X ||
              mode.type == Addressing::ADDR_Y) {
            // See if we are using the zero page version, as above.

            if (auto bo = Evaluate16(&assembly, mode.exp.get(), Error)) {
              uint16_t v = bo.value();

              if (v < 0x100) {
                // Zero page version.
                uint8_t bbb =
                  mode.type == Addressing::ADDR ? 0b000'001'00 :
                  // ADDR_X and ADDR_Y are encoded the same.
                  0b000'101'00;
                EmitByte(opcode | bbb);
                // And the byte.
                EmitByte(v);
                return;
              }
            }
          }

          if (mnemonic == "stx") {
            CHECK(mode.type != Addressing::ADDR_X) << "For mysterious "
              "reasons, 6502 does not support STX abs,x." << Error();
          }

          uint8_t bbb = 0;
          switch (mode.type) {
          case Addressing::ADDR: bbb = 0b000'011'00; break;
          // Again, ADDR_X and ADDR_Y are encoded the same.
          case Addressing::ADDR_X: bbb = 0b000'111'00; break;
          case Addressing::ADDR_Y: bbb = 0b000'111'00; break;
          default:
            LOG(FATAL) << "Bug: Should have been handled by now." << Error();
          }

          EmitByte(opcode | bbb);
          WriteExp16(mode.exp);

        }();


      } else if (auto it = group3.find(mnemonic); it != group3.end()) {
        uint8_t opcode = it->second;

        [&]() {
          if (mode.type == Addressing::IMMEDIATE) {
            CHECK(mnemonic == "ldy" ||
                  mnemonic == "cpy" ||
                  mnemonic == "cpx") << "Illegal addressing mode." << Error();
            EmitByte(opcode | 0b000'000'00);
            WriteExp8(mode.exp);
            return;
          }

          if (mode.type == Addressing::ADDR ||
              mode.type == Addressing::ADDR_X) {

            if (auto bo = Evaluate16(&assembly, mode.exp.get(), Error)) {
              uint16_t v = bo.value();

              if (v < 0x100) {
                // Zero page version.
                uint8_t bbb =
                  mode.type == Addressing::ADDR ? 0b000'001'00 : 0b000'101'00;

                if (mode.type == Addressing::ADDR_X) {
                  CHECK(mnemonic == "ldy" || mnemonic == "sty")
                    << "Illegal addressing mode." << Error();
                }

                EmitByte(opcode | bbb);
                // And the byte.
                EmitByte(v);
                return;
              }
            }
          }

          // We know this is the abs,x mode now (not zp,x), and LDY is
          // the only group 3 instruction that can use abs,x.
          if (mode.type == Addressing::ADDR_X) {
            CHECK(mnemonic == "ldy") << "Illegal addressing mode." << Error();
          }

          uint8_t bbb = 0;
          switch (mode.type) {
          case Addressing::ADDR: bbb = 0b000'011'00; break;
          case Addressing::ADDR_X: bbb = 0b000'111'00; break;
          default:
            LOG(FATAL) << "Bug: Should have been handled by now." << Error();
          }

          EmitByte(opcode | bbb);
          WriteExp16(mode.exp);

        }();

      } else if (auto it = branches.find(mnemonic); it != branches.end()) {

        uint8_t opcode = it->second;

        CHECK(mode.type == Addressing::ADDR) << "Branches are only allowed "
          "to absolute addresses." << Error();

        // The displacement is relative to the instruction that follows
        // this one (because the PC is incremented regardless). This is
        // two bytes after the current address.
        const uint16_t base_addr = CurrentBank().NextAddress() + 2;
        EmitByte(opcode);
        WriteExpSigned8(DisplacementExp(base_addr, mode.exp));

      } else if (mnemonic == "jmp") {

        // This is sort of a group 3 instruction, but handled separately.
        switch (mode.type) {
        case Addressing::ADDR:
          EmitByte(0x4C);
          WriteExp16(mode.exp);
          break;
        case Addressing::INDIRECT:
          EmitByte(0x6C);
          WriteExp16(mode.exp);
          break;
        default:
          LOG(FATAL) << "Invalid addressing mode for JMP." << Error();
          break;
        }

      } else if (mnemonic == "jsr") {
        EmitByte(0x20);

        CHECK(mode.type == Addressing::ADDR) << "Expected an expression "
          "denoting a 16-bit address (tyically the label of the subroutine) "
          "in JSR.";

        WriteExp16(mode.exp);

      } else {
        LOG(FATAL) << "Unimplemented instruction: " << tokens[0].str;
      }
    }
      break;

    default:
      LOG(FATAL) << "Unimplemented/invalid line type?";
    }

  }

  printf(AYELLOW("assembly") "\n");
  printf("  %d symbols\n", (int)assembly.symbols.size());
  printf("  %d delayed symbols\n", (int)assembly.delayed_symbols.size());

  printf("  There are %d banks.\n", (int)assembly.banks.size());
  for (const Bank &bank : assembly.banks) {
    printf("  " AWHITE("bank") "\n");
    printf("    %d delayed8\n", (int)bank.delayed_8.size());
    printf("    %d delayed16\n", (int)bank.delayed_16.size());
    printf("    %d bytes\n", (int)bank.bytes.size());
  }

  auto ErrorAt = [&lines](int line_num) {
      return std::function<std::string()>(
          [&lines, line_num]() {
            CHECK(line_num >= 0 && line_num < lines.size());
            return StringPrintf("\n On line %d:\n"
                                "%s\n",
                                line_num, lines[line_num].c_str());
          });
    };

  // Second pass: Resolve symbolic constants.
  // The best way to do this would be to generate a topological ordering
  // and detect any cycles. But if there exists such a thing, then
  // iteratively resolving them will terminate. In practice the number
  // of required passes is very small.
  {
    ArcFour rc("second pass");
    std::vector<std::pair<std::string, Delayed>> todo;
    todo.reserve(assembly.delayed_symbols.size());
    for (auto &[sym, delayed] : assembly.delayed_symbols) {
      todo.emplace_back(sym, std::move(delayed));
    }
    assembly.delayed_symbols.clear();

    while (!todo.empty()) {
      const size_t start_size = todo.size();

      std::vector<std::pair<std::string, Delayed>> remaining;

      // ... do a pass ...
      for (const auto &[sym, delayed] : todo) {
        auto Error = ErrorAt(delayed.line_num);
        auto io = Evaluate(&assembly, delayed.exp.get(), Error);
        if (io.has_value()) {
          printf("  " AGREY("%s") " => " ABLUE("%lld") "\n",
                 sym.c_str(), io.value());
          assembly.symbols[sym] = io.value();
        } else {
          remaining.emplace_back(sym, delayed);
        }
      }

      const size_t end_size = remaining.size();
      if (start_size == end_size) {
        fprintf(stderr,
                "In the second pass: Could not resolve symbolic "
                "constants. There is probably an undefined symbol "
                "or a cycle. These are the remaining symbols:\n");
        for (const auto &[sym, delayed] : todo) {
          fprintf(stderr, "  %s = %s\n",
                  sym.c_str(), ExpString(delayed.exp.get()).c_str());
        }
        LOG(FATAL) << "Failed due to unresolved symbols.";
      }

      todo = std::move(remaining);

      // To prevent pathological behavior (quadratic) when the
      // symbols are just in a reversed dependency order, attend
      // to them in a random order.
      Shuffle(&rc, &todo);
    }
  }

  // Third pass: Resolve delayed writes.
  for (Bank &bank : assembly.banks) {
    for (const Delayed &delayed : bank.delayed_16) {
      auto Error = ErrorAt(delayed.line_num);
      auto vo = Evaluate16(&assembly, delayed.exp.get(), Error);
      CHECK(vo.has_value()) << "Delayed 16-bit expression was "
        "not evaluatable in the second pass. It's probably "
        "using a label that was never defined: " <<
        ExpString(delayed.exp.get()) << Error();

      uint16_t v = vo.value();
      bank.Write(delayed.dest_addr, v & 0xFF);
      v >>= 8;
      bank.Write(delayed.dest_addr + 1, v & 0xFF);
    }

    for (const Delayed &delayed : bank.delayed_8) {
      auto Error = ErrorAt(delayed.line_num);
      auto vo = Evaluate8(&assembly, delayed.exp.get(), Error);
      CHECK(vo.has_value()) << "Delayed 8-bit expression was "
        "not evaluatable in the second pass. It's probably "
        "using a label that was never defined: " <<
        ExpString(delayed.exp.get()) << Error();

      uint8_t v = vo.value();
      bank.Write(delayed.dest_addr, v);
    }

    for (const Delayed &delayed : bank.delayed_s8) {
      auto Error = ErrorAt(delayed.line_num);
      auto vo = EvaluateSigned8(&assembly, delayed.exp.get(), Error);
      CHECK(vo.has_value()) << "Delayed signed 8-bit expression was "
        "not evaluatable in the second pass. It's probably "
        "using a label that was never defined: " <<
        ExpString(delayed.exp.get()) << Error();

      const int8_t v = vo.value();
      bank.Write(delayed.dest_addr, (uint8_t)v);
    }
  }

  return assembly;
}

void Assembly::WriteToDisk(const std::string &rom_file) {
  // Now write the ROM, debugging symbols, and so on.
  CHECK(banks.size() == 1) << "Actually I only know how to "
    "write one bank right now, but this would be easily rectified.";

  // The ROM.
  Util::WriteFileBytes(rom_file, banks[0].bytes);
  printf("Wrote " AGREEN("%s") "\n", rom_file.c_str());


  // Debugging info.
  std::string fbase = (std::string)Util::FileBaseOf(rom_file);

  // nl file
  {
    std::string contents;
    for (const auto &[addr, name] : banks[0].debug_labels) {
      StringAppendF(&contents, "$%04x#%s#\n", addr, name.c_str());
    }
    std::string nlfile = StringPrintf("%s.nes.0.nl", fbase.c_str());
    Util::WriteFile(nlfile, contents);
    printf("Wrote " AGREEN("%s") "\n", nlfile.c_str());
  }

  {
    std::string zonefile = StringPrintf("%s.zoning", fbase.c_str());
    banks[0].zoning.Save(zonefile);
    printf("Wrote " AGREEN("%s") "\n", zonefile.c_str());
  }

  {
    std::string smfile = StringPrintf("%s.sourcemap", fbase.c_str());
    banks[0].source_map.Save(smfile);
    printf("Wrote " AGREEN("%s") "\n", smfile.c_str());
  }
}


