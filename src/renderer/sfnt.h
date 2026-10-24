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
struct GlyphPoint {int16_t x=0,y=0;bool on_curve=false;};
struct SimpleGlyph {
 int16_t x_min=0,y_min=0,x_max=0,y_max=0;
 std::vector<uint16_t> contour_ends;
 std::vector<GlyphPoint> points;
};
// Decodes a bounded simple TrueType glyf outline. Empty glyphs succeed
// with zero points. Composite glyphs are currently unsupported.
bool sfnt_simple_glyph(const SfntFont& font,uint16_t glyph_id,SimpleGlyph& outline);
// Resolve simple or translated component-only composite outlines.
// Unsupported composite transforms and point matching fail closed.
bool sfnt_glyph_outline(const SfntFont& font,uint16_t glyph_id,SimpleGlyph& outline);
// Obtain validated units-per-em and horizontal glyph advance from head,
// hhea and hmtx. Needed to position real embedded TrueType outlines.
bool sfnt_horizontal_metrics(const SfntFont& font,uint16_t glyph_id,
 uint16_t& units_per_em,uint16_t& advance_width);
// Read a Unicode BMP cmap format 4, or format 12 for supplementary codepoints.
// Returns false for malformed/unsupported mappings.
bool sfnt_glyph_id(const SfntFont& font,uint32_t unicode,uint16_t& glyph);
// Validate the CFF 1.0 INDEX chain and locate the CharStrings INDEX.
// This does not decode Type 2 glyph outlines.
struct CffCharStrings {
 size_t offset=0;
 size_t length=0;
 size_t index_offset=0;
 uint16_t count=0;
};
// Select a CID-keyed CFF Font DICT via validated FDSelect format 0 or 3.
bool sfnt_cff_fd_for_glyph(const SfntFont& font,uint16_t glyph,
 uint16_t& fd_index);
bool sfnt_cff_cid_local_subroutine(const SfntFont& font,uint16_t glyph,
 int32_t operand,const uint8_t*& bytes,size_t& length);
bool sfnt_cff_local_subroutine(const SfntFont& font,int32_t operand,
 const uint8_t*& bytes,size_t& length);
bool sfnt_cff_global_subroutine(const SfntFont& font,int32_t operand,
 const uint8_t*& bytes,size_t& length);
bool sfnt_cff_glyph_program(const SfntFont& font,uint16_t glyph_id,
 const uint8_t*& bytes,size_t& length);
bool sfnt_cff_charstrings(const SfntFont& font,CffCharStrings& out);
// Decode a bounded subset of Type 2 CharStrings into font-unit cubic paths.
// Unsupported hinting, subroutines and flex operators fail closed.
struct CffPoint {double x=0,y=0;};
using CffPath=std::vector<CffPoint>;
bool sfnt_cff_simple_outline(const SfntFont& font,uint16_t glyph,
 std::vector<CffPath>& contours);
}
