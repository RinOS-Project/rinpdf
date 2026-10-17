#pragma once
#include "sfnt.h"
#include <cstddef>
#include <cstdint>
namespace rinpdf::renderer {
// Bounded GSUB SingleSubst (lookup type 1), for 'vrt2'/'vert' features.
// Returns true with unchanged glyph when no supported feature applies.
bool sfnt_vertical_substitution(const SfntFont& font,uint16_t glyph,
 uint16_t& replacement);
}
