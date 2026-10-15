#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
// Decode /Contents of a page by zero-based index, classic xref documents only.
bool read_page_contents(const uint8_t *data,size_t size,size_t page_index,std::vector<uint8_t>& contents,size_t limit=64*1024*1024);
}
