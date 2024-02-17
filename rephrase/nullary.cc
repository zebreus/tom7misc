#include "nullary.h"

#include "el.h"
#include "el-pass.h"
#include "base/logging.h"
#include "functional-map.h"

namespace { struct Unit { }; }
using FunctionalSet = FunctionalMap<std::string, Unit>;

namespace el {

// Argument is the set of nullary constructors.
struct NullaryPass : public Pass<FunctionalSet> {
  using Pass::Pass;

};

Nullary::Nullary(AstPool *pool) : pool(pool) {}

const Exp *Nullary::Rewrite(const Exp *e) {
  NullaryPass pass(pool);
  LOG(FATAL) << "Unimplemented";
  return nullptr;
}

}
