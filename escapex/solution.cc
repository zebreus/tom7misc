
#include "solution.h"

#include <string>
#include <cstdint>
#include <string_view>
#include <vector>

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

  size_t len = pp.W32();
  if (!pp.OK()) return false;

  std::vector<int> dd = EscapeRLE::Decode(&pp, len);
  if (!pp.OK()) return false;

  CHECK(dd.size() == len);

  sol->Clear();
  sol->dirs.reserve(dd.size());
  for (int i = 0; i < len; i++) sol->Append(dd[i]);
  sol->verified = false;

  return pp.Empty() && pp.OK();
}

// static
bool Solution::Equal(const Solution &l, const Solution &r) {
  if (l.dirs.size() != r.dirs.size()) return false;
  for (size_t i = 0; i < l.dirs.size(); i++) {
    if (l.dirs[i] != r.dirs[i]) return false;
  }
  return true;
}
