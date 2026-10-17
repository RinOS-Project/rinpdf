#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <utility>
namespace rinpdf::parser {
// A deliberately bounded subset of ToUnicode CMap: one-to-four-byte source codes
// and single Unicode scalar destinations, via beginbfchar/beginbfrange.
struct CMapKey {uint8_t width=0;uint32_t code=0;
 bool operator<(const CMapKey& b) const {
  return width!=b.width?width<b.width:code<b.code;
 }
};
// Preserves multi-codepoint destinations and bfrange array entries.
bool parse_tounicode_sequences(const uint8_t* data,size_t size,
 std::map<CMapKey,std::u32string>& mapping,
 std::vector<std::pair<CMapKey,CMapKey>>* codespaces=nullptr,
 const std::string& parent_name={},
 const std::map<CMapKey,std::u32string>* parent_mapping=nullptr,
 const std::vector<std::pair<CMapKey,CMapKey>>* parent_codespaces=nullptr);
bool parse_tounicode_cmap(const uint8_t* data,size_t size,
 std::map<CMapKey,uint32_t>& mapping);
}
