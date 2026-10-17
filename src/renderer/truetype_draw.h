#pragma once
#include "text_show.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace rinpdf::renderer {
// Minimal untransformed, unclipped glyph drawing for embedded TrueType.
// ASCII code maps via Unicode cmap. The caller must guard PDF font Encoding,
// graphics transforms and clipping. Fails before painting on invalid glyphs.
bool render_embedded_truetype_runs(const std::vector<uint8_t>& font,
 const std::vector<TextRun>& runs,double font_size,double origin_x,
 double baseline_y,unsigned width,unsigned height,uint8_t* rgba,size_t stride,
 double box_x0,double box_y1,double scale_x,double scale_y,
 uint8_t r,uint8_t g,uint8_t b,double& next_x);
}
