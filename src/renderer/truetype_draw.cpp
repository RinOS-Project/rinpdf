#include "truetype_draw.h"
#include "sfnt.h"
#include "sfnt_gsub.h"
#include "glyph_outline.h"
#include "path_mask.h"
#include "../parser/font_encoding.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
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
 // A text run frequently repeats glyph IDs. Cache validated outlines and
 // metrics within this call; the font bytes and sfnt table view remain live.
 struct CachedGlyph {
  uint16_t units=0,advance=0;
  SimpleGlyph outline;
  std::vector<std::vector<MaskPoint>> flattened;
  bool has_flattened=false;
  double flattened_em=0;
 };
 // Per-thread last-font cache: exact byte comparison prevents collisions and
 // owns the font bytes independently of PDF document and resource lifetime.
 // Only small fonts are retained; large fonts use a transient per-call map.
 struct LastFont {
  std::vector<uint8_t> bytes;
  std::map<uint16_t,CachedGlyph> glyphs;
  size_t estimated_bytes=0;
  std::map<uint16_t,uint64_t> recency;
  uint64_t clock=0;
 };
 thread_local LastFont last_font;
 std::map<uint16_t,CachedGlyph> transient_glyphs;
 const bool retain=bytes.size()<=2*1024*1024;
 if(retain && last_font.bytes!=bytes){
  // Construct the replacement first: allocation failures do not damage
  // an existing cached font.
  std::vector<uint8_t> replacement(bytes);
  last_font.bytes.swap(replacement);
  last_font.glyphs.clear();
  last_font.estimated_bytes=0;
  last_font.recency.clear();last_font.clock=0;
 }
 auto& glyph_cache=retain?last_font.glyphs:transient_glyphs;
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
     }else if(pdf_language=="zh-CN"||pdf_language=="zh-SG"||
              pdf_language.rfind("zh-Hans",0)==0){
      script=0x68616e69;language=0x5a485320; // hani / ZHS
     }else if(pdf_language=="zh-TW"||pdf_language=="zh-HK"||
              pdf_language=="zh-MO"||
              pdf_language.rfind("zh-Hant",0)==0){
      script=0x68616e69;language=0x5a485420; // hani / ZHT
     }
     if(!sfnt_vertical_substitution(sfnt,glyph_id,glyph_id,
          script,language))return false;
    }
    auto cached=glyph_cache.find(glyph_id);
    if(cached==glyph_cache.end()){
     CachedGlyph decoded;
     if(!sfnt_horizontal_metrics(sfnt,glyph_id,decoded.units,decoded.advance)||
        !sfnt_glyph_outline(sfnt,glyph_id,decoded.outline))return false;
     // Keep transient decoding for large fonts. Bound retained glyph
     // outlines by count and estimated allocated payload.
     const size_t estimated=sizeof(CachedGlyph)+
      decoded.outline.points.size()*sizeof(GlyphPoint)+
      decoded.outline.contour_ends.size()*sizeof(uint16_t)+128;
     if(retain&&estimated>4*1024*1024){
      // A single oversized outline must never occupy the retained font
      // cache. Keep it only for this render call.
      cached=transient_glyphs.emplace(glyph_id,std::move(decoded)).first;
     }else{
     if(retain){
      // Evict older entries before insertion. This glyph is not yet held
      // by any reference, and the currently active glyph is never evicted.
      while(!last_font.glyphs.empty()&&
            (last_font.glyphs.size()>=512||
             last_font.estimated_bytes>4*1024*1024-
                std::min(estimated,size_t(4*1024*1024)))){
       auto oldest=std::min_element(last_font.recency.begin(),
        last_font.recency.end(),[](const auto& x,const auto& y){
         return x.second<y.second;
        });
       if(oldest==last_font.recency.end())break;
       auto victim=last_font.glyphs.find(oldest->first);
       if(victim!=last_font.glyphs.end()){
        size_t weight=sizeof(CachedGlyph)+128+
         victim->second.outline.points.size()*sizeof(GlyphPoint)+
         victim->second.outline.contour_ends.size()*sizeof(uint16_t);
        for(const auto& contour:victim->second.flattened)
         weight+=sizeof(contour)+contour.size()*sizeof(MaskPoint);
        last_font.estimated_bytes-=std::min(last_font.estimated_bytes,weight);
        last_font.glyphs.erase(victim);
       }
       last_font.recency.erase(oldest);
      }
     }
     cached=glyph_cache.emplace(glyph_id,std::move(decoded)).first;
     if(retain){
      last_font.estimated_bytes+=estimated;
      last_font.recency[glyph_id]=++last_font.clock;
     }
     }
    }
    const bool persisted=retain&&
       last_font.glyphs.find(glyph_id)!=last_font.glyphs.end();
    if(persisted)last_font.recency[glyph_id]=++last_font.clock;
    units=cached->second.units;advance=cached->second.advance;
    const SimpleGlyph& glyph=cached->second.outline;
    parser::CIDVerticalMetric vertical_metric;
    if(vertical&&!parser::cid_vertical_metric(*vertical_metrics,
       *cid_widths,cid,vertical_metric))return false;
    const double origin_x=vertical?
       start_x-vertical_metric.v1x*em/1000:cursor;
    const double origin_y=vertical?
       baseline+cursor-vertical_metric.v1y*em/1000:baseline;
    Paint paint;
    // The font-space flattening tolerance and size are constant across
    // this call. Copy cached contours before applying per-glyph transforms.
    // Flattening depends on em, unlike the decoded sfnt glyph outline.
    if(!cached->second.has_flattened || cached->second.flattened_em!=em){
     std::vector<std::vector<MaskPoint>> replacement;
     if(!flatten_simple_glyph(glyph,em/units,0,0,0.15,replacement))
      return false;
     auto path_weight=[](const std::vector<std::vector<MaskPoint>>& paths){
      size_t n=0;
      for(const auto& path:paths)
       n+=sizeof(path)+path.size()*sizeof(MaskPoint);
      return n;
     };
     if(persisted){
      const size_t prior=path_weight(cached->second.flattened);
      const size_t next=path_weight(replacement);
      // Keep the currently used glyph alive while evicting older glyphs.
      while(last_font.glyphs.size()>1 &&
            next>prior &&
            last_font.estimated_bytes>4*1024*1024-
                std::min(next-prior,size_t(4*1024*1024))){
       auto oldest=last_font.recency.end();
       for(auto it=last_font.recency.begin();it!=last_font.recency.end();++it)
        if(it->first!=glyph_id &&
           (oldest==last_font.recency.end()||it->second<oldest->second))
         oldest=it;
       if(oldest==last_font.recency.end())break;
       auto victim=last_font.glyphs.find(oldest->first);
       if(victim!=last_font.glyphs.end()){
        size_t weight=sizeof(CachedGlyph)+128+
         victim->second.outline.points.size()*sizeof(GlyphPoint)+
         victim->second.outline.contour_ends.size()*sizeof(uint16_t);
        weight+=path_weight(victim->second.flattened);
        last_font.estimated_bytes-=std::min(weight,last_font.estimated_bytes);
        last_font.glyphs.erase(victim);
       }
       last_font.recency.erase(oldest);
      }
      last_font.estimated_bytes-=std::min(prior,last_font.estimated_bytes);
      last_font.estimated_bytes+=next;
     }
     cached->second.flattened=std::move(replacement);
     cached->second.has_flattened=true;
     cached->second.flattened_em=em;
    }
    paint.paths=cached->second.flattened;
    if(persisted&&last_font.estimated_bytes>4*1024*1024){
     // Preserve the active paint in its owned copy, but do not retain an
     // oversized glyph between calls.
     const auto& stored=cached->second;
     size_t weight=sizeof(CachedGlyph)+128+
       stored.outline.points.size()*sizeof(GlyphPoint)+
       stored.outline.contour_ends.size()*sizeof(uint16_t);
     for(const auto& contour:stored.flattened)
      weight+=sizeof(contour)+contour.size()*sizeof(MaskPoint);
     last_font.estimated_bytes-=std::min(last_font.estimated_bytes,weight);
     last_font.glyphs.erase(glyph_id);
     last_font.recency.erase(glyph_id);
    }
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
 // All outlines are validated before painting. Build a conservative
 // union of their pixel bounds, clipped to the visible rectangle.
 double min_x=std::numeric_limits<double>::infinity(),min_y=min_x;
 double max_x=-min_x,max_y=-min_y;
 for(const auto& paint:paints)
  for(const auto& contour:paint.paths)
   for(const auto& point:contour){
    min_x=std::min(min_x,point.x);max_x=std::max(max_x,point.x);
    min_y=std::min(min_y,point.y);max_y=std::max(max_y,point.y);
   }
 if(min_x!=std::numeric_limits<double>::infinity()){
  const unsigned x0=unsigned(std::clamp(std::floor(min_x)-1.0,0.0,double(width)));
  const unsigned y0=unsigned(std::clamp(std::floor(min_y)-1.0,0.0,double(height)));
  const unsigned x1=unsigned(std::clamp(std::ceil(max_x)+1.0,0.0,double(width)));
  const unsigned y1=unsigned(std::clamp(std::ceil(max_y)+1.0,0.0,double(height)));
  const unsigned left=std::max(x0,std::min(width,clip_x0));
  const unsigned top=std::max(y0,std::min(height,clip_y0));
  const unsigned right=std::min(x1,std::min(width,clip_x1));
  const unsigned bottom=std::min(y1,std::min(height,clip_y1));
  if(left<right&&top<bottom){
   const unsigned layer_width=right-left,layer_height=bottom-top;
   std::vector<uint8_t> layer(size_t(layer_width)*layer_height,0);
   for(const auto& paint:paints){
    if(paint.paths.empty())continue;
    CoverageRegion coverage;
    if(!rasterize_path_coverage_region_2x(paint.paths,width,height,
                                          FillRule::NonZero,coverage))return false;
    const unsigned cx0=std::max(left,coverage.x);
    const unsigned cy0=std::max(top,coverage.y);
    const unsigned cx1=std::min(right,coverage.x+coverage.width);
    const unsigned cy1=std::min(bottom,coverage.y+coverage.height);
    for(unsigned y=cy0;y<cy1;++y)
     for(unsigned x=cx0;x<cx1;++x){
      const unsigned src=coverage.pixels[size_t(y-coverage.y)*coverage.width+x-coverage.x];
      const size_t i=size_t(y-top)*layer_width+x-left;
      const unsigned dst=layer[i];
      layer[i]=uint8_t(src+(dst*(255-src)+127)/255);
     }
   }
   for(unsigned y=top;y<bottom;++y)
    for(unsigned x=left;x<right;++x){
     unsigned alpha=layer[size_t(y-top)*layer_width+x-left];
     if(clip_mask)alpha=(alpha*(*clip_mask)[size_t(y)*width+x]+127)/255;
     if(!alpha)continue;
     auto* pixel=rgba+size_t(y)*stride+size_t(x)*4;
     pixel[0]=uint8_t((alpha*red+(255-alpha)*pixel[0]+127)/255);
     pixel[1]=uint8_t((alpha*green+(255-alpha)*pixel[1]+127)/255);
     pixel[2]=uint8_t((alpha*blue+(255-alpha)*pixel[2]+127)/255);
     pixel[3]=255;
    }
  }
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
