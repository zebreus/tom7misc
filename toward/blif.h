
#ifndef _TOWARD_BLIF_H
#define _TOWARD_BLIF_H

#include <string>
#include <string_view>

#include "prop.h"

// I think it stands for "Berkeley Logic Interchange Format," but anyway it's
// one of the formats that Berkeley ABC can read. It's good for this
// use case because it allows us an "external don't care" proposition.

std::string ToBLIF(std::string_view model_name,
                   const World &world, const Prop &prop,
                   const Prop &exdc);

#endif
