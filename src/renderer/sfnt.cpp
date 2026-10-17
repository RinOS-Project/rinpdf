#include "sfnt.h"
#include <algorithm>
#include <limits>
#include <set>
namespace rinpdf::renderer {
static bool u16(const uint8_t* d,size_t n,size_t p,uint16_t& v){
 if(!d||p>n||n-p<2)return false;
 v=(uint16_t(d[p])<<8)|d[p+1];return true;
}
static bool u32(const uint8_t* d,size_t n,size_t p,uint32_t& v){
 if(!d||p>n||n-p<4)return false;
 v=(uint32_t(d[p])<<24)|(uint32_t(d[p+1])<<16)|
   (uint32_t(d[p+2])<<8)|d[p+3];return true;
}
static const SfntTable* table(const SfntFont& f,uint32_t tag){
 auto it=std::find_if(f.tables.begin(),f.tables.end(),
   [&](const SfntTable& t){return t.tag==tag;});
 return it==f.tables.end()?nullptr:&*it;
}
bool parse_sfnt(const uint8_t* d,size_t n,SfntFont& out){
 if(!d||n<12||n>64*1024*1024)return false;
 uint32_t magic=0;
 uint16_t count=0;
 if(!u32(d,n,0,magic)||!u16(d,n,4,count)||
    (magic!=0x00010000&&magic!=0x4f54544f)||
    count==0||count>256||size_t(count)>(n-12)/16)return false;
 std::vector<SfntTable> tables;
 std::set<uint32_t> tags;
 for(size_t i=0;i<count;++i){
  size_t pos=12+i*16;
  uint32_t tag=0,off=0,len=0;
  if(!u32(d,n,pos,tag)||!u32(d,n,pos+8,off)||
     !u32(d,n,pos+12,len)||off>n||len>n-off||
     !tags.insert(tag).second)return false;
  tables.push_back({tag,off,len});
 }
 SfntFont result{d,n,0,std::move(tables)};
 const SfntTable* maxp=table(result,0x6d617870);
 if(!maxp||maxp->length<6||
    !u16(d,n,size_t(maxp->offset)+4,result.glyph_count)||
    result.glyph_count==0)return false;
 out=std::move(result);return true;
}
bool sfnt_glyph_id(const SfntFont& f,uint32_t code,uint16_t& glyph){
 const SfntTable* cmap=table(f,0x636d6170);
 if(!cmap||cmap->length<4)return false;
 const size_t start=cmap->offset,end=start+cmap->length;
 uint16_t count=0;
 if(!u16(f.bytes,f.size,start+2,count)||count>256||
    size_t(count)>(end-start-4)/8)return false;
 for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<count;++i){
  size_t rec=start+4+size_t(i)*8;
  uint16_t platform=0,encoding=0;
  uint32_t offset=0;
  if(!u16(f.bytes,f.size,rec,platform)||
     !u16(f.bytes,f.size,rec+2,encoding)||
     !u32(f.bytes,f.size,rec+4,offset))return false;
  if(!((platform==3&&(encoding==1||encoding==10))||platform==0))continue;
  if(offset> end-start||end-start-offset<4)return false;
  size_t sub=start+offset;
  uint16_t format=0;
  if(!u16(f.bytes,f.size,sub,format))return false;
  if(pass==0&&format!=12)continue;
  if(pass==1&&format!=4)continue;
  if(format==12){
   uint32_t length=0,groups=0;
   if(end-sub<16||!u32(f.bytes,f.size,sub+4,length)||
      !u32(f.bytes,f.size,sub+12,groups)||length<16||
      length>end-sub||groups>(length-16)/12||groups>100000)return false;
   for(uint32_t j=0;j<groups;++j){
    size_t p=sub+16+size_t(j)*12;
    uint32_t lo=0,hi=0,base=0;
    if(!u32(f.bytes,f.size,p,lo)||!u32(f.bytes,f.size,p+4,hi)||
       !u32(f.bytes,f.size,p+8,base)||lo>hi)return false;
    if(code>=lo&&code<=hi){
     uint64_t id=uint64_t(base)+code-lo;
     if(id>=f.glyph_count)return false;
     glyph=static_cast<uint16_t>(id);return true;
    }
   }
  }else if(code<=65535){
   uint16_t length=0,segx2=0;
   if(!u16(f.bytes,f.size,sub+2,length)||length<24||length>end-sub||
      !u16(f.bytes,f.size,sub+6,segx2)||segx2==0||(segx2&1))return false;
   size_t seg=segx2/2;
   if(seg>(length-16)/8)return false;
   size_t ends=sub+14,starts=ends+2*seg+2;
   size_t deltas=starts+2*seg,ranges=deltas+2*seg;
   for(size_t j=0;j<seg;++j){
    uint16_t lo=0,hi=0,delta=0,range=0;
    if(!u16(f.bytes,f.size,ends+2*j,hi)||
       !u16(f.bytes,f.size,starts+2*j,lo)||
       !u16(f.bytes,f.size,deltas+2*j,delta)||
       !u16(f.bytes,f.size,ranges+2*j,range)||lo>hi)return false;
    if(code>=lo&&code<=hi){
     uint16_t id=0;
     if(range==0)id=uint16_t((code+delta)&65535);
     else{
      size_t addr=ranges+2*j+range+2*(code-lo);
      if(addr<sub||addr>=sub+length||!u16(f.bytes,f.size,addr,id))return false;
      if(id)id=uint16_t((id+delta)&65535);
     }
     if(id>=f.glyph_count)return false;
     glyph=id;return true;
    }
   }
  }
 }
 return false;
}
}
