
#include "string-table.h"

#include <string>
#include <span>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void SimpleTests() {
  StringTable st;

  const std::string s1 = "hello";
  StringTable::Entry e1 = st.Add(s1);
  CHECK(st.GetView(e1) == s1);

  // Second copy. It could get deduplicated.
  StringTable::Entry e1b = st.Add(s1);
  CHECK(st.GetView(e1b) == s1);

  const std::string s2 = "world";
  StringTable::Entry e2 = st.Add(s2);

  // All Entries themselves are still valid.
  CHECK(st.GetView(e1) == s1);
  CHECK(st.GetView(e1b) == s1);
  CHECK(st.GetView(e2) == s2);

  StringTable::Entry ee = st.Add("");
  CHECK(st.GetView(ee).empty());

  // Add string with embedded nuls.
  const std::string s0("a\0b", 3);
  StringTable::Entry e0 = st.Add(s0);
  CHECK(st.GetView(e0) == s0);

  std::span<const uint8_t> sp = st.GetSpan(e1);
  CHECK(sp.size() == s1.size());
  CHECK(sp[0] == 'h');

  st.Finalize();
  CHECK(st.GetView(e1) == s1);
  CHECK(st.GetView(e1b) == s1);
  CHECK(st.GetView(e2) == s2);
  CHECK(st.GetView(ee).empty());
  CHECK(st.GetView(e0) == s0);
}

int main(int argc, char **argv) {
  ANSI::Init();

  SimpleTests();

  Print("OK\n");
  return 0;
}
