#pragma once
#include "sfnt.h"
#include "path_mask.h"
#include <vector>
namespace rinpdf::renderer {
// Convert a TrueType simple outline to flattened, closed pixel-space paths.
// This does not perform font scaling, hinting, or glyph placement.
bool flatten_simple_glyph(const SimpleGlyph& glyph,double scale,
 double offset_x,double offset_y,double tolerance,
 std::vector<std::vector<MaskPoint>>& paths);
}
