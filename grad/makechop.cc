
// 2023: Make choppy functions for fluint8.

#include <optional>
#include <array>
#include <vector>
#include <set>
#include <string>

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
#include "ansi.h"
#include "timer.h"

using Choppy = ChoppyGrid<256>;
using DB = Choppy::DB;
using Allocator = Exp::Allocator;
using Table = Exp::Table;

// This canonicalizes values in [-1,1) such that every value
// within each 1/256th interval is mapped to its canonical
// representative (exactly k/128).
static const Exp *MakeCanonical(DB *db) {

  const Exp *canon = nullptr;

  for (int i = 0; i < 256; i++) {
    int scale = i - 128;
    DB::key_type key = DB::BasisKey(i);
    auto it = db->fns.find(key);
    CHECK(it != db->fns.end());
    const Exp *e =
      db->alloc.TimesC(
          it->second,
          Exp::GetU16((half)scale));
    if (canon == nullptr) canon = e;
    else canon = db->alloc.PlusE(canon, e);
  }

  auto ao = Choppy::GetChoppy(canon);
  CHECK(ao.has_value());

  printf("Canon chop:");
  for (int i = 0; i < ao.value().size(); i++)
    printf(" %d", ao.value()[i]);
  printf("\n");

  return canon;
}


int main(int argc, char **argv) {
  AnsiInit();

  DB db;
  db.LoadFile("basis8.txt");

  const Exp *canon = MakeCanonical(&db);

  ImageRGBA img(1920, 1920);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  Table result = Exp::TabulateExpression(canon);
  const uint32_t color = 0xFF9900FF;
  GradUtil::Graph(result, color, &img);

  // VerboseChoppy(v, &img);

  img.Save("makechop.png");

  string canons = Exp::Serialize(canon) + "\n";
  Util::WriteFile("canon.txt", canons);

  return 0;
}
