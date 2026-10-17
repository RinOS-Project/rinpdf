#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace rinpdf::renderer {
struct FontResource { std::string base_font,subtype; std::vector<uint8_t> embedded_truetype; };
using FontResources=std::map<std::string,FontResource>;
struct PageBox { double x0=0,y0=0,x1=0,y1=0; };
bool render_rectangles_box_with_fonts(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride,PageBox box,const FontResources& fonts);
bool render_rectangles_box(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride,PageBox box);
// Legacy API uses an implicit 0,0,width,height box.
bool render_rectangles(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride);
}
