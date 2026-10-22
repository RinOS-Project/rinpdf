#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include "tounicode.h"
#include "xref_stream.h"
#include "cid_font.h"
namespace rinpdf::parser {
// Scoped per-thread view of an immutable document xref cache. The scope
// never outlives the owning document and is restored after nested calls.
struct VerifiedPageCounts {
 mutable std::mutex mutex;
 std::map<std::pair<int64_t,int64_t>,size_t> counts;
 std::map<size_t,std::set<std::pair<int64_t,int64_t>>> paths;
 std::map<size_t,uint64_t> path_ages;
 std::map<std::pair<uint64_t,size_t>,size_t> path_oldest_first;
 uint64_t path_clock=0;
 size_t resident_bytes=0;
 static constexpr size_t count_weight=sizeof(std::pair<int64_t,int64_t>)+sizeof(size_t)+48;
 static size_t path_weight(size_t length){return sizeof(size_t)+length*(sizeof(std::pair<int64_t,int64_t>)+48)+48;}
};
class ScopedPageXref {
 const ScopedPageXref* previous_;
 const uint8_t* data_;
 size_t size_;
 const std::vector<XrefEntry>* entries_;
 const std::vector<XrefStreamEntry>* compressed_;
 const Value* trailer_;
 mutable std::map<std::pair<int64_t,int64_t>,Value> objects_;
 mutable bool path_attempted_=false;
 mutable size_t path_target_=0;
 mutable std::set<std::pair<int64_t,int64_t>> page_path_;
 VerifiedPageCounts* shared_counts_=nullptr;
 mutable std::map<std::pair<int64_t,int64_t>,size_t> verified_counts_;
public:
 ScopedPageXref(const uint8_t* data,size_t size,
  const std::vector<XrefEntry>& entries,
  const std::vector<XrefStreamEntry>& compressed,const Value& trailer,
  VerifiedPageCounts* shared_counts=nullptr);
 ~ScopedPageXref();
 struct View {
  std::vector<XrefEntry> owned_entries;
  std::vector<XrefStreamEntry> owned_compressed;
  Value owned_trailer;
  const std::vector<XrefEntry>* entries=nullptr;
  const std::vector<XrefStreamEntry>* compressed=nullptr;
  const Value* trailer=nullptr;
  bool open(const uint8_t* data,size_t size);
 };
 static bool cached_count(const uint8_t* data,size_t size,
  const Value& ref,size_t& count);
 static void store_count(const uint8_t* data,size_t size,
  const Value& ref,size_t count);
 static bool has_scope(const uint8_t* data,size_t size);
 static bool path_contains(const uint8_t* data,size_t size,size_t target,const Value& ref);
 static bool get_cached_path(const uint8_t* data,size_t size,size_t target);
 static void store_path(const uint8_t* data,size_t size,size_t target,
  const std::set<std::pair<int64_t,int64_t>>& path);
 static bool resolve(const uint8_t* data,size_t size,
  const std::vector<XrefEntry>& entries,
  const std::vector<XrefStreamEntry>& compressed,const Value& ref,
  Value& out,std::map<size_t,Value>* compressed_cache);
 static bool read(const uint8_t* data,size_t size,
  std::vector<XrefEntry>& entries,
  std::vector<XrefStreamEntry>& compressed,Value& trailer);
 ScopedPageXref(const ScopedPageXref&)=delete;
 ScopedPageXref& operator=(const ScopedPageXref&)=delete;
};

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
struct PageImage {unsigned width=0,height=0,channels=0;bool stencil=false;std::vector<uint8_t> pixels;std::vector<uint8_t> alpha;std::vector<uint8_t> cmyk_pixels;};
struct SeparationColorSpace {
 std::string alternate;
 double c0[4]{0,0,0,0},c1[4]{1,1,1,1},exponent=1;
 unsigned components=0;
 // FunctionType 3: bounded stitching of FunctionType 2 subfunctions.
 struct Segment {
  double c0[4]{},c1[4]{},exponent=1;
  std::vector<uint8_t> samples;
  unsigned sample_count=0;
  double domain[2]{0,1},encode[2]{0,1};
  double range[8]{0,1,0,1,0,1,0,1};
  double decode[8]{0,1,0,1,0,1,0,1};
  std::vector<Segment> children;
  std::vector<double> bounds,stitch_encode;
 };
 std::vector<Segment> segments;
 std::vector<double> bounds;
 std::vector<double> encode;
 // FunctionType 0: one-input sampled tint table, 8-bit precision.
 std::vector<uint8_t> samples;
 unsigned sample_count=0;
 double sample_domain[2]{0,1};
 double sample_encode[2]{0,1};
 double sample_range[8]{0,1,0,1,0,1,0,1};
 double sample_decode[8]{0,1,0,1,0,1,0,1};
};
struct PageForm {std::vector<uint8_t> content; double bbox[4]{0,0,0,0}; double matrix[6]{1,0,0,1,0,0}; bool own_resources=false; bool transparency_group=false; bool isolated_transparency_group=false; bool knockout_transparency_group=false; std::string group_color_space; std::vector<uint8_t> group_icc_profile; unsigned group_icc_channels=0; std::map<std::string,std::string> local_color_spaces; std::map<std::string,SeparationColorSpace> local_separations; std::map<std::string,std::string> xobject_aliases; std::map<std::string,PageForm> local_forms; std::map<std::string,PageImage> local_images; std::map<std::string,PageFont> local_fonts;};
bool read_page_color_spaces(const uint8_t* data,size_t size,size_t page_index,
 std::map<std::string,std::string>& aliases,
 std::map<std::string,SeparationColorSpace>& separations);
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
