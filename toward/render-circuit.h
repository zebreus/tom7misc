
#ifndef _TOWARD_RENDER_CIRCUIT_H
#define _TOWARD_RENDER_CIRCUIT_H

#include <vector>

#include "image.h"

struct CellLibrary;
struct Circuit;
struct Layout;

// Render a circuit for debugging.
ImageRGBA RenderCircuit(const CellLibrary &library,
                        const Circuit &circuit,
                        // If is_var[n] is in range, then the
                        // nth input is known to be a variable.
                        // Can just pass an empty vector.
                        std::vector<bool> is_var = {});

ImageRGBA RenderLayout(const CellLibrary &library,
                       const Layout &layout);

ImageRGBA RenderCircuitMini(const CellLibrary &library,
                            const Circuit &circuit,
                            std::vector<bool> is_var = {});

Circuit TruncateCircuit(Circuit circuit, int max_layers);

#endif
