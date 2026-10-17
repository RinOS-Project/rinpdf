#pragma once
#include "xref.h"
#include "value.h"
#include <cstdint>
#include <cstddef>
#include <vector>
namespace rinpdf::parser {
// ISO 32000 cross-reference stream entry, including compressed objects.
// For type 2, field1 is object stream number and field2 is its index.
struct XrefStreamEntry {
 size_t object=0;
 uint8_t type=0;
 uint64_t field1=0;
 uint64_t field2=0;
};
// Decode already-unfiltered stream data. 'index' contains (first,count)
// pairs from /Index, or {0,Size} when /Index is absent.
// This does not resolve XRef streams, /Prev chains, or compressed objects.
bool decode_xref_stream_entries(const uint8_t* data,size_t size,
 const std::vector<unsigned>& widths,const std::vector<uint64_t>& index,
 std::vector<XrefStreamEntry>& result);
// Read a direct-length XRef stream object at the byte offset provided
// by startxref. The trailer is returned as the stream dictionary.
// Does not follow /Prev or resolve compressed objects yet.
bool read_xref_stream_at(const uint8_t* data,size_t size,size_t offset,
 std::vector<XrefStreamEntry>& entries,Value& trailer);

enum class XrefFormat { Classic, Stream };
// Dispatch by the final startxref target. Classic entries keep their
// existing resolver format; compressed stream entries remain separate.
bool read_xref_section(const uint8_t* data,size_t size,
 XrefFormat& format,std::vector<XrefEntry>& classic_entries,
 std::vector<XrefStreamEntry>& stream_entries,Value& trailer);

}
