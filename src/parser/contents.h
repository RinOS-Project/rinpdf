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
struct PageForm {std::vector<uint8_t> content; double bbox[4]{0,0,0,0}; double matrix[6]{1,0,0,1,0,0}; bool own_resources=false; std::map<std::string,std::string> xobject_aliases;};
bool read_page_forms(const uint8_t* data,size_t size,size_t page_index,
 std::map<std::string,PageForm>& forms);
struct PageImage {unsigned width=0,height=0,channels=0;std::vector<uint8_t> pixels;std::vector<uint8_t> alpha;};
bool read_page_images(const uint8_t* data,size_t size,size_t page_index,
 std::map<std::string,PageImage>& images);
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
