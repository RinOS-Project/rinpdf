#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::renderer {
struct SfntTable {uint32_t tag=0, offset=0, length=0;};
struct SfntFont {
 const uint8_t* bytes=nullptr;
 size_t size=0;
 uint16_t glyph_count=0;
 std::vector<SfntTable> tables;
};
// Supports TrueType sfnt and OpenType/CFF containers (directory only).
// Borrowed data must remain alive as long as SfntFont is used.
bool parse_sfnt(const uint8_t* bytes,size_t length,SfntFont& font);
// Read a Unicode BMP cmap format 4, or format 12 for supplementary codepoints.
// Returns false for malformed/unsupported mappings.
bool sfnt_glyph_id(const SfntFont& font,uint32_t unicode,uint16_t& glyph);
}
