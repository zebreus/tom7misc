#include "atomic-util.h"

// Shared across all EightCounters instances.
thread_local size_t internal::EightCounters::idx;
