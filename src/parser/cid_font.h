#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <utility>
#include "value.h"
namespace rinpdf::parser {
// PDF CIDToGIDMap: big-endian uint16 per CID. Identity is implicit.
struct CIDGlyphMap { bool identity=true; std::vector<uint8_t> bytes; };
inline bool cid_to_gid(const CIDGlyphMap& map,uint16_t cid,uint16_t& gid){
 if(map.identity){gid=cid;return true;}
 const size_t pos=size_t(cid)*2;
 if(pos>map.bytes.size()||map.bytes.size()-pos<2)return false;
 gid=uint16_t((uint16_t(map.bytes[pos])<<8)|map.bytes[pos+1]);
 return true;
}
inline bool cid_map_valid(const CIDGlyphMap& map){
 return map.identity?map.bytes.empty():
        !map.bytes.empty()&&map.bytes.size()%2==0&&
        map.bytes.size()<=131072;
}
struct CIDWidths {
 double default_width=1000;
 std::map<uint16_t,double> overrides;
};
inline bool cid_width(const CIDWidths& widths,uint16_t cid,double& width){
 const auto found=widths.overrides.find(cid);
 const double value=found==widths.overrides.end()?widths.default_width:found->second;
 if(value<0||value>100000)return false;
 width=value;return true;
}
// /W accepts [start [w1 w2 ...]] and [start end w].
inline bool parse_cid_widths(const Value* dw,const Value* w,CIDWidths& out){
 CIDWidths result;
 if(dw){
  if(dw->kind!=Value::Kind::Integer||dw->integer<0||
     dw->integer>100000)return false;
  result.default_width=static_cast<double>(dw->integer);
 }
 if(w){
  if(w->kind!=Value::Kind::Array||w->array.size()>65536)return false;
  size_t pos=0;
  while(pos<w->array.size()){
   const Value& start=w->array[pos++];
   if(start.kind!=Value::Kind::Integer||start.integer<0||
      start.integer>65535||pos>=w->array.size())return false;
   const Value& next=w->array[pos++];
   const unsigned first=static_cast<unsigned>(start.integer);
   if(next.kind==Value::Kind::Array){
    if(next.array.empty()||next.array.size()>65536-first)return false;
    for(size_t j=0;j<next.array.size();++j){
     const Value& v=next.array[j];
     if(v.kind!=Value::Kind::Integer||v.integer<0||v.integer>100000||
        !result.overrides.emplace(static_cast<uint16_t>(first+j),
          static_cast<double>(v.integer)).second)return false;
    }
   }else if(next.kind==Value::Kind::Integer){
    if(next.integer<first||next.integer>65535||
       pos>=w->array.size())return false;
    const Value& width=w->array[pos++];
    if(width.kind!=Value::Kind::Integer||width.integer<0||
       width.integer>100000)return false;
    for(unsigned i=first;i<=static_cast<unsigned>(next.integer);++i)
     if(!result.overrides.emplace(static_cast<uint16_t>(i),
        static_cast<double>(width.integer)).second)return false;
   }else return false;
  }
 }
 out=std::move(result);return true;
}

}
