#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "../parser/cid_font.h"
#include "../parser/contents.h"
#include "premultiplied_surface.h"
#include "premultiplied_cmyk_surface.h"
namespace rinpdf::renderer {
struct FontResource { std::string base_font,subtype,encoding; std::vector<uint8_t> embedded_truetype; std::map<uint8_t,uint32_t> differences; parser::CIDGlyphMap cid_map; parser::CIDWidths cid_widths; parser::CIDVerticalMetrics vertical_metrics; std::string language; };
using FontResources=std::map<std::string,FontResource>;
using MarkedProperties=parser::PageProperties;
using ImageResources=std::map<std::string,parser::PageImage>;
using FormResources=std::map<std::string,parser::PageForm>;
using GraphicsStateResources=parser::PageGraphicsStates;
struct PageBox { double x0=0,y0=0,x1=0,y1=0; };
bool render_rectangles_box_with_fonts(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride,PageBox box,const FontResources& fonts,
 const MarkedProperties& properties={},const ImageResources& images={},const FormResources& forms={},
 bool clear_background=true,unsigned form_depth=0,
 const GraphicsStateResources& graphic_states={},
 PremultipliedSurface* transparent_target=nullptr,
 PremultipliedCMYKSurface* cmyk_target=nullptr,
 const std::map<std::string,std::string>& named_color_spaces={},
 const std::map<std::string,parser::SeparationColorSpace>& separations={});
bool render_rectangles_box(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride,PageBox box);
// Legacy API uses an implicit 0,0,width,height box.
bool render_rectangles(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride);
}
