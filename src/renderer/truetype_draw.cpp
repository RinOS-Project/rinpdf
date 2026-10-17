#include "truetype_draw.h"
#include "sfnt.h"
#include "sfnt_gsub.h"
#include "glyph_outline.h"
#include "path_mask.h"
#include "../parser/font_encoding.h"
#include <cmath>
#include <limits>
#include <utility>
namespace rinpdf::renderer {
bool render_embedded_truetype_runs(const std::vector<uint8_t>& bytes,
 const std::vector<TextRun>& runs,double em,double start_x,double baseline,
 unsigned width,unsigned height,uint8_t* rgba,size_t stride,
 double x0,double y1,double sx,double sy,
 uint8_t red,uint8_t green,uint8_t blue,double& next_x,
 const std::vector<uint8_t>* clip_mask,
 unsigned clip_x0,unsigned clip_y0,unsigned clip_x1,unsigned clip_y1,
 GlyphTransform matrix,
 const std::map<uint8_t,uint32_t>* differences,
 const std::string& encoding,
 const parser::CIDGlyphMap* cid_map,
 const parser::CIDWidths* cid_widths,
 const parser::CIDVerticalMetrics* vertical_metrics,
 double* next_y,
 const std::string& pdf_language){
 if(!rgba||!width||!height||width>512||height>512||
    stride<size_t(width)*4||!std::isfinite(em)||em<=0||em>1000||
    !std::isfinite(start_x)||!std::isfinite(baseline)||
    !std::isfinite(sx)||!std::isfinite(sy)||sx<=0||sy<=0||
    runs.size()>4096||
    (clip_mask&&clip_mask->size()!=size_t(width)*height))return false;
 for(double v:{matrix.a,matrix.b,matrix.c,matrix.d,matrix.e,matrix.f,
               matrix.ta,matrix.tb,matrix.tc,matrix.td,
               matrix.character_spacing,matrix.word_spacing,matrix.horizontal_scale})
  if(!std::isfinite(v)||std::abs(v)>100000)return false;
 if(matrix.horizontal_scale<0||matrix.horizontal_scale>10||
    std::abs(matrix.character_spacing)>10000||
    std::abs(matrix.word_spacing)>10000)return false;
 if(clip_x1==0&&clip_y1==0){clip_x1=width;clip_y1=height;}
 if(clip_x0>clip_x1||clip_y0>clip_y1||clip_x1>width||clip_y1>height)return false;
 const bool vertical=encoding=="Identity-V";
 if(vertical&&(!cid_map||!cid_widths||!vertical_metrics||!next_y))return false;
 SfntFont sfnt;
 if(!parse_sfnt(bytes.data(),bytes.size(),sfnt))return false;
 struct Paint {std::vector<std::vector<MaskPoint>> paths;};
 std::vector<Paint> paints;
 size_t glyph_count=0;
 double cursor=vertical?0:start_x;
 uint16_t units=0,advance=0;
 for(const auto& run:runs){
  if(run.adjustment){
   if(!std::isfinite(run.displacement)||std::abs(run.displacement)>1000000)return false;
   cursor-=run.displacement*em*(vertical?1:matrix.horizontal_scale)/1000;
  }else{
   if(cid_map&&(run.bytes.size()%2!=0))return false;
   for(size_t offset=0;offset<run.bytes.size();offset+=cid_map?2:1){
    const unsigned char ch=static_cast<unsigned char>(run.bytes[offset]);
    if(++glyph_count>4096)return false;
    uint16_t glyph_id=0;
    uint16_t cid=0;
    if(cid_map){
     cid=uint16_t((uint16_t(ch)<<8)|
                           uint8_t(run.bytes[offset+1]));
     if(!parser::cid_to_gid(*cid_map,cid,glyph_id)||
        glyph_id>=sfnt.glyph_count)return false;
    }else{
     uint32_t unicode=0;
     static const std::map<uint8_t,uint32_t> no_overrides;
     if(!parser::font_byte_unicode(encoding,ch,
        differences?*differences:no_overrides,unicode)||
        !sfnt_glyph_id(sfnt,unicode,glyph_id))return false;
    }
    if(vertical){
     uint32_t script=0,language=0;
     if(pdf_language=="ja"||pdf_language.rfind("ja-",0)==0){
      script=0x68616e69;language=0x4a414e20; // hani / JAN
     }else if(pdf_language=="ko"||pdf_language.rfind("ko-",0)==0){
      script=0x68616e67;language=0x4b4f5220; // hang / KOR
     }
     if(!sfnt_vertical_substitution(sfnt,glyph_id,glyph_id,
          script,language))return false;
    }
    if(!sfnt_horizontal_metrics(sfnt,glyph_id,units,advance))return false;
    SimpleGlyph glyph;
    if(!sfnt_glyph_outline(sfnt,glyph_id,glyph))return false;
    parser::CIDVerticalMetric vertical_metric;
    if(vertical&&!parser::cid_vertical_metric(*vertical_metrics,
       *cid_widths,cid,vertical_metric))return false;
    const double origin_x=vertical?
       start_x-vertical_metric.v1x*em/1000:cursor;
    const double origin_y=vertical?
       baseline+cursor-vertical_metric.v1y*em/1000:baseline;
    Paint paint;
    // Build in font-coordinate units, then apply the text basis and CTM
    // before mapping PDF user coordinates to device pixels.
    if(!flatten_simple_glyph(glyph,em/units,0,0,0.15,paint.paths))
     return false;
    for(auto& contour:paint.paths)for(auto& point:contour){
     const double local_x=point.x*matrix.horizontal_scale,local_y=-point.y;
     const double ux=origin_x+matrix.ta*local_x+matrix.tc*local_y;
     const double uy=origin_y+matrix.tb*local_x+matrix.td*local_y;
     const double px=matrix.a*ux+matrix.c*uy+matrix.e;
     const double py=matrix.b*ux+matrix.d*uy+matrix.f;
     const double device_x=(px-x0)*sx,device_y=(y1-py)*sy;
     if(!std::isfinite(device_x)||!std::isfinite(device_y)||
        std::abs(device_x)>10000000||std::abs(device_y)>10000000)
      return false;
     point={device_x,device_y};
    }
    paints.push_back(std::move(paint));
    double glyph_advance=double(advance)*em/units;
    if(cid_map&&cid_widths){
     double width1000=0;
     if(!parser::cid_width(*cid_widths,cid,width1000))return false;
     glyph_advance=width1000*em/1000;
    }
    cursor+=vertical?(vertical_metric.w1y*em/1000-matrix.character_spacing):
            (glyph_advance+matrix.character_spacing+
             (!cid_map&&ch==32?matrix.word_spacing:0))*matrix.horizontal_scale;
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
  if(x<clip_x0||x>=clip_x1||y<clip_y0||y>=clip_y1)continue;
  if(clip_mask)alpha=(alpha*(*clip_mask)[size_t(y)*width+x]+127)/255;
  if(!alpha)continue;
  auto* pixel=rgba+size_t(y)*stride+size_t(x)*4;
  pixel[0]=uint8_t((alpha*red+(255-alpha)*pixel[0]+127)/255);
  pixel[1]=uint8_t((alpha*green+(255-alpha)*pixel[1]+127)/255);
  pixel[2]=uint8_t((alpha*blue+(255-alpha)*pixel[2]+127)/255);
  pixel[3]=255;
 }
 if(vertical){
  const double y=baseline+cursor;
  if(!std::isfinite(y)||std::abs(y)>10000000)return false;
  *next_y=y;
  next_x=start_x;
 }else next_x=cursor;
 return true;
}
}
