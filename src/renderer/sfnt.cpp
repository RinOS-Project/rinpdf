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
bool sfnt_simple_glyph(const SfntFont& font,uint16_t id,SimpleGlyph& outline){
 if(!font.bytes||id>=font.glyph_count)return false;
 const auto* head=table(font,0x68656164);
 const auto* loca=table(font,0x6c6f6361);
 const auto* glyf=table(font,0x676c7966);
 if(!head||!loca||!glyf||head->length<54)return false;
 uint16_t raw_format=0;
 if(!u16(font.bytes,font.size,size_t(head->offset)+50,raw_format))return false;
 const bool short_offsets=raw_format==0;
 if(!short_offsets&&raw_format!=1)return false;
 const size_t unit=short_offsets?2:4;
 if(size_t(id)+1>SIZE_MAX/unit-1||
    loca->length<(size_t(id)+2)*unit)return false;
 auto location=[&](size_t index,uint32_t& out)->bool{
  const size_t p=size_t(loca->offset)+index*unit;
  if(short_offsets){uint16_t value=0;if(!u16(font.bytes,font.size,p,value))return false;out=uint32_t(value)*2;return true;}
  return u32(font.bytes,font.size,p,out);
 };
 uint32_t begin=0,end=0;
 if(!location(id,begin)||!location(size_t(id)+1,end)||
    begin>end||end>glyf->length)return false;
 SimpleGlyph parsed;
 if(begin==end){outline=std::move(parsed);return true;}
 const size_t base=size_t(glyf->offset)+begin,limit=size_t(glyf->offset)+end;
 uint16_t contours_raw=0;
 if(limit-base<10||!u16(font.bytes,font.size,base,contours_raw))return false;
 const int16_t contours=int16_t(contours_raw);
 if(contours<0||contours>1024)return false;
 auto signed16=[&](size_t p,int16_t& val)->bool{
  uint16_t raw=0;if(!u16(font.bytes,font.size,p,raw))return false;
  val=int16_t(raw);return true;
 };
 if(!signed16(base+2,parsed.x_min)||!signed16(base+4,parsed.y_min)||
    !signed16(base+6,parsed.x_max)||!signed16(base+8,parsed.y_max)||
    parsed.x_min>parsed.x_max||parsed.y_min>parsed.y_max)return false;
 size_t pos=base+10;
 if(size_t(contours)>(limit-pos)/2)return false;
 uint16_t previous=0;
 for(int k=0;k<contours;++k){
  uint16_t last=0;
  if(!u16(font.bytes,font.size,pos,last)||
     (k&&last<=previous)||last>=16384)return false;
  parsed.contour_ends.push_back(last);
  previous=last;pos+=2;
 }
 uint16_t instructions=0;
 if(!u16(font.bytes,limit,pos,instructions))return false;
 pos+=2;
 if(instructions>limit-pos)return false;
 pos+=instructions;
 const size_t points=contours?size_t(previous)+1:0;
 std::vector<uint8_t> flags;
 flags.reserve(points);
 while(flags.size()<points){
  if(pos>=limit)return false;
  const uint8_t flag=font.bytes[pos++];
  size_t repetitions=1;
  if(flag&0x08){if(pos>=limit)return false;repetitions+=font.bytes[pos++];}
  if(repetitions>points-flags.size())return false;
  flags.insert(flags.end(),repetitions,flag);
 }
 parsed.points.resize(points);
 auto coordinates=[&](bool x_axis)->bool{
  int32_t coordinate=0;
  for(size_t i=0;i<points;++i){
   const uint8_t flag=flags[i];
   const bool is_short=flag&(x_axis?0x02:0x04);
   const bool same=flag&(x_axis?0x10:0x20);
   int32_t delta=0;
   if(is_short){
    if(pos>=limit)return false;
    delta=font.bytes[pos++];
    if(!same)delta=-delta;
   }else if(!same){
    int16_t signed_delta=0;
    if(pos>limit||limit-pos<2||!signed16(pos,signed_delta))return false;
    delta=signed_delta;pos+=2;
   }
   coordinate+=delta;
   if(coordinate<-32768||coordinate>32767)return false;
   if(x_axis)parsed.points[i].x=int16_t(coordinate);
   else parsed.points[i].y=int16_t(coordinate);
  }
  return true;
 };
 for(size_t i=0;i<points;++i)parsed.points[i].on_curve=(flags[i]&1)!=0;
 if(!coordinates(true)||!coordinates(false))return false;
 outline=std::move(parsed);
 return true;
}

bool sfnt_horizontal_metrics(const SfntFont& font,uint16_t id,
 uint16_t& units_per_em,uint16_t& advance_width){
 if(!font.bytes||id>=font.glyph_count)return false;
 const SfntTable* head=table(font,0x68656164);
 const SfntTable* hhea=table(font,0x68686561);
 const SfntTable* hmtx=table(font,0x686d7478);
 if(!head||!hhea||!hmtx||head->length<54||hhea->length<36)return false;
 uint16_t units=0,metrics=0;
 if(!u16(font.bytes,font.size,size_t(head->offset)+18,units)||
    !u16(font.bytes,font.size,size_t(hhea->offset)+34,metrics)||
    units<16||units>16384||metrics==0||metrics>font.glyph_count)return false;
 // LongHorMetric is advanceWidth (2 bytes), leftSideBearing (2 bytes).
 // Remaining glyphs inherit the last stored advanceWidth.
 if(size_t(metrics)>hmtx->length/4)return false;
 const size_t metric_index=std::min<size_t>(id,metrics-1);
 uint16_t advance=0;
 if(!u16(font.bytes,font.size,size_t(hmtx->offset)+metric_index*4,advance))
  return false;
 if(id>=metrics){
  const size_t extra=size_t(id)-metrics;
  if(extra>(SIZE_MAX-size_t(metrics)*4)/2||
     hmtx->length<size_t(metrics)*4+(extra+1)*2)return false;
 }
 units_per_em=units;advance_width=advance;
 return true;
}

}
