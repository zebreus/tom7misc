
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

using Choppy = ChoppyGrid<16>;
using DB = Choppy::DB;
using Allocator = Exp::Allocator;
using Table = Exp::Table;

// We probably want to do some Gauss-Jordan elimination kinda
// thing, but even easier, we could see if we already have
// solutions for any columns.
static void Reduce(DB *db) {


  DB basis;
  // Add a vector [0, 0, 0, ..., X, ..., 0] with X != 0, if it
  // can be normalized.
  //
  // Note that these were allocated inside the other database.
  // Probably we should be copying them.
  auto TryAddBasis = [db, &basis](int col, const DB::key_type &k,
                                  const Exp *exp) ->
    DB::AddResult {
      CHECK(exp != nullptr);

      for (int i = 0; i < Choppy::GRID; i++) {
        if (i != col) {
          CHECK(k[i] == 0) << Choppy::DB::KeyString(k);
        }
      }

      const int mag = k[col];
      CHECK(mag != 0);
      if (mag == 1) {
        return basis.Add(exp);
      } else {
        // Multiply by the inverse to normalize.
        // This might not work for non-powers of two...
        half inv = (half)(1.0 / mag);
        const Exp *e = db->alloc.TimesC(exp, Exp::GetU16(inv));
        return basis.Add(e);
      }
    };

  printf("Find easy basis vectors:\n");
  // Start with the easy case: (not necessarily normalized) basis
  // vectors that are already in the database.
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
      TryAddBasis(nonzero, k, v);
    }

  next:;
  }

  printf("Easy pass solves %d.\n", (int)basis.fns.size());

  // G-J Elimination would work here, but it likely will build
  // up very large expressions. Since our existing vectors are
  // pretty dense already, we try to find small solutions to
  // missing bases.
  //
  // For each unsolved column, rekey the database ignoring
  // that column.
  for (int col = 0; col < Choppy::GRID; col++) {
    DB::key_type target_key;
    for (int i = 0; i < Choppy::GRID; i++)
      target_key[i] = (i == col) ? 1 : 0;

    if (!basis.fns.contains(target_key)) {
      printf("Reducing column %d:\n", col);

      // We just blank out the position in the key. Now multiple
      // expressions can be mapped to the same key (and this is what
      // we're hoping for).
      std::unordered_map<DB::key_type,
                         // with the original value from that column
                         std::vector<std::pair<int, const Exp *>>,
                         Hashing<DB::key_type>> rekeyed;
      for (const auto &[k, v] : db->fns) {
        DB::key_type kk = k;
        // Ignore this position.
        kk[col] = 0;
        rekeyed[kk].emplace_back(k[col], v);
      }
      printf("There are %d rows now\n", (int)rekeyed.size());

      // Now look for cases where we have more than one expression.
      for (const auto &[k, vv] : rekeyed) {
        if (vv.size() > 1) {
          // now we have [a, b, c, X, e] = e1
          //             [a, b, c, Y, e] = e2
          // (or more).
          // we should be able to subtract these to get
          //             [0, 0, 0, X - Y, 0] = e1 - e2
          //

          // PERF take the smallest two, or two that differ
          // by exactly one, etc.
          for (int i = 0; i < vv.size(); i++) {
            const auto &[c1, v1] = vv[i];
            const Exp *v1neg = db->alloc.Neg(v1);
            for (int j = i + 1; j < vv.size(); j++) {
              const auto &[c2, v2] = vv[j];

              DB::key_type new_key;
              for (int k = 0; k < Choppy::GRID; k++) {
                new_key[k] = (k == col) ? c2 - c1 : 0;
              }

              auto ar =
                TryAddBasis(col, new_key, db->alloc.PlusE(v1neg, v2));
              if (ar == DB::AddResult::SUCCESS_NEW ||
                  ar == DB::AddResult::SUCCESS_SMALLER) {
                // XXX could continue to maybe find a
                // smaller solution
                goto next_column;
              }
            }
          }
        }
      }
    next_column:;
    }
  }

  printf("Single diff pass: %d now solved\n",
         (int)basis.fns.size());

  // Finally, for each remaining row, shift it so that it has 1
  // in the unsolved column. Then eliminate any nonzero columns
  // using existing basis vectors.
  std::array<const Exp *, Choppy::GRID> solved;
  for (auto &e : solved) e = nullptr;
  for (int col = 0; col < Choppy::GRID; col++) {
    DB::key_type target_key;
    for (int i = 0; i < Choppy::GRID; i++)
      target_key[i] = (i == col) ? 1 : 0;

    if (basis.fns.contains(target_key)) {
      const Exp *fn = basis.fns[target_key];
      CHECK(fn != nullptr);
      solved[col] = fn;
    }
  }

  for (int col = 0; col < Choppy::GRID; col++) {
    if (solved[col] == nullptr) {
      printf("Trying column-wise for col %d:\n", col);

      for (const auto &[k, v] : db->fns) {
        // e.g. if the column currently has 0, we want to add 1.
        const int dy = 1 - k[col];
        DB::key_type shift_key = k;
        for (int &c : shift_key) c += dy;
        CHECK(shift_key[col] == 1);

        // Do we have solutions for the necessary positions?
        for (int i = 0; i < Choppy::GRID; i++)
          if (i != col && shift_key[i] != 0)
            if (solved[i] == nullptr)
              goto next_columnwise_row;

        {
          // Then create it.
          const Exp *shift =
            db->alloc.PlusC(v, Exp::GetU16((half)(1.0 / Choppy::GRID)));

          for (int i = 0; i < Choppy::GRID; i++) {
            if (i != col && shift_key[i] != 0) {
              // multiply it to the same magnitude
              int mult = shift_key[i];
              const Exp *b = solved[i];
              CHECK(b != nullptr) << "above";
              shift = db->alloc.PlusE(
                  shift,
                  db->alloc.TimesC(b,
                                   // but we're actually subtracting it
                                   Exp::GetU16((half)-mult)));
            }
          }

          DB::key_type new_key = DB::BasisKey(col);

          auto ar =
            TryAddBasis(col, new_key, shift);

          if (ar == DB::AddResult::SUCCESS_NEW ||
              ar == DB::AddResult::SUCCESS_SMALLER) {
            printf("Solved col %d from %s\n", DB::KeyString(k).c_str());
            // XXX could continue to maybe find a
            // smaller solution
            goto next_columnwise_column;
          }
        }

      next_columnwise_row:;
      }

    next_columnwise_column:;
    }
  }

  printf("Column-wise pass: %d now solved\n",
         (int)basis.fns.size());

  Util::WriteFile("basis.txt", basis.Dump());
}

int main(int argc, char **argv) {
  DB db;
  printf("Load database:\n");
  db.LoadFile("chopdb.txt");
  db.Image().ScaleBy(10).Save("database.png");

  Reduce(&db);

  printf("OK\n");
  return 0;
}
