#include "tounicode.h"
#include <map>
#include <utility>

namespace rinpdf::parser {

// Compatibility entry point for callers that require one Unicode scalar per
// source code. Keep parsing semantics aligned with the sequence-aware parser.
bool parse_tounicode_cmap(const uint8_t* data,size_t size,
 std::map<CMapKey,uint32_t>& output){
 std::map<CMapKey,std::u32string> sequences;
 if(!parse_tounicode_sequences(data,size,sequences))return false;
 std::map<CMapKey,uint32_t> decoded;
 for(const auto& [key,sequence]:sequences){
  if(sequence.size()!=1)return false;
  const uint32_t scalar=static_cast<uint32_t>(sequence.front());
  if(scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff))return false;
  decoded.emplace(key,scalar);
 }
 output=std::move(decoded);
 return true;
}
}
