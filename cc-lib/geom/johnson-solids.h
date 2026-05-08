
#ifndef _CC_LIB_GEOM_JOHNSON_SOLIDS_H
#define _CC_LIB_GEOM_JOHNSON_SOLIDS_H

#include <string_view>

#include "polyhedra.h"

// For n in 1..92.
Polyhedron JohnsonSolid(int n);

std::string_view JohnsonSolidName(int n);

#endif

