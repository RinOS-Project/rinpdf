#include "bitmap_glyph.h"
#include <cmath>
#include <algorithm>
namespace rinpdf::renderer {
static const uint8_t* glyph(uint8_t c){
 static constexpr uint8_t H[7]={17,17,17,31,17,17,17};
 static constexpr uint8_t I[7]={31,4,4,4,4,4,31};
 static constexpr uint8_t bang[7]={4,4,4,4,4,0,4};
 static constexpr uint8_t A[7]={14,17,17,31,17,17,17};
 static constexpr uint8_t space[7]={0,0,0,0,0,0,0};
 switch(c){case 'H':return H;case 'I':return I;case '!':return bang;
 case 'A':return A;case ' ':return space;default:return nullptr;}
}
bool diagnostic_glyph_supported(uint8_t c){return glyph(c)!=nullptr;}
bool draw_diagnostic_glyph(uint8_t ascii,double x,double baseline,
 double em,unsigned width,unsigned height,uint8_t* rgba,size_t stride,
 double x0,double y1,double sx,double sy,uint8_t red,uint8_t green,uint8_t blue){
 const auto* rows=glyph(ascii);
 if(!rows||!rgba||!width||!height||stride<size_t(width)*4||
    !std::isfinite(x)||!std::isfinite(baseline)||!std::isfinite(em)||
    em<=0||em>1000||!std::isfinite(sx)||!std::isfinite(sy)||
    sx<=0||sy<=0)return false;
 // Render within 0.6em advance, with ascenders 0.7em above baseline.
 const double cell_w=em*0.1,cell_h=em*0.1;
 for(int row=0;row<7;++row)for(int col=0;col<5;++col){
  if(!(rows[row]&(1u<<(4-col))))continue;
  const double xa=(x+col*cell_w-x0)*sx;
  const double xb=(x+(col+1)*cell_w-x0)*sx;
  const double ya=(y1-(baseline+(7-row)*cell_h))*sy;
  const double yb=(y1-(baseline+(6-row)*cell_h))*sy;
  if(!std::isfinite(xa)||!std::isfinite(xb)||!std::isfinite(ya)||!std::isfinite(yb)||
     xa<-1e8||xa>1e8||xb<-1e8||xb>1e8||
     ya<-1e8||ya>1e8||yb<-1e8||yb>1e8)return false;
  int left=std::max(0,int(std::floor(xa)));
  int right=std::min(int(width),int(std::ceil(xb)));
  int top=std::max(0,int(std::floor(ya)));
  int bottom=std::min(int(height),int(std::ceil(yb)));
  for(int py=top;py<bottom;++py)for(int px=left;px<right;++px){
   auto* pixel=rgba+size_t(py)*stride+size_t(px)*4;
   pixel[0]=red;pixel[1]=green;pixel[2]=blue;pixel[3]=255;
  }
 }
 return true;
}
}
