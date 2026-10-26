#include "ccitt_image.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <utility>
#include <limits>
#if defined(RINPDF_HAVE_TIFF)
#include <tiffio.h>
#endif
namespace rinpdf::parser {
#if defined(RINPDF_HAVE_TIFF)
namespace {
struct Memory {
 const uint8_t* bytes;
 size_t size;
 size_t offset=0;
};
tsize_t read_bytes(thandle_t handle,tdata_t dest,tsize_t count){
 auto& m=*static_cast<Memory*>(handle);
 if(count<0||m.offset>m.size)return 0;
 const size_t n=std::min(size_t(count),m.size-m.offset);
 if(n)std::memcpy(dest,m.bytes+m.offset,n);
 m.offset+=n;return tsize_t(n);
}
tsize_t write_bytes(thandle_t,tdata_t,tsize_t){return 0;}
toff_t seek_bytes(thandle_t handle,toff_t off,int whence){
 auto& m=*static_cast<Memory*>(handle);
 if(m.offset>m.size)return toff_t(-1);
 size_t base=0;
 if(whence==SEEK_SET)base=0;
 else if(whence==SEEK_CUR)base=m.offset;
 else if(whence==SEEK_END)base=m.size;
 else return toff_t(-1);
 // libtiff represents signed relative moves in an unsigned toff_t.
 // Interpret its sign bit for SEEK_CUR/SEEK_END without signed overflow.
 if(whence!=SEEK_SET && off>(std::numeric_limits<toff_t>::max()/2)){
  const toff_t backwards=toff_t(0)-off;
  if(backwards>base)return toff_t(-1);
  m.offset=base-size_t(backwards);
 }else{
  if(off>m.size-base)return toff_t(-1);
  m.offset=base+size_t(off);
 }
 return toff_t(m.offset);
}
int close_bytes(thandle_t){return 0;}
toff_t size_bytes(thandle_t handle){return static_cast<Memory*>(handle)->size;}
int map_bytes(thandle_t,tdata_t*,toff_t*){return 0;}
void unmap_bytes(thandle_t,tdata_t,toff_t){}
void put16(std::vector<uint8_t>& b,size_t p,unsigned v){
 b[p]=uint8_t(v);b[p+1]=uint8_t(v>>8);
}
void put32(std::vector<uint8_t>& b,size_t p,uint32_t v){
 for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(i*8));
}
}
#endif
bool decode_ccitt_image(const uint8_t* data,size_t length,
 unsigned width,unsigned height,int k,bool byte_align,bool black_is_one,
 std::vector<uint8_t>& grayscale){
 grayscale.clear();
 if(!data||!length||length>32*1024*1024||!width||!height||
    width>4096||height>4096||size_t(width)*height>16*1024*1024||
    (k < -1 || k > 32767))return false;
#if defined(RINPDF_HAVE_TIFF)
 // Wrap the raw fax stream in an in-memory classic TIFF IFD. This uses
 // libtiff's CCITT Group 3/4 codec without temporary files.
 constexpr unsigned tags=11;
 constexpr size_t header=8+2+tags*12+4;
 if(length>UINT32_MAX-header)return false;
 std::vector<uint8_t> tiff(header+length,0);
 tiff[0]='I';tiff[1]='I';put16(tiff,2,42);put32(tiff,4,8);
 put16(tiff,8,tags);
 unsigned n=0;
 auto tag=[&](unsigned id,unsigned type,uint32_t value){
  const size_t p=10+size_t(n++)*12;
  put16(tiff,p,id);put16(tiff,p+2,type);put32(tiff,p+4,1);
  put32(tiff,p+8,value);
 };
 tag(256,4,width);tag(257,4,height);tag(258,3,1);
 tag(259,3,k==-1?4:3);tag(262,3,0);
 tag(266,3,1);tag(273,4,uint32_t(header));
 tag(277,3,1);tag(278,4,height);tag(279,4,uint32_t(length));
 // Group 3 uses T4Options (292), while Group 4 uses T6Options
 // (293). EncodedByteAlign applies only to Group 3 fax data.
 tag(k==-1?293:292,4,k==-1?0u:\n     ((k>0?1u:0u)|(byte_align?4u:0u)));
 put32(tiff,10+tags*12,0);
 std::memcpy(tiff.data()+header,data,length);
 Memory memory{tiff.data(),tiff.size()};
 TIFF* decoder=TIFFClientOpen("rinpdf-ccitt","rm",&memory,
    read_bytes,write_bytes,seek_bytes,close_bytes,size_bytes,
    map_bytes,unmap_bytes);
 if(!decoder)return false;
 const size_t pitch=(size_t(width)+7)/8;
 if(TIFFScanlineSize(decoder)<0||
    size_t(TIFFScanlineSize(decoder))<pitch){
  TIFFClose(decoder);return false;
 }
 std::vector<uint8_t> scan(size_t(TIFFScanlineSize(decoder)));
 std::vector<uint8_t> out(size_t(width)*height);
 bool ok=true;
 for(unsigned y=0;y<height;++y){
  if(TIFFReadScanline(decoder,scan.data(),y,0)<0){ok=false;break;}
  for(unsigned x=0;x<width;++x){
   const bool bit=(scan[x/8]>>(7-(x%8)))&1;
   // LibTIFF exposes MINISWHITE bits (0 white, 1 black).
   // PDF BlackIs1 determines which input sample value denotes black.
   const bool black=black_is_one?bit:!bit;
   out[size_t(y)*width+x]=black?0:255;
  }
 }
 TIFFClose(decoder);
 if(!ok)return false;
 grayscale=std::move(out);return true;
#else
 (void)byte_align;(void)black_is_one;
 return false;
#endif
}
}
