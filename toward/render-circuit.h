
#ifndef _TOWARD_RENDER_CIRCUIT_H
#define _TOWARD_RENDER_CIRCUIT_H

#include "image.h"
#include "circuit.h"
#include "cell-library.h"

// Render a circuit for debugging.
ImageRGBA RenderCircuit(const CellLibrary &library,
                        const Circuit &circuit);

ImageRGBA RenderCircuitMini(const CellLibrary &library,
                            const Circuit &circuit);

#endif
