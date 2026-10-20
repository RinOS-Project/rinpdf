#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rinpdf::renderer {

// Bounded ICC v2/v4 RGB matrix/TRC parser. Unsupported LUT and multi-process
// profiles are rejected, rather than silently interpreted as sRGB.
struct IccMatrixProfile {
 std::array<std::array<double,3>,3> xyz{};
 std::array<double,3> gamma{};
 static uint32_t be32(const std::vector<uint8_t>& p,size_t i){
  return (uint32_t(p[i])<<24)|(uint32_t(p[i+1])<<16)|
         (uint32_t(p[i+2])<<8)|p[i+3];
 }
 static double s15f16(const std::vector<uint8_t>& p,size_t i){
  return double(int32_t(be32(p,i)))/65536.0;
 }
 bool parse(const std::vector<uint8_t>& p){
  if(p.size()<132||p.size()>4*1024*1024||be32(p,0)!=p.size()||
     be32(p,16)!=0x52474220u||be32(p,20)!=0x58595a20u||
     be32(p,36)!=0x61637370u)return false;
  const uint32_t count=be32(p,128);
  if(count>256||size_t(count)>(p.size()-132)/12)return false;
  constexpr uint32_t tags[6]={
    0x7258595au,0x6758595au,0x6258595au,
    0x72545243u,0x67545243u,0x62545243u};
  bool found[6]{};
  for(uint32_t i=0;i<count;++i){
   const size_t t=132+size_t(i)*12;
   const uint32_t tag=be32(p,t),off=be32(p,t+4),len=be32(p,t+8);
   if(off>p.size()||len>p.size()-off) return false;
   for(unsigned c=0;c<6;++c)if(tag==tags[c]){
    if(found[c])return false;
    found[c]=true;
    if(c<3){
     if(len<20||be32(p,off)!=0x58595a20u)return false;
     for(unsigned j=0;j<3;++j){
      const double v=s15f16(p,off+8+j*4);
      if(!std::isfinite(v)||std::abs(v)>4)return false;
      xyz[j][c]=v;
     }
    }else{
     if(len<14||be32(p,off)!=0x63757276u||
        be32(p,off+8)!=1)return false;
     gamma[c-3]=double((unsigned(p[off+12])<<8)|p[off+13])/256.0;
     if(gamma[c-3]<0.1||gamma[c-3]>10)return false;
    }
   }
  }
  for(bool v:found)if(!v)return false;
  return true;
 }
 std::array<double,3> to_xyz(const std::array<double,3>& rgb) const {
  std::array<double,3> out{};
  for(unsigned r=0;r<3;++r)
   for(unsigned c=0;c<3;++c)
    out[r]+=xyz[r][c]*std::pow(std::clamp(rgb[c],0.0,1.0),gamma[c]);
  return out;
 }
};
} // namespace rinpdf::renderer
