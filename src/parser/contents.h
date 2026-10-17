#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <string>
namespace rinpdf::parser {
struct PageFont {
 std::string base_font;
 std::string subtype;
 std::string encoding;
 std::vector<uint8_t> embedded_truetype; // decoded /FontFile2, if present
 std::map<uint8_t,uint32_t> differences; // explicit PDF byte-to-Unicode overrides
};
// Resolve effective inherited /Resources and supported font dictionary metadata.
// Does not imply font rasterization or character encoding support.
bool read_page_fonts(const uint8_t* data,size_t size,size_t page_index,
 std::map<std::string,PageFont>& fonts);
struct MediaBox { double x0,y0,x1,y1; unsigned rotation=0; };
bool read_page_box(const uint8_t *data,size_t size,size_t page_index,MediaBox& box);
// Decode /Contents by zero-based page index; supports validated classic and stream xref.
bool read_page_contents(const uint8_t *data,size_t size,size_t page_index,std::vector<uint8_t>& contents,size_t limit=64*1024*1024);
}
