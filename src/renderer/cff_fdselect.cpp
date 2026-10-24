#include "sfnt.h"
#include <cstdint>
#include <vector>
namespace rinpdf::renderer {
static bool locate_fd(const SfntFont& font,uint16_t glyph,
 uint16_t& fd_index,size_t* dict_start,size_t* dict_end){
 if(!font.bytes||glyph>=font.glyph_count)return false;
 const SfntTable* cff=nullptr;
 for(const auto& t:font.tables)if(t.tag==0x43464620){cff=&t;break;}
 if(!cff||cff->length<4)return false;
 const uint8_t* d=font.bytes+cff->offset;
 const size_t n=cff->length;
 if(d[0]!=1||d[2]<4||d[2]>n)return false;
 auto u16=[&](size_t p)->uint16_t{
  return uint16_t(d[p])<<8|d[p+1];
 };
 auto index=[&](size_t& cursor,size_t& first,size_t& end,
                uint16_t& count)->bool{
  if(cursor>n||n-cursor<2)return false;
  count=u16(cursor);cursor+=2;
  if(!count){first=end=cursor;return true;}
  if(cursor>=n)return false;
  const unsigned width=d[cursor++];
  if(width<1||width>4||(size_t(count)+1)>(n-cursor)/width)return false;
  auto value=[&](size_t i)->uint32_t{
   uint32_t result=0;
   for(unsigned k=0;k<width;++k)
    result=(result<<8)|d[cursor+i*width+k];
   return result;
  };
  if(value(0)!=1)return false;
  uint32_t previous=1;
  for(size_t i=1;i<=count;++i){
   const uint32_t current=value(i);
   if(current<previous)return false;
   previous=current;
  }
  first=cursor+(size_t(count)+1)*width;
  if(first>n||size_t(previous-1)>n-first)return false;
  end=first+previous-1;cursor=end;
  return true;
 };
 size_t cursor=d[2],first=0,end=0;
 uint16_t count=0;
 if(!index(cursor,first,end,count)||count!=1||
    !index(cursor,first,end,count)||count!=1)return false;
 // Read CID operators 12 36 (FDArray) and 12 37 (FDSelect).
 int32_t array=-1,select=-1;
 std::vector<int32_t> args;
 for(size_t p=first;p<end;){
  const unsigned op=d[p++];
  if(op<=21){
   if(op==12){
    if(p>=end)return false;
    const unsigned escaped=d[p++];
    if(escaped==36||escaped==37){
     if(args.size()!=1||args[0]<0)return false;
     if(escaped==36)array=args[0];else select=args[0];
    }
   }
   args.clear();continue;
  }
  int32_t v=0;
  if(op>=32&&op<=246)v=int32_t(op)-139;
  else if(op>=247&&op<=250){
   if(p>=end)return false;
   v=(int32_t(op)-247)*256+d[p++]+108;
  }else if(op>=251&&op<=254){
   if(p>=end)return false;
   v=-(int32_t(op)-251)*256-d[p++]-108;
  }else if(op==28){
   if(end-p<2)return false;
   v=int16_t(u16(p));p+=2;
  }else if(op==29){
   if(end-p<4)return false;
   const uint32_t bits=(uint32_t(d[p])<<24)|(uint32_t(d[p+1])<<16)|
     (uint32_t(d[p+2])<<8)|d[p+3];
   v=int32_t(bits);p+=4;
  }else if(op==30){
   bool done=false;
   while(p<end&&!done){
    const unsigned pair=d[p++];
    done=((pair>>4)==15||(pair&15)==15);
   }
   if(!done)return false;
  }else return false;
  if(args.size()>=48)return false;
  args.push_back(v);
 }
 if(array<0||select<0||size_t(array)>=n||size_t(select)>=n)return false;
 cursor=size_t(array);
 if(!index(cursor,first,end,count)||count==0||count>256)return false;
 const size_t pos=size_t(select);
 uint16_t selected=0;
 if(d[pos]==0){
  if(pos+1>n||size_t(font.glyph_count)>n-pos-1)return false;
  selected=d[pos+1+glyph];
 }else if(d[pos]==3){
  if(n-pos<5)return false;
  const uint16_t ranges=u16(pos+1);
  if(ranges==0||ranges>font.glyph_count||
     size_t(ranges)> (n-pos-5)/3)return false;
  const size_t sentinel=pos+3+size_t(ranges)*3;
  if(sentinel>n||n-sentinel<2||u16(sentinel)!=font.glyph_count)
   return false;
  uint16_t previous=0;
  bool seen=false;
  for(size_t i=0;i<ranges;++i){
   const size_t at=pos+3+i*3;
   const uint16_t first_gid=u16(at),next=(i+1<ranges)?
     u16(at+3):font.glyph_count;
   if((i==0&&first_gid!=0)||(i&&first_gid<=previous)||
      first_gid>=next||next>font.glyph_count)return false;
   previous=first_gid;
   if(glyph>=first_gid&&glyph<next){
    selected=d[at+2];seen=true;
   }
  }
  if(!seen)return false;
 }else return false;
 if(selected>=count)return false;
 fd_index=selected;
 if(dict_start&&dict_end){
  // Read the selected FDArray INDEX entry without trusting FDSelect alone.
  const size_t at=size_t(array);
  if(n-at<3)return false;
  const uint16_t entries=u16(at);
  const unsigned width=d[at+2];
  if(entries!=count||width<1||width>4||
     size_t(entries)+1>(n-at-3)/width)return false;
  const size_t offsets=at+3;
  auto entry=[&](size_t i)->uint32_t{
   uint32_t v=0;
   for(unsigned j=0;j<width;++j)v=(v<<8)|d[offsets+i*width+j];
   return v;
  };
  const uint32_t a=entry(selected),b=entry(size_t(selected)+1);
  const size_t data=offsets+(size_t(entries)+1)*width;
  if(a<1||b<a||data>n||size_t(b-1)>n-data)return false;
  *dict_start=data+size_t(a)-1;
  *dict_end=data+size_t(b)-1;
 }
 return true;
}
bool sfnt_cff_fd_for_glyph(const SfntFont& font,uint16_t glyph,
 uint16_t& fd_index){
 return locate_fd(font,glyph,fd_index,nullptr,nullptr);
}
}
