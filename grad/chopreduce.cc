
#include <optional>
#include <array>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "expression.h"
#include "half.h"
#include "hashing.h"

#include "choppy.h"
#include "grad-util.h"
#include "color-util.h"
#include "arcfour.h"

using DB = Choppy::DB;
using Allocator = Exp::Allocator;
using Table = Exp::Table;


// We probably want to do some Gauss-Jordan elimination kinda
// thing, but even easier, we could see if we already have
// solutions for any columns.
static void Reduce(DB *db) {
  std::array<std::optional<DB::key_type>, Choppy::GRID> solved;

  for (const auto &[k, v] : db->fns) {
    int nonzero = -1;
    for (int i = 0; i < Choppy::GRID; i++) {
      if (k[i] != 0) {
        if (nonzero == -1) {
          nonzero = i;
        } else {
          // This means more than one column was nonzero.
          goto next;
        }
      }
    }

    if (nonzero != -1) {
      // or take the smallest expression, or prefer norm
      solved[nonzero].emplace(k);
    }

  next:;
  }

  DB basis;
  for (int i = 0; i < Choppy::GRID; i++) {
    if (solved[i].has_value()) {
      const DB::key_type &k = solved[i].value();
      printf("%d solved: %s\n", i, DB::KeyString(k).c_str());

      const Exp *v = db->fns[k];
      CHECK(v != nullptr);

      // Note that these were allocated inside the other database.
      // Probably we should be copying them.
      const int mag = k[i];
      CHECK(mag != 0);
      if (mag == 1) {
        CHECK(basis.Add(v) == DB::AddResult::SUCCESS_NEW);
      } else {
        // Multiply by the inverse to normalize.
        // This might not work for non-powers of two...
        half inv = (half)(1.0 / mag);
        const Exp *e = db->alloc.TimesC(v, Exp::GetU16(inv));
        CHECK(basis.Add(e) == DB::AddResult::SUCCESS_NEW);
      }
    }
  }

  Util::WriteFile("basis.tmp", basis.Dump());
}

int main(int argc, char **argv) {
  DB db;
  db.LoadFile("chopdb.txt");

  Reduce(&db);

  printf("OK\n");
  return 0;
}
