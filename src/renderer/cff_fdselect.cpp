#include "sfnt.h"
#include "cff_dict.h"
#include "cff_index.h"
#include <cstdint>
#include <vector>
namespace rinpdf::renderer {
// A CID-keyed CFF must advertise ROS (12 30) in its Top DICT.
// Distinguish absent ROS from malformed FDSelect: the latter must fail closed.
bool sfnt_cff_is_cid_keyed(const SfntFont& font,bool& cid_keyed){
 cid_keyed=false;
 if(!font.bytes)return false;
 const SfntTable* cff=nullptr;
 for(const auto& t:font.tables)if(t.tag==0x43464620){cff=&t;break;}
 if(!cff||cff->length<4)return false;
 const uint8_t* d=font.bytes+cff->offset;
 const size_t n=cff->length;
 if(d[0]!=1||d[2]<4||d[2]>n)return false;
 size_t p=d[2];
 CffIndex names,top;
 if(!cff_read_index(d,n,p,names)||names.count!=1||
    !cff_read_index(d,n,p,top)||top.count!=1)return false;
 size_t first=0,last=0;
 if(!top.item(0,first,last))return false;
 bool ros_seen=false;
 if(!cff_walk_dict(d,n,first,last,[&](unsigned op,
                     const std::vector<int32_t>& args){
  if(op==0x100+30){
   if(ros_seen||args.size()!=3||args[0]<0||args[1]<0||
      args[2]<0)return false;
   ros_seen=true;
  }
  return true;
 }))return false;
 cid_keyed=ros_seen;
 return true;
}
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
 size_t cursor=d[2],first=0,end=0;
 CffIndex names,top;
 if(!cff_read_index(d,n,cursor,names)||names.count!=1||
    !cff_read_index(d,n,cursor,top)||top.count!=1||
    !top.item(0,first,end))return false;
 // Read CID operators 12 36 (FDArray) and 12 37 (FDSelect).
 int32_t array=-1,select=-1;
 if(!cff_walk_dict(d,n,first,end,[&](unsigned op,
                    const std::vector<int32_t>& args){
  if(op==0x100+36||op==0x100+37){
   if(args.size()!=1||args[0]<0)return false;
   if(op==0x100+36){if(array>=0)return false;array=args[0];}
   else{if(select>=0)return false;select=args[0];}
  }
  return true;
 }))return false;
 if(array<0||select<0||size_t(array)>=n||size_t(select)>=n)return false;
 cursor=size_t(array);
 CffIndex fdarray;
 if(!cff_read_index(d,n,cursor,fdarray)||fdarray.count==0||fdarray.count>256)return false;
 const uint16_t count=fdarray.count;
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
  if(!fdarray.item(selected,*dict_start,*dict_end))return false;
 }
 return true;
}
bool sfnt_cff_fd_for_glyph(const SfntFont& font,uint16_t glyph,
 uint16_t& fd_index){
 return locate_fd(font,glyph,fd_index,nullptr,nullptr);
}
bool sfnt_cff_cid_local_subroutine(const SfntFont& font,uint16_t glyph,
 int32_t operand,const uint8_t*& bytes,size_t& length){
 bytes=nullptr;length=0;
 uint16_t fd=0;size_t start=0,end=0;
 if(!locate_fd(font,glyph,fd,&start,&end))return false;
 const SfntTable* cff=nullptr;
 for(const auto& t:font.tables)if(t.tag==0x43464620){cff=&t;break;}
 if(!cff)return false;
 const uint8_t* d=font.bytes+cff->offset;
 const size_t n=cff->length;
 auto parse_dict=[&](size_t begin,size_t limit,unsigned desired,
                     int32_t& first,int32_t& second)->bool{
  bool found=false;
  if(!cff_walk_dict(d,n,begin,limit,[&](unsigned op,
                 const std::vector<int32_t>& args){
   if(op!=desired)return true;
   if(found||args.size()!=(desired==18?2u:1u))return false;
   first=args[0];
   if(desired==18)second=args[1];
   found=true;return true;
  }))return false;
  return found;
 };
 int32_t private_length=-1,private_offset=-1;
 if(!parse_dict(start,end,18,private_length,private_offset)||
    private_length<=0||private_offset<0||
    size_t(private_offset)>n||
    size_t(private_length)>n-size_t(private_offset))return false;
 int32_t relative_subrs=-1,unused=0;
 if(!parse_dict(size_t(private_offset),
                size_t(private_offset)+size_t(private_length),
                19,relative_subrs,unused)||relative_subrs<0||
    size_t(relative_subrs)>n-size_t(private_offset))return false;
 const size_t index=size_t(private_offset)+size_t(relative_subrs);
 if(index>n||n-index<3)return false;
 const uint16_t count=(uint16_t(d[index])<<8)|d[index+1];
 if(!count)return false;
 const unsigned width=d[index+2];
 const size_t off=index+3;
 if(width<1||width>4||size_t(count)+1>(n-off)/width)return false;
 auto entry=[&](size_t i)->uint32_t{
  uint32_t v=0;
  for(unsigned j=0;j<width;++j)v=(v<<8)|d[off+i*width+j];
  return v;
 };
 if(entry(0)!=1)return false;
 uint32_t previous=1;
 for(size_t i=1;i<=count;++i){
  const uint32_t next=entry(i);
  if(next<previous)return false;
  previous=next;
 }
 const int32_t bias=count<1240?107:count<33900?1131:32768;
 const int64_t id=int64_t(operand)+bias;
 if(id<0||id>=count)return false;
 const uint32_t a=entry(size_t(id)),b=entry(size_t(id)+1);
 const size_t payload=off+(size_t(count)+1)*width;
 if(payload>n||a<1||b<a||size_t(b-1)>n-payload)return false;
 bytes=d+payload+a-1;
 length=b-a;
 return length>0&&length<=65536;
}

}
