#pragma once
#include <cstddef>
#include <cstdint>
namespace rinpdf::renderer {
// Small diagnostic raster glyphs, NOT Courier outlines.
// Returns false for every glyph without an explicitly defined pattern.
bool draw_diagnostic_glyph(uint8_t ascii,double origin_x,double baseline_y,
 double em_size,unsigned width,unsigned height,uint8_t* rgba,size_t stride,
 double page_x0,double page_y1,double scale_x,double scale_y,
 uint8_t red,uint8_t green,uint8_t blue);
bool diagnostic_glyph_supported(uint8_t ascii);
}
