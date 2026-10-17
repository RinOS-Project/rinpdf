#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <utility>
#include <cmath>
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
inline bool cid_numeric_width(const Value& value,double& width){
 if(value.kind==Value::Kind::Integer)width=static_cast<double>(value.integer);
 else if(value.kind==Value::Kind::Real)width=value.real;
 else return false;
 return std::isfinite(width)&&width>=0&&width<=100000;
}
// /W accepts [start [w1 w2 ...]] and [start end w].
inline bool parse_cid_widths(const Value* dw,const Value* w,CIDWidths& out){
 CIDWidths result;
 if(dw){
  if(!cid_numeric_width(*dw,result.default_width))return false;
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
     double width=0;
     if(!cid_numeric_width(v,width)||
        !result.overrides.emplace(static_cast<uint16_t>(first+j),
          width).second)return false;
    }
   }else if(next.kind==Value::Kind::Integer){
    if(next.integer<first||next.integer>65535||
       pos>=w->array.size())return false;
    const Value& width=w->array[pos++];
    double numeric_width=0;
    if(!cid_numeric_width(width,numeric_width))return false;
    for(unsigned i=first;i<=static_cast<unsigned>(next.integer);++i)
     if(!result.overrides.emplace(static_cast<uint16_t>(i),
        numeric_width).second)return false;
   }else return false;
  }
 }
 out=std::move(result);return true;
}

struct CIDVerticalMetric {double w1y=-1000,v1x=0,v1y=880;};
struct CIDVerticalMetrics {
 CIDVerticalMetric fallback;
 std::map<uint16_t,CIDVerticalMetric> overrides;
};
inline bool cid_vertical_number(const Value& v,double& n){
 if(v.kind==Value::Kind::Integer)n=static_cast<double>(v.integer);
 else if(v.kind==Value::Kind::Real)n=v.real;
 else return false;
 return std::isfinite(n)&&std::abs(n)<=100000;
}
inline bool cid_vertical_metric(const CIDVerticalMetrics& widths,
 const CIDWidths& horizontal,uint16_t cid,CIDVerticalMetric& out){
 const auto it=widths.overrides.find(cid);
 if(it!=widths.overrides.end()){out=it->second;return true;}
 out=widths.fallback;
 double width=0;
 if(!cid_width(horizontal,cid,width))return false;
 out.v1x=width/2;
 return true;
}
// /DW2 [v1y w1y], /W2 [c [w1y v1x v1y ...] cfirst clast w1y v1x v1y]
inline bool parse_cid_vertical_metrics(const Value* dw2,const Value* w2,
 CIDVerticalMetrics& out){
 CIDVerticalMetrics parsed;
 if(dw2){
  if(dw2->kind!=Value::Kind::Array||dw2->array.size()!=2||
     !cid_vertical_number(dw2->array[0],parsed.fallback.v1y)||
     !cid_vertical_number(dw2->array[1],parsed.fallback.w1y))return false;
 }
 if(w2){
  if(w2->kind!=Value::Kind::Array||w2->array.size()>65536)return false;
  size_t pos=0;
  while(pos<w2->array.size()){
   const auto& first=w2->array[pos++];
   if(first.kind!=Value::Kind::Integer||first.integer<0||
      first.integer>65535||pos>=w2->array.size())return false;
   const auto& spec=w2->array[pos++];
   unsigned start=static_cast<unsigned>(first.integer);
   if(spec.kind==Value::Kind::Array){
    if(spec.array.empty()||spec.array.size()%3||
       spec.array.size()/3>65536-start)return false;
    for(size_t i=0;i<spec.array.size();i+=3){
     CIDVerticalMetric v;
     if(!cid_vertical_number(spec.array[i],v.w1y)||
        !cid_vertical_number(spec.array[i+1],v.v1x)||
        !cid_vertical_number(spec.array[i+2],v.v1y)||
        !parsed.overrides.emplace(static_cast<uint16_t>(start+i/3),v).second)
       return false;
    }
   }else if(spec.kind==Value::Kind::Integer){
    if(spec.integer<first.integer||spec.integer>65535||
       pos+3>w2->array.size())return false;
    CIDVerticalMetric v;
    if(!cid_vertical_number(w2->array[pos++],v.w1y)||
       !cid_vertical_number(w2->array[pos++],v.v1x)||
       !cid_vertical_number(w2->array[pos++],v.v1y))return false;
    for(unsigned code=start;code<=static_cast<unsigned>(spec.integer);++code)
     if(!parsed.overrides.emplace(static_cast<uint16_t>(code),v).second)
      return false;
   }else return false;
  }
 }
 // The implicit v1x is computed per CID at lookup time.
 out=std::move(parsed);return true;
}

}
