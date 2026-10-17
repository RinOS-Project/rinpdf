#include "xref_stream.h"
#include <limits>
namespace rinpdf::parser {
bool decode_xref_stream_entries(const uint8_t* data,size_t size,
 const std::vector<unsigned>& widths,const std::vector<uint64_t>& index,
 std::vector<XrefStreamEntry>& result){
 if((!data&&size)||widths.size()!=3||index.empty()||index.size()%2!=0||
    index.size()>2048)return false;
 size_t stride=0;
 for(unsigned w:widths){
  if(w>8||stride>24-w)return false;
  stride+=w;
 }
 if(stride==0)return false;
 uint64_t total=0;
 uint64_t previous_end=0;
 for(size_t i=0;i<index.size();i+=2){
  const uint64_t first=index[i],count=index[i+1];
  if(count==0||count>1000000||first>1000000||
     count>1000000-first||total>1000000-count)return false;
  // Reject overlapping or non-monotonic ranges; the caller may sort
  // entries only after successful decoding.
  if(i&&first<previous_end)return false;
  previous_end=first+count;
  total+=count;
 }
 if(total>static_cast<uint64_t>(std::numeric_limits<size_t>::max()/stride)||
    size!=static_cast<size_t>(total)*stride)return false;
 std::vector<XrefStreamEntry> decoded;
 decoded.reserve(static_cast<size_t>(total));
 size_t pos=0;
 auto field=[&](unsigned width)->uint64_t{
  uint64_t value=0;
  for(unsigned j=0;j<width;++j)value=(value<<8)|data[pos++];
  return value;
 };
 for(size_t i=0;i<index.size();i+=2){
  for(uint64_t n=0;n<index[i+1];++n){
   uint64_t type=widths[0]?field(widths[0]):1;
   uint64_t first=field(widths[1]),second=field(widths[2]);
   if(type>2)return false;
   // Type 0 (free), 1 (offset,generation), 2 (object stream,index).
   // A compressed object has an implicit generation number of zero.
   if(type==1&&second>65535)return false;
   decoded.push_back({static_cast<size_t>(index[i]+n),
                      static_cast<uint8_t>(type),first,second});
  }
 }
 result=std::move(decoded);
 return true;
}
}
