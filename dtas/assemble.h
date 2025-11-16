// Simple 6502 assembler, with extensions for debugging output and
// modeling assertions.
//
// This reproduces the mario PRG byte-for-byte from mario.asm,
// following what x816 would do (that assembler seems to be so old
// that it needs DOSBox or similar to run!).
//
// It might have bugs outside of the instructions and techniques that
// mario.asm uses; no guarantees!

#ifndef _DTAS_ASSEMBLE_H
#define _DTAS_ASSEMBLE_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "formula.h"
#include "parsing.h"
#include "sourcemap.h"
#include "zoning.h"

// An assembled program.
struct Assembly {
  // Use this to create an Assembly.
  static Assembly Assemble(const std::string &asm_file);
  // Write the rom file and debugging data. Note that the
  // constraints are not saved; for these, assemble in process.
  void WriteToDisk(const std::string &rom_file);

  // A delayed expression. These are written after we've finished
  // the first pass and know the value of every label. We compute the
  // value and write it to the 16-bit dest_addr. Whether we write an
  // 8-bit value or a 16-bit one needs to be determined by the context.
  //
  // For clarity: The address here is a machine address, not an offset
  // into assembled bytes (but the offset can be computed using the
  // origin).
  struct Delayed {
    int line_num = 0;
    std::shared_ptr<Exp> exp;
    uint16_t dest_addr = 0;
  };

  struct Bank {
    // Creates an empty bank. The origin is required.
    Bank(int origin, SourceMap sm) : origin(origin), source_map(std::move(sm)) {
      // The zoning we generate is constructive; we only
      // mark the addresses where we actually generated
      // an instruction (with an instruction mnemonic) as
      // executable. So begin with nothing set.
      zoning.Clear();
    }

    // The assembled data, which is expected to be loaded at the origin
    // address. The next instruction assembled goes at the end.
    std::vector<uint8_t> bytes;
    // The origin where the bytes will be loaded in memory, e.g. 0x8000.
    // This just affects what absolute value a label has.
    int origin = 0;

    std::map<uint16_t, std::string> debug_labels;
    // Get the label and address from debug_labels, or (0, "zero") if
    // the address is before any label.
    std::pair<uint16_t, std::string> GetMostRecentLabel(uint16_t addr) const;
    // TODO: Comments too?

    // Debugging information about what addresses represent
    // instructions (that we assembled).
    Zoning zoning;

    // Debugging information about where in the source code
    // the code addresses (in this bank) came from.
    SourceMap source_map;

    // Constraints for modeling. For those that have addresses,
    // the addresses (may) refer to the current bank.
    std::vector<Constraint> constraints;

    // Implementation details while assembling.
   private:
    friend struct Assembly;
    uint16_t NextAddress() const {
      return origin + (int)bytes.size();
    }

    void Write(uint16_t machine_addr, uint8_t v);

    std::vector<Delayed> delayed_16;
    std::vector<Delayed> delayed_8;
    std::vector<Delayed> delayed_s8;
  };

  // Symbolic constants include "Constant = $Value" and "Label:".
  // These are global to the assembly (and must be globally unique),
  // but an address might belong to a specific bank. The programmer
  // just has to manage this.

  // Symbols that we know the values of already.
  // The syntax does not distinguish between a declaration like
  //   BuzzyBeetle = $02
  // and
  //   BowserOrigXPos = $0366
  // even though the former is used as a constant byte, and the
  // second as a memory address. Additionally, a label
  //   WarpZoneWelcome:
  // is a machine address (not byte offset) that could be used
  // as a constant, or jumped to, etc. Basically, every symbol has
  // a value, but it is the use that tells us what it means (and
  // what the appropriate range of values is).
  //
  // While assembling, we also have delayed_symbols.
  std::unordered_map<std::string, int64_t> symbols;

  // True if we already saw this symbol (even if we don't know its
  // value yet).
  bool HasSymbol(const std::string &sym) const {
    return symbols.contains(sym) || delayed_symbols.contains(sym);
  }

  // The ROM banks. Each one is a contiguous series of bytes that
  // will live at a given memory location (its origin). Metadata
  // like zoning and constraints are in the banks, since they often
  // are referring to machine addresses (which are ambiguous if
  // not in the context of a specific choice of bank).
  std::vector<Bank> banks;

  Assembly(const Assembly &other) = default;
  Assembly &operator =(const Assembly &other) = default;
  Assembly(Assembly &&other) = default;
  Assembly &operator =(Assembly &&other) = default;
  ~Assembly() = default;

 private:
  Assembly() {}

  // Symbolic "constants" with delayed expressions. Note they may have
  // forward references within this set. (There could even be cycles,
  // which should be rejected later.) Note that the destination address
  // here is unused; the value is never written anywhere. It just gets
  // moved to the constants table.
  std::unordered_map<std::string, Delayed> delayed_symbols;
};

#endif
