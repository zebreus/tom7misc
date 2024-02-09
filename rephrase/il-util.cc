
#include "il-util.h"

#include <unordered_set>
#include <string>

#include "il.h"
#include "il-pass.h"
#include "functional-map.h"

namespace { struct Unit { }; }
using FunctionalSet = FunctionalMap<std::string, Unit>;

namespace il {

struct FreeVarsPass : public Pass<FunctionalSet> {
  using Pass::Pass;

  // Anything that binds a variable needs to pass it in the functional
  // set, so we can detect that it's not free. Only a few constructs
  // bind expression variables.

  // Need to collect the vars for the body, so we can't just do
  // this by overriding DoDec.
  const Exp *DoLet(const std::vector<const Dec *> &ds,
                   const Exp *e,
                   const Exp *guess,
                   FunctionalSet bound) override {
    std::vector<const Dec *> dds;
    dds.reserve(ds.size());
    for (const Dec *d : ds) {
      const Dec *dd = DoDec(d, bound);
      dds.push_back(dd);
      if (dd->type == DecType::VAL) {
        const auto &[tvs, x, rhs] = dd->Val();
        bound = bound.Insert(x, {});
      }
    }
    return pool->Let(dds, DoExp(e, bound), guess);
  }

  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Exp *body,
                  const Exp *guess,
                  FunctionalSet bound) override {
    return pool->Fn(self, x, DoExp(body, bound.Insert(x, {})), guess);
  }


  std::unordered_set<std::string> freevars;
};

std::unordered_set<std::string> ILUtil::FreeExpVars(const Exp *e) {
  AstPool temp;
  FreeVarsPass pass(&temp);
  (void)pass.DoExp(e, {});
  return pass.freevars;
}

}  // namespace il
