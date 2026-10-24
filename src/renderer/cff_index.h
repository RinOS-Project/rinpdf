#pragma once
#include <cstddef>
#include <cstdint>
namespace rinpdf::renderer {
struct CffIndex {
 const uint8_t* data=nullptr;
 size_t size=0;
 size_t offsets=0;
 size_t payload=0;
 size_t end=0;
 uint16_t count=0;
 uint8_t offsize=0;
 uint32_t last=1;
 uint32_t read(size_t i)const{
  uint32_t v=0;
  for(unsigned k=0;k<offsize;++k)
   v=(v<<8)|data[offsets+i*offsize+k];
  return v;
 }
 bool item(size_t i,size_t& first,size_t& limit)const{
  if(i>=count||!offsize)return false;
  const uint32_t a=read(i),b=read(i+1);
  if(a<1||b<a||b>last)return false;
  first=payload+size_t(a)-1;
  limit=payload+size_t(b)-1;
  return true;
 }
};
inline bool cff_read_index(const uint8_t* d,size_t n,size_t& cursor,
                           CffIndex& result){
 if(!d||cursor>n||n-cursor<2)return false;
 const uint16_t count=(uint16_t(d[cursor])<<8)|d[cursor+1];
 cursor+=2;
 if(!count){
  result={d,n,cursor,cursor,cursor,0,0,1};
  return true;
 }
 if(cursor>=n)return false;
 const uint8_t width=d[cursor++];
 if(width<1||width>4||(size_t(count)+1)>(n-cursor)/width)return false;
 CffIndex parsed{d,n,cursor,0,0,count,width,1};
 if(parsed.read(0)!=1)return false;
 uint32_t previous=1;
 for(size_t i=1;i<=count;++i){
  const uint32_t next=parsed.read(i);
  if(next<previous)return false;
  previous=next;
 }
 parsed.payload=cursor+(size_t(count)+1)*width;
 if(parsed.payload>n||size_t(previous-1)>n-parsed.payload)return false;
 parsed.end=parsed.payload+size_t(previous)-1;
 parsed.last=previous;
 cursor=parsed.end;
 result=parsed;
 return true;
}
}
