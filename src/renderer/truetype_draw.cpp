#include "truetype_draw.h"
#include "sfnt.h"
#include "glyph_outline.h"
#include "path_mask.h"
#include <cmath>
#include <limits>
#include <utility>
namespace rinpdf::renderer {
bool render_embedded_truetype_runs(const std::vector<uint8_t>& bytes,
 const std::vector<TextRun>& runs,double em,double start_x,double baseline,
 unsigned width,unsigned height,uint8_t* rgba,size_t stride,
 double x0,double y1,double sx,double sy,
 uint8_t red,uint8_t green,uint8_t blue,double& next_x){
 if(!rgba||!width||!height||width>512||height>512||
    stride<size_t(width)*4||!std::isfinite(em)||em<=0||em>1000||
    !std::isfinite(start_x)||!std::isfinite(baseline)||
    !std::isfinite(sx)||!std::isfinite(sy)||sx<=0||sy<=0||
    runs.size()>4096)return false;
 SfntFont sfnt;
 if(!parse_sfnt(bytes.data(),bytes.size(),sfnt))return false;
 struct Paint {std::vector<std::vector<MaskPoint>> paths;};
 std::vector<Paint> paints;
 size_t glyph_count=0;
 double cursor=start_x;
 uint16_t units=0,advance=0;
 for(const auto& run:runs){
  if(run.adjustment){
   if(!std::isfinite(run.displacement)||std::abs(run.displacement)>1000000)return false;
   cursor-=run.displacement*em/1000;
  }else{
   for(unsigned char ch:run.bytes){
    if(ch<32||ch>126||++glyph_count>4096)return false;
    uint16_t glyph_id=0;
    if(!sfnt_glyph_id(sfnt,ch,glyph_id)||
       !sfnt_horizontal_metrics(sfnt,glyph_id,units,advance))return false;
    SimpleGlyph glyph;
    if(!sfnt_glyph_outline(sfnt,glyph_id,glyph))return false;
    Paint paint;
    if(!flatten_simple_glyph(glyph,em*sx/units,
          (cursor-x0)*sx,(y1-baseline)*sy,0.2,paint.paths))return false;
    // The outline helper uses a uniform scale. Asymmetric output scales
    // are unsupported until arbitrary device transforms are introduced.
    if(std::abs(sx-sy)>1e-8)return false;
    paints.push_back(std::move(paint));
    cursor+=double(advance)*em/units;
    if(!std::isfinite(cursor)||std::abs(cursor)>10000000)return false;
   }
  }
 }
 // Every glyph is validated before writing output pixels.
 std::vector<uint8_t> layer(size_t(width)*height,0);
 for(const auto& paint:paints){
  if(paint.paths.empty())continue;
  std::vector<uint8_t> coverage;
  if(!rasterize_path_coverage_2x(paint.paths,width,height,
                                FillRule::NonZero,coverage))return false;
  for(size_t i=0;i<coverage.size();++i){
   const unsigned src=coverage[i],dst=layer[i];
   layer[i]=uint8_t(src+(dst*(255-src)+127)/255);
  }
 }
 for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
  unsigned alpha=layer[size_t(y)*width+x];
  if(!alpha)continue;
  auto* pixel=rgba+size_t(y)*stride+size_t(x)*4;
  pixel[0]=uint8_t((alpha*red+(255-alpha)*pixel[0]+127)/255);
  pixel[1]=uint8_t((alpha*green+(255-alpha)*pixel[1]+127)/255);
  pixel[2]=uint8_t((alpha*blue+(255-alpha)*pixel[2]+127)/255);
  pixel[3]=255;
 }
 next_x=cursor;
 return true;
}
}
