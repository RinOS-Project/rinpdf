#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
namespace rinpdf::parser {
// A deliberately bounded subset of ToUnicode CMap: one/two-byte source codes
// and single Unicode scalar destinations, via beginbfchar/beginbfrange.
struct CMapKey {uint8_t width=0;uint16_t code=0;
 bool operator<(const CMapKey& b) const {
  return width!=b.width?width<b.width:code<b.code;
 }
};
bool parse_tounicode_cmap(const uint8_t* data,size_t size,
 std::map<CMapKey,uint32_t>& mapping);
}
