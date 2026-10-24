#include "sfnt.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cmath>
#include <set>
namespace rinpdf::renderer {
static bool be16(const uint8_t* d,size_t n,size_t pos,uint16_t& out){
 if(!d||pos>n||n-pos<2)return false;
 out=(uint16_t(d[pos])<<8)|d[pos+1];return true;
}
static bool be32(const uint8_t* d,size_t n,size_t pos,uint32_t& out){
 if(!d||pos>n||n-pos<4)return false;
 out=(uint32_t(d[pos])<<24)|(uint32_t(d[pos+1])<<16)|
      (uint32_t(d[pos+2])<<8)|d[pos+3];return true;
}
static const SfntTable* find(const SfntFont& f,uint32_t tag){
 for(const auto& t:f.tables)if(t.tag==tag)return &t;
 return nullptr;
}
static bool resolve(const SfntFont& font,uint16_t id,SimpleGlyph& out,
 std::set<uint16_t>& active,unsigned depth){
 if(depth>=8||!active.insert(id).second)return false;
 struct Guard{std::set<uint16_t>& active;uint16_t id;~Guard(){active.erase(id);}} guard{active,id};
 if(!font.bytes||id>=font.glyph_count)return false;
 const auto* head=find(font,0x68656164);
 const auto* loca=find(font,0x6c6f6361);
 const auto* glyf=find(font,0x676c7966);
 if(!head||!loca||!glyf||head->length<54)return false;
 uint16_t format=0;
 if(!be16(font.bytes,font.size,size_t(head->offset)+50,format)||format>1)
  return false;
 const size_t unit=format?4:2;
 if(loca->length<(size_t(id)+2)*unit)return false;
 auto location=[&](size_t index,uint32_t& offset){
  if(format)return be32(font.bytes,font.size,size_t(loca->offset)+index*4,offset);
  uint16_t value=0;
  if(!be16(font.bytes,font.size,size_t(loca->offset)+index*2,value))return false;
  offset=uint32_t(value)*2;return true;
 };
 uint32_t a=0,b=0;
 if(!location(id,a)||!location(size_t(id)+1,b)||a>b||b>glyf->length)return false;
 if(a==b){SimpleGlyph empty;out=std::move(empty);return true;}
 const size_t begin=size_t(glyf->offset)+a,end=size_t(glyf->offset)+b;
 uint16_t raw=0;
 if(end-begin<10||!be16(font.bytes,font.size,begin,raw))return false;
 if(static_cast<int16_t>(raw)>=0)return sfnt_simple_glyph(font,id,out);
 SimpleGlyph result;
 bool more=true;
 size_t pos=begin+10,components=0;
 while(more){
  if(++components>32||pos>end||end-pos<4)return false;
  uint16_t flags=0,component=0;
  if(!be16(font.bytes,end,pos,flags)||!be16(font.bytes,end,pos+2,component))
   return false;
  pos+=4;
  constexpr uint16_t ARG_WORDS=1,ARGS_XY=2,ROUND_XY=4,
                     SCALE=8,MORE=32,XY_SCALE=64,TWO_BY_TWO=128,
                     INSTRUCTIONS=256,SCALED_OFFSET=2048,UNSCALED_OFFSET=4096;
  if((flags&SCALED_OFFSET)&&(flags&UNSCALED_OFFSET))return false;
  if(((flags&SCALE)!=0)+((flags&XY_SCALE)!=0)+((flags&TWO_BY_TWO)!=0)>1)
   return false;
  int32_t arg1=0,arg2=0;
  if(flags&ARG_WORDS){
   uint16_t x=0,y=0;
   if(pos>end||end-pos<4||!be16(font.bytes,end,pos,x)||
      !be16(font.bytes,end,pos+2,y))return false;
   arg1=(flags&ARGS_XY)?static_cast<int16_t>(x):x;
   arg2=(flags&ARGS_XY)?static_cast<int16_t>(y):y;
   pos+=4;
  }else{
   if(pos>end||end-pos<2)return false;
   arg1=(flags&ARGS_XY)?static_cast<int8_t>(font.bytes[pos]):font.bytes[pos];
   arg2=(flags&ARGS_XY)?static_cast<int8_t>(font.bytes[pos+1]):font.bytes[pos+1];
   pos+=2;
  }
  double xx=1,xy=0,yx=0,yy=1;
  auto matrix_value=[&](double& value)->bool{
   uint16_t raw=0;
   if(pos>end||end-pos<2||!be16(font.bytes,end,pos,raw))return false;
   value=double(static_cast<int16_t>(raw))/16384.0;pos+=2;
   return true;
  };
  if(flags&SCALE){if(!matrix_value(xx))return false;yy=xx;}
  else if(flags&XY_SCALE){
   if(!matrix_value(xx)||!matrix_value(yy))return false;
  }else if(flags&TWO_BY_TWO){
   if(!matrix_value(xx)||!matrix_value(xy)||
      !matrix_value(yx)||!matrix_value(yy))return false;
  }
  SimpleGlyph child;
  if(!resolve(font,component,child,active,depth+1))return false;
  if(result.points.size()+child.points.size()>16384||
     result.contour_ends.size()+child.contour_ends.size()>1024)return false;
  const size_t previous=result.points.size();
  double dx=0,dy=0;
  if(flags&ARGS_XY){
   dx=arg1;dy=arg2;
   if(flags&SCALED_OFFSET){
    const double x=xx*dx+xy*dy,y=yx*dx+yy*dy;
    dx=x;dy=y;
   }
   // Default and UNSCALED_COMPONENT_OFFSET retain unscaled offsets.
   if(flags&ROUND_XY){dx=std::round(dx);dy=std::round(dy);}
  }else{
   if(size_t(arg1)>=result.points.size()||
      size_t(arg2)>=child.points.size())return false;
   const auto& parent=result.points[size_t(arg1)];
   const auto& match=child.points[size_t(arg2)];
   dx=parent.x-(xx*match.x+xy*match.y);
   dy=parent.y-(yx*match.x+yy*match.y);
  }
  for(auto point:child.points){
   const double fx=xx*point.x+xy*point.y+dx;
   const double fy=yx*point.x+yy*point.y+dy;
   if(!std::isfinite(fx)||!std::isfinite(fy)||
      fx<-32768||fx>32767||fy<-32768||fy>32767)return false;
   const long x=std::lround(fx),y=std::lround(fy);
   if(x<-32768||x>32767||y<-32768||y>32767)return false;
   point.x=static_cast<int16_t>(x);point.y=static_cast<int16_t>(y);
   result.points.push_back(point);
  }
  for(uint16_t end_point:child.contour_ends)
   result.contour_ends.push_back(static_cast<uint16_t>(previous+end_point));
  more=(flags&MORE)!=0;
  if(!more&&(flags&INSTRUCTIONS)){
   uint16_t count=0;
   // Check the two-byte instruction length before subtracting from the
   // glyph boundary: size_t subtraction must never wrap on truncated data.
   if(pos>end||end-pos<2||!be16(font.bytes,end,pos,count))return false;
   pos+=2;
   if(size_t(count)>end-pos)return false;
   pos+=count;
  }
 }
 if(!result.points.empty()){
  result.x_min=result.x_max=result.points.front().x;
  result.y_min=result.y_max=result.points.front().y;
  for(const auto& point:result.points){
   result.x_min=std::min(result.x_min,point.x);
   result.x_max=std::max(result.x_max,point.x);
   result.y_min=std::min(result.y_min,point.y);
   result.y_max=std::max(result.y_max,point.y);
  }
 }
 out=std::move(result);
 return true;
}
bool sfnt_glyph_outline(const SfntFont& font,uint16_t id,SimpleGlyph& out){
 std::set<uint16_t> active;
 SimpleGlyph result;
 if(!resolve(font,id,result,active,0))return false;
 out=std::move(result);
 return true;
}

}
