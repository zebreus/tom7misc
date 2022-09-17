#include "diff.h"

#include <utility>
#include <vector>
#include <string>
#include <functional>

#include "ansi.h"
#include "util.h"
#include "edit-distance.h"

std::pair<string, string> ColorDiff(const Exp *a, const Exp *b) {
  std::vector<string> v1 = Util::Split(Exp::Serialize(a), ' ');
  std::vector<string> v2 = Util::Split(Exp::Serialize(b), ' ');

  // Compare strings, but treat the first character as more expensive.
  // TODO: Should probably just segment these?
  auto Compare = [](const string &s1, const string &s2) {
      return
        EditDistance::GetAlignment(
            s1.size(), s2.size(),
            [](int idx) { return idx == 0 ? 10000 : 1; },
            [](int idx) { return idx == 0 ? 10000 : 1; },
            [&s1, &s2](int a, int b) {
              if (s1[a] == s2[b]) return 0;
              else if (a == 0 || b == 0) return 10000;
              else return 1;
            });
    };

  const auto [cmds, cost] =
    EditDistance::GetAlignment(
        v1.size(), v2.size(),
                 // Insertions and deletions are constant cost
                 [](int) { return 100; },
                 [](int) { return 100; },
                 [&v1, &v2, &Compare](int idx1, int idx2) {
                   return Compare(v1[idx1], v2[idx2]).second;
                 });

  string before, after;
  for (const EditDistance::Command &cmd : cmds) {
    if (cmd.Delete()) {
      if (!before.empty()) StringAppendF(&before, " ");
      StringAppendF(&before, ABGCOLOR(100, 0, 0, "%s"),
                    v1[cmd.index1].c_str());
    } else if (cmd.Insert()) {
      if (!after.empty()) StringAppendF(&after, " ");
      StringAppendF(&after, ABGCOLOR(0, 100, 0, "%s"),
                    v2[cmd.index2].c_str());
    } else {
      CHECK(cmd.Subst());
      if (!before.empty()) StringAppendF(&before, " ");
      if (!after.empty()) StringAppendF(&after, " ");

      const string &s1 = v1[cmd.index1];
      const string &s2 = v2[cmd.index2];
      if (s1 == s2) {
        StringAppendF(&before, "%s", s1.c_str());
        StringAppendF(&after, "%s", s2.c_str());
      } else {
        auto [ccmds, ccost_] = Compare(s1, s2);
        string bef, aft;
        // PERF: Don't need to set color for each character!
        // Not we use ANSI_FG so that we don't reset the active background.
        for (const EditDistance::Command &cc : ccmds) {
          if (cc.Delete()) {
            StringAppendF(&bef, ANSI_FG(255, 0, 0) "%c", s1[cc.index1]);
          } else if (cc.Insert()) {
            StringAppendF(&aft, ANSI_FG(0, 255, 0) "%c", s2[cc.index2]);
          } else {
            CHECK(cc.Subst());
            char c1 = s1[cc.index1];
            char c2 = s2[cc.index2];
            if (c1 == c2) {
              StringAppendF(&bef, ANSI_FG(255, 255, 255) "%c", c1);
              StringAppendF(&aft, ANSI_FG(255, 255, 255) "%c", c2);
            } else {
              StringAppendF(&bef, ANSI_FG(255, 0, 0) "%c", c1);
              StringAppendF(&aft, ANSI_FG(0, 255, 0) "%c", c2);
            }
          }
        }
        StringAppendF(&before, ABGCOLOR(64, 64, 0, "%s"), bef.c_str());
        StringAppendF(&after, ABGCOLOR(64, 64, 0, "%s"), aft.c_str());
      }
    }
  }

  return std::make_pair(before, after);
}
