
#include "solution.h"

#include <string>
#include <cstdint>
#include <string_view>

#include "level-base.h"
#include "escape-rle.h"
#include "bytes.h"
#include "packet-writer.h"
#include "packet-parser.h"

using namespace std;

string Solution::ToString() const {
  PacketWriter pw;
  pw.W32(Length());
  pw.Bytes(EscapeRLE::Encode(Length(), (int *)dirs.data()));
  return pw.ToString();
}

// static
bool Solution::FromString(std::string_view s, Solution *sol) {
  PacketParser pp(s);

  uint32_t len = pp.W32();

  unsigned int idx = 0;
  if (len < 0) return false;

  dir *dd = EscapeRLE::Decode(s, idx, len);

  if (!dd) return false;

  sol->Clear();
  sol->dirs.reserve(len);
  for (int i = 0; i < len; i++) sol->Append(dd[i]);
  sol->verified = false;

  if (!pp.OK()) return false;

  return true;
}

// static
bool Solution::Equal(const Solution &l, const Solution &r) {
  if (l.dirs.size() != r.dirs.size()) return false;
  for (size_t i = 0; i < l.dirs.size(); i++) {
    if (l.dirs[i] != r.dirs[i]) return false;
  }
  return true;
}
