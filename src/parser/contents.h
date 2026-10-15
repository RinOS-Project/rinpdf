#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
struct MediaBox { double x0,y0,x1,y1; unsigned rotation=0; };
bool read_page_box(const uint8_t *data,size_t size,size_t page_index,MediaBox& box);
// Decode /Contents of a page by zero-based index, classic xref documents only.
bool read_page_contents(const uint8_t *data,size_t size,size_t page_index,std::vector<uint8_t>& contents,size_t limit=64*1024*1024);
}
