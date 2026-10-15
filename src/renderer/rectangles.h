#pragma once
#include <cstddef>
#include <cstdint>
namespace rinpdf::renderer {
struct PageBox { double x0=0,y0=0,x1=0,y1=0; };
bool render_rectangles_box(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride,PageBox box);
// Legacy API uses an implicit 0,0,width,height box.
bool render_rectangles(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride);
}
