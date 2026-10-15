#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
struct XrefEntry { size_t object; size_t offset; unsigned generation; bool in_use; };
/* Classic xref tables only; does not support xref streams or incremental revisions. */
bool read_classic_xref(const uint8_t *data,size_t size,std::vector<XrefEntry>& entries);
}
