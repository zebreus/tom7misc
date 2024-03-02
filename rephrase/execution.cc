
#include "execution.h"

#include <variant>

#include "bytecode.h"
#include "base/logging.h"

namespace bc {

Execution::Execution(const Program &pgm) : pgm(pgm) {

}

}  // namespace bc
