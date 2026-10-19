#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include "tounicode.h"
#include "cid_font.h"
namespace rinpdf::parser {
struct PageFont {
 std::string base_font;
 std::string subtype;
 std::string encoding;
 std::vector<uint8_t> embedded_truetype; // decoded /FontFile2, if present
 double missing_width=0; // FontDescriptor /MissingWidth; zero means unspecified
 std::map<uint8_t,double> simple_widths; // PDF FirstChar/LastChar/Widths, in glyph units
 std::map<uint8_t,uint32_t> differences; // explicit PDF byte-to-Unicode overrides
 std::map<CMapKey,uint32_t> to_unicode; // legacy scalar metadata
 std::map<CMapKey,std::u32string> unicode_sequences;
 std::vector<std::pair<CMapKey,CMapKey>> unicode_codespaces;
 CIDGlyphMap cid_map; // only for Type0 / CIDFontType2
 CIDWidths cid_widths;
 CIDVerticalMetrics vertical_metrics;
 std::string language;
};
struct PageImage {unsigned width=0,height=0,channels=0;bool stencil=false;std::vector<uint8_t> pixels;std::vector<uint8_t> alpha;};
struct PageForm {std::vector<uint8_t> content; double bbox[4]{0,0,0,0}; double matrix[6]{1,0,0,1,0,0}; bool own_resources=false; bool transparency_group=false; bool isolated_transparency_group=false; bool knockout_transparency_group=false; std::map<std::string,std::string> xobject_aliases; std::map<std::string,PageForm> local_forms; std::map<std::string,PageImage> local_images; std::map<std::string,PageFont> local_fonts;};
bool read_page_forms(const uint8_t* data,size_t size,size_t page_index,
 std::map<std::string,PageForm>& forms);
bool read_page_images(const uint8_t* data,size_t size,size_t page_index,
 std::map<std::string,PageImage>& images);
struct PageGraphicsState {double fill_alpha=1,stroke_alpha=1;bool has_fill_alpha=false,has_stroke_alpha=false;std::string blend_mode="Normal";bool has_blend_mode=false;};
using PageGraphicsStates=std::map<std::string,PageGraphicsState>;
bool read_page_graphics_states(const uint8_t* data,size_t size,size_t page_index,
 PageGraphicsStates& states);
struct PageProperty {std::string language; std::string actual_text; bool has_actual_text=false;};
using PageProperties=std::map<std::string,PageProperty>;
bool read_page_properties(const uint8_t* data,size_t size,size_t page_index,
 PageProperties& properties);
// Resolve effective inherited /Resources and supported font dictionary metadata.
// Does not imply font rasterization or character encoding support.
bool read_page_fonts(const uint8_t* data,size_t size,size_t page_index,
 std::map<std::string,PageFont>& fonts);
struct MediaBox { double x0,y0,x1,y1; unsigned rotation=0; };
bool read_page_box(const uint8_t *data,size_t size,size_t page_index,MediaBox& box);
// Decode /Contents by zero-based page index; supports validated classic and stream xref.
bool read_page_contents(const uint8_t *data,size_t size,size_t page_index,std::vector<uint8_t>& contents,size_t limit=64*1024*1024);
}
