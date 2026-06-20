
#include <vector>
#include <string>

struct World {
  // Just for input/output. A variable is uniquely
  // identified by its index.
  std::vector<std::string> symbol_names;


};

struct Prop;

struct Var {
  int id = 0;
};

// This is recursive, so we need
struct Prop {
  std::variant<Var, Binop, Unop> p;
};

