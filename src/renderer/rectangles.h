#pragma once
#include <cstddef>
#include <cstdint>
namespace rinpdf::renderer {
// A deliberately limited content-stream renderer: solid grayscale filled rectangles.
bool render_rectangles(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride);
}
