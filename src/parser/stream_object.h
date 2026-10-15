#pragma once
#include "xref.h"
#include "stream.h"
#include <vector>
namespace rinpdf::parser {
// Resolve direct or indirect /Length using classic xref entries.
bool locate_indirect_stream(const uint8_t *data,size_t size,const XrefEntry& entry,const std::vector<XrefEntry>& xref,StreamRange& result);
}
