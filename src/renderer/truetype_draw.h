#pragma once
#include "text_show.h"
#include "../parser/cid_font.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
namespace rinpdf::renderer {
// Embedded TrueType glyph drawing with bounded PDF affine transforms.
// ASCII code maps via Unicode cmap. The caller must guard PDF font Encoding,
// font encodings and text-state features. Fails before painting on invalid glyphs.
struct GlyphTransform {
 // CTM in PDF user coordinates.
 double a=1,b=0,c=0,d=1,e=0,f=0;
 // Text matrix linear basis (translation supplied as origin_x/baseline_y).
 double ta=1,tb=0,tc=0,td=1;
 double character_spacing=0,word_spacing=0,horizontal_scale=1;
};
bool render_embedded_truetype_runs(const std::vector<uint8_t>& font,
 const std::vector<TextRun>& runs,double font_size,double origin_x,
 double baseline_y,unsigned width,unsigned height,uint8_t* rgba,size_t stride,
 double box_x0,double box_y1,double scale_x,double scale_y,
 uint8_t r,uint8_t g,uint8_t b,double& next_x,
 const std::vector<uint8_t>* clip_mask=nullptr,
 unsigned clip_x0=0,unsigned clip_y0=0,
 unsigned clip_x1=0,unsigned clip_y1=0,
 GlyphTransform transform={},
 const std::map<uint8_t,uint32_t>* differences=nullptr,
 const std::string& encoding="WinAnsiEncoding",
 const parser::CIDGlyphMap* cid_map=nullptr);
}
