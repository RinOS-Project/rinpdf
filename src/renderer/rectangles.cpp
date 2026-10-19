#include "rectangles.h"
#include "premultiplied_surface.h"
#include "premultiplied_cmyk_surface.h"
#include "path_mask.h"
#include "cubic_arc.h"
#include "cubic_dash.h"
#include "text_show.h"
#include "bitmap_glyph.h"
#include "truetype_draw.h"
#include "../parser/lexer.h"
#include "../parser/value.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
namespace rinpdf::renderer {
static bool number(const std::string& s,double& v){
 if(s.empty()||s.size()>64)return false;
 auto r=std::from_chars(s.data(),s.data()+s.size(),v);
 return r.ec==std::errc()&&r.ptr==s.data()+s.size()&&std::isfinite(v)&&std::abs(v)<10000000.0;
}
static bool space(unsigned char c){return c==0||c==9||c==10||c==12||c==13||c==32;}
struct Rect {double x,y,w,h;};
bool render_rectangles_box_with_fonts(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride,PageBox box,const FontResources& fonts,
 const MarkedProperties& properties,const ImageResources& images,
 const FormResources& forms,bool clear_background,unsigned form_depth,
 const GraphicsStateResources& graphic_states,
 PremultipliedSurface* transparent_target){
 if(form_depth>8)return false;
 if(!std::isfinite(box.x0)||!std::isfinite(box.y0)||!std::isfinite(box.x1)||!std::isfinite(box.y1)||box.x1<=box.x0||box.y1<=box.y0)return false;
 const double sx=(double)width/(box.x1-box.x0),sy=(double)height/(box.y1-box.y0);
 if(!std::isfinite(sx)||!std::isfinite(sy))return false;
 if(!rgba||(!contents&&size)||!width||!height||width>16384||height>16384||stride<(size_t)width*4)return false;
 if(size>16*1024*1024)return false;
 if((size_t)width*height>16777216)return false;
 if(transparent_target&&
    (transparent_target->width!=width||transparent_target->height!=height||
     transparent_target->pixels.size()!=size_t(width)*height*4))return false;
 if(clear_background)for(unsigned y=0;y<height;++y)
  std::memset(rgba+(size_t)y*stride,255,(size_t)width*4);
 double red=0,green=0,blue=0;
 double fill_alpha=1,stroke_alpha=1;
 std::string blend_mode="Normal";
 std::vector<std::string> saved_blend;
 std::vector<std::pair<double,double>> saved_alpha;
 struct Color { double r,g,b,a,bx,cx,d,e,f,width,sr,sg,sb; int cap,join; double miter,flatness; std::vector<double> dash; double dash_phase; };
 double a=1,bx=0,cx=0,d=1,e=0,f=0;
 double line_width=1;
 int line_cap=0,line_join=0;
 double miter_limit=10;
 double flatness=1;
 unsigned compatibility_depth=0;
 std::vector<std::string> marked_languages;
 std::string active_marked_language;
 bool in_text=false;
 std::string selected_font;
 double selected_font_size=0;
 std::string pending_font;
 std::string pending_image;
 double text_x=0,text_y=0,text_leading=0;
 double line_x=0,line_y=0;
 double text_a=1,text_b=0,text_c=0,text_d=1;
 double char_spacing=0,word_spacing=0,horizontal_scale=1;
 std::string pending_text;bool has_pending_text=false;
 std::vector<double> dash_pattern;
 double dash_phase=0;
 bool reading_dash=false;
 bool dash_array_ready=false;
 std::vector<double> dash_values;
 double stroke_red=0,stroke_green=0,stroke_blue=0;
 std::vector<Color> saved;
 std::vector<double> stack;
 std::vector<Rect> path;
 std::vector<std::vector<Point>> polygons;
 // Single-cubic stroke fast path; all other paths retain the existing renderer.
 struct CubicStroke {Point p0,p1,p2,p3;size_t polygon,start,endpoint;};
 std::vector<CubicStroke> cubic_strokes;
 struct Matrix {double a,b,c,d;};
 std::vector<std::vector<Matrix>> polygon_matrices;
 std::vector<Matrix> rect_matrices;
 auto matrix=[&](){return Matrix{a,bx,cx,d};};
 struct Clip {unsigned x0,y0,x1,y1;};
 Clip clip{0,0,width,height};
 std::vector<Clip> clip_stack;
 std::shared_ptr<const std::vector<uint8_t>> clip_mask;
 std::vector<std::shared_ptr<const std::vector<uint8_t>>> clip_mask_stack;
 bool pending_rect_clip=false;
 bool pending_path_clip=false;
 FillRule pending_rule=FillRule::NonZero;
 auto transform=[&](double x,double y){return Point{a*x+cx*y+e,bx*x+d*y+f};};
 auto shapes_from_path=[&](){
  std::vector<std::vector<MaskPoint>> shapes;
  for(const Rect& rect:path){
   auto pixel=[&](double x,double y){return MaskPoint{(x-box.x0)*sx,(box.y1-y)*sy};};
   shapes.push_back({pixel(rect.x,rect.y),pixel(rect.x+rect.w,rect.y),pixel(rect.x+rect.w,rect.y+rect.h),pixel(rect.x,rect.y+rect.h)});
  }
  for(const auto& poly:polygons){std::vector<MaskPoint> shape;for(const Point& p:poly)shape.push_back({(p.x-box.x0)*sx,(box.y1-p.y)*sy});shapes.push_back(std::move(shape));}
  return shapes;
 };

 // Explicit opt-in diagnostic font only: does not pretend to be Courier.
 auto diagnostic_show=[&](const std::vector<TextRun>& runs)->bool{
  const auto it=fonts.find(selected_font);
  if(it==fonts.end()||it->second.base_font!="RinPDF-Diagnostic"||
     selected_font_size<=0||selected_font_size>1000||
     text_a!=1||text_b!=0||text_c!=0||text_d!=1||
     a!=1||bx!=0||cx!=0||d!=1||e!=0||f!=0||
     horizontal_scale!=1||char_spacing!=0||word_spacing!=0||
     clip_mask||clip.x0!=0||clip.y0!=0||clip.x1!=width||clip.y1!=height)
   return false;
  double x=text_x,y=text_y;
  size_t glyphs=0;
  for(const auto& run:runs){
   if(run.adjustment){
    if(!std::isfinite(run.displacement)||std::abs(run.displacement)>1000000)
     return false;
    x-=run.displacement*selected_font_size/1000;
   }else for(unsigned char ch:run.bytes){
    if(++glyphs>4096||!diagnostic_glyph_supported(ch))return false;
    x+=selected_font_size*0.6;
   }
   if(!std::isfinite(x)||std::abs(x)>10000000)return false;
  }
  // All glyphs validated before writing any pixels.
  double cursor=text_x;
  const auto red8=static_cast<uint8_t>(std::lround(red*255));
  const auto green8=static_cast<uint8_t>(std::lround(green*255));
  const auto blue8=static_cast<uint8_t>(std::lround(blue*255));
  for(const auto& run:runs){
   if(run.adjustment)cursor-=run.displacement*selected_font_size/1000;
   else for(unsigned char ch:run.bytes){
    if(!draw_diagnostic_glyph(ch,cursor,text_y,selected_font_size,
          width,height,rgba,stride,box.x0,box.y1,sx,sy,red8,green8,blue8))
     return false;
    cursor+=selected_font_size*0.6;
   }
  }
  text_x=x;
  return true;
 };
 // Restricted embedded TrueType rendering for explicitly WinAnsi encoded
 // simple fonts. Unknown mappings and transformed text fail closed.
 auto truetype_show=[&](const std::vector<TextRun>& runs)->bool{
  const auto it=fonts.find(selected_font);
  if(it==fonts.end()||
     !(it->second.subtype=="TrueType"&&
        (it->second.encoding=="WinAnsiEncoding"||
         it->second.encoding=="MacRomanEncoding"||
         it->second.encoding=="StandardEncoding")||
       it->second.subtype=="Type0"&&
       (it->second.encoding=="Identity-H"||it->second.encoding=="Identity-V"))||
     it->second.embedded_truetype.empty()||
     !pending_font.empty()||selected_font_size<=0||
     !std::isfinite(horizontal_scale)||!std::isfinite(char_spacing)||
     !std::isfinite(word_spacing))
   return false;
  double next=0,next_vertical=text_y;
  if(!render_embedded_truetype_runs(it->second.embedded_truetype,runs,
       selected_font_size,text_x,text_y,width,height,rgba,stride,
       box.x0,box.y1,sx,sy,
       static_cast<uint8_t>(std::lround(red*255)),
       static_cast<uint8_t>(std::lround(green*255)),
       static_cast<uint8_t>(std::lround(blue*255)),next,
       clip_mask.get(),clip.x0,clip.y0,clip.x1,clip.y1,
       {a,bx,cx,d,e,f,text_a,text_b,text_c,text_d,
        char_spacing,word_spacing,horizontal_scale},
       &it->second.differences,it->second.encoding,
       it->second.subtype=="Type0"?&it->second.cid_map:nullptr,
       it->second.subtype=="Type0"?&it->second.cid_widths:nullptr,
       it->second.encoding=="Identity-V"?&it->second.vertical_metrics:nullptr,
       &next_vertical,marked_languages.empty()?it->second.language:
         (active_marked_language.empty()?it->second.language:active_marked_language)))return false;
  const bool vertical=it->second.encoding=="Identity-V";
  const double advance=vertical?next_vertical-text_y:next-text_x;
  const double new_x=text_x+advance*(vertical?text_c:text_a);
  const double new_y=text_y+advance*(vertical?text_d:text_b);
  if(!std::isfinite(new_x)||!std::isfinite(new_y)||
     std::abs(new_x)>10000000||std::abs(new_y)>10000000)return false;
  text_x=new_x;text_y=new_y;return true;
 };
 // Apply nonstroking opacity to both supported font renderers.
 // Rendering over a snapshot preserves glyph coverage and existing antialiasing.
 // Blend the opaque source and destination before source-over opacity.
 auto blend_channel=[&](unsigned src,unsigned dst)->unsigned{
  if(blend_mode=="Multiply")return (src*dst+127)/255;
  if(blend_mode=="Screen")return 255-((255-src)*(255-dst)+127)/255;
  return src;
 };
 auto transparent_blend_mode=[&]()->unsigned{
  return blend_mode=="Multiply"?1u:blend_mode=="Screen"?2u:0u;
 };
 auto paint_text=[&](auto&& draw)->bool{
  if(blend_mode!="Normal")return false;
  if(transparent_target){
   // Derive coverage from one draw on a contrasting opaque background.
   const unsigned color[3]={unsigned(std::lround(red*255)),
      unsigned(std::lround(green*255)),unsigned(std::lround(blue*255))};
   unsigned chosen=0,contrast=0,bg=255;
   for(unsigned c=0;c<3;++c){
    const unsigned white=255-color[c],black=color[c];
    if(std::max(white,black)>contrast){
     contrast=std::max(white,black);chosen=c;bg=white>=black?255u:0u;
    }
   }
   if(!contrast)return false;
   const size_t pitch=size_t(width)*4;
   std::vector<uint8_t> scratch(pitch*height,uint8_t(bg));
   for(size_t i=3;i<scratch.size();i+=4)scratch[i]=255;
   uint8_t* destination=rgba;
   const size_t original_stride=stride;
   rgba=scratch.data();stride=pitch;
   const bool ok=draw();
   rgba=destination;stride=original_stride;
   if(!ok)return false;
   const unsigned opacity=unsigned(std::lround(fill_alpha*255));
   for(unsigned y=0;y<height;++y)
    for(unsigned x=0;x<width;++x){
     const size_t i=(size_t(y)*width+x)*4;
     const unsigned observed=scratch[i+chosen];
     const unsigned delta=observed>bg?observed-bg:bg-observed;
     const unsigned coverage=std::min(255u,(delta*255u+contrast/2)/contrast);
     const unsigned alpha=(coverage*opacity+127)/255;
     if(coverage&&!transparent_target->mark_shape(x,y,uint8_t(coverage)))return false;
     // Even zero-opacity glyphs knock out earlier objects by shape.
     if(coverage&&(alpha||transparent_target->knockout)&&
        !transparent_target->fill_pixel(x,y,uint8_t(color[0]),
                   uint8_t(color[1]),uint8_t(color[2]),uint8_t(alpha),
                   uint8_t(coverage)))return false;
    }
   return true;
  }
  if(fill_alpha==1)return draw();
  const size_t row_bytes=size_t(width)*4;
  std::vector<uint8_t> original(row_bytes*height);
  for(unsigned y=0;y<height;++y)
   std::memcpy(original.data()+size_t(y)*row_bytes,
               rgba+size_t(y)*stride,row_bytes);
  if(!draw())return false;
  const unsigned opacity=unsigned(std::lround(fill_alpha*255));
  for(unsigned y=0;y<height;++y){
   uint8_t* pixel=rgba+size_t(y)*stride;
   const uint8_t* before=original.data()+size_t(y)*row_bytes;
   for(size_t x=0;x<row_bytes;x+=4){
    for(unsigned c=0;c<3;++c)
     pixel[x+c]=uint8_t((unsigned(pixel[x+c])*opacity+
                          unsigned(before[x+c])*(255-opacity)+127)/255);
    pixel[x+3]=before[x+3];
   }
  }
  return true;
 };
 // Share PDF tokenization rules with the object parser. Whitespace inside
 // literal strings, nested parentheses and hex-string bytes stay atomic.
 parser::Lexer lex(contents,size);
 while(true){
  const parser::Token token=lex.next();
  if(token.kind==parser::TokenKind::End)break;
  if(token.kind==parser::TokenKind::Invalid)return false;
  if(token.kind==parser::TokenKind::Name){
   // Marked content starts with a tag name followed by BMC or an
   // inline property dictionary followed by BDC.
   parser::Lexer look=lex;
   const parser::Token next=look.next();
   if(next.kind==parser::TokenKind::Keyword&&next.value=="BMC"){
    if(marked_languages.size()>=64||!stack.empty()||has_pending_text)return false;
    marked_languages.push_back(active_marked_language);
    lex=look;continue;
   }
   if(next.kind==parser::TokenKind::Name){
    parser::Lexer look_op=look;
    const auto op=look_op.next();
    if(op.kind==parser::TokenKind::Keyword&&op.value=="BDC"){
     if(marked_languages.size()>=64||!stack.empty()||has_pending_text)
      return false;
     const auto match=properties.find(next.value);
     if(match==properties.end())return false;
     marked_languages.push_back(active_marked_language);
     if(!match->second.language.empty())active_marked_language=match->second.language;
     lex=look_op;continue;
    }
   }
   if(next.kind==parser::TokenKind::DictOpen){
    parser::Value properties;
    parser::Lexer properties_lexer=lex;
    if(!parser::parse_value(properties_lexer,properties,8)||
       properties.kind!=parser::Value::Kind::Dictionary)return false;
    const auto op=properties_lexer.next();
    if(op.kind==parser::TokenKind::Keyword&&op.value=="BDC"){
     if(marked_languages.size()>=64||!stack.empty()||has_pending_text)return false;
     std::string lang;
     auto it=properties.dictionary.find("Lang");
     if(it!=properties.dictionary.end()){
      if(it->second.kind!=parser::Value::Kind::String||
         it->second.text.size()>64)return false;
      lang=it->second.text;
     }
     marked_languages.push_back(active_marked_language);
     if(!lang.empty())active_marked_language=lang;
     lex=properties_lexer;continue;
    }
   }
   if(!in_text&&next.kind==parser::TokenKind::Keyword&&
      next.value=="gs"&&stack.empty()){
    const auto state=graphic_states.find(token.value);
    if(state==graphic_states.end())return false;
    if(state->second.has_fill_alpha)
     fill_alpha=state->second.fill_alpha;
    if(state->second.has_stroke_alpha)
     stroke_alpha=state->second.stroke_alpha;
    if(state->second.has_blend_mode)
     blend_mode=state->second.blend_mode;
    lex=look;continue;
   }
   if(!in_text&&next.kind==parser::TokenKind::Keyword&&
      next.value=="Do"&&stack.empty()){
    pending_image=token.value;
    lex=look;
    // Dispatch image painting through the normal Do branch.
    const auto form=forms.find(pending_image);
    if(form!=forms.end()){
     if(form_depth>=8)return false;
     // A recursive call begins with the current page pixels and a fresh
     // graphics state. Do not attempt nested forms or inherited clipping.
     if(clip_mask||clip.x0||clip.y0||clip.x1!=width||clip.y1!=height||
        !path.empty()||!polygons.empty())return false;
     const auto& fm=form->second;
     auto number_text=[](double value){return std::to_string(value);};
     // Each recursive Form starts with fresh state, so pass its inherited
     // nonstroking/stroking opacity through an explicit internal gs entry.
     // Forms with their own resources must not see page-level gs names.
     GraphicsStateResources form_graphics=fm.own_resources?
         GraphicsStateResources{}:graphic_states;
     std::string inherited_name="RinPDFFormInheritedAlpha";
     while(form_graphics.count(inherited_name))inherited_name+="_";
     parser::PageGraphicsState inherited;
     inherited.fill_alpha=fill_alpha;
     inherited.stroke_alpha=stroke_alpha;
     inherited.has_fill_alpha=true;
     inherited.has_stroke_alpha=true;
     // Isolated groups blend against transparent black internally.
     // Their external blend mode applies only when composited to parent.
     inherited.blend_mode=fm.isolated_transparency_group?"Normal":blend_mode;
     inherited.has_blend_mode=true;
     form_graphics.emplace(inherited_name,inherited);
     std::string stream="q /"+inherited_name+" gs ";
     // Form XObjects inherit the caller's graphics state. The recursive
     // renderer starts fresh, so serialize supported state explicitly.
     for(double v:{red,green,blue})stream+=number_text(v)+" ";
     stream+="rg ";
     for(double v:{stroke_red,stroke_green,stroke_blue})
      stream+=number_text(v)+" ";
     stream+="RG ";
     stream+=number_text(line_width)+" w ";
     stream+=std::to_string(line_cap)+" J ";
     stream+=std::to_string(line_join)+" j ";
     stream+=number_text(miter_limit)+" M ";
     stream+="[ ";
     for(double v:dash_pattern)stream+=number_text(v)+" ";
     stream+="] "+number_text(dash_phase)+" d ";
     for(double v:{a,bx,cx,d,e,f})stream+=number_text(v)+" ";
     stream+="cm ";
     for(double v:fm.matrix)stream+=number_text(v)+" ";
     stream+="cm ";
     stream+=number_text(fm.bbox[0])+" "+number_text(fm.bbox[1])+" "+
             number_text(fm.bbox[2]-fm.bbox[0])+" "+
             number_text(fm.bbox[3]-fm.bbox[1])+" re W n ";
     if(stream.size()+fm.content.size()+3>4*1024*1024)return false;
     stream.append(reinterpret_cast<const char*>(fm.content.data()),
                   fm.content.size());
     stream+=" Q";
     pending_image.clear();
     ImageResources scoped_images;
     FormResources scoped_forms;
     FontResources scoped_fonts=fonts;
     if(fm.own_resources){
      // A Form's Font dictionary is local; it may shadow a page font.
      scoped_fonts.clear();
      for(const auto& [alias,local]:fm.local_fonts)
       scoped_fonts.emplace(alias,FontResource{
         local.base_font,local.subtype,local.encoding,
         local.embedded_truetype,local.differences,local.cid_map,
         local.cid_widths,local.vertical_metrics,local.language});
      // A Form's own resource dictionary replaces its parent's namespace.
      // Child Forms resolved from their indirect objects remain local.
      for(const auto& [alias,child]:fm.local_forms)
       scoped_forms.emplace(alias,child);
      for(const auto& [alias,image]:fm.local_images)
       scoped_images.emplace(alias,image);
      for(const auto& [alias,referenced]:fm.xobject_aliases){
       const auto image=images.find(referenced);
       const auto child=forms.find(referenced);
       if(image!=images.end())scoped_images.emplace(alias,image->second);
       else if(child!=forms.end())scoped_forms.emplace(alias,child->second);
       else return false;
      }
     }else{
      scoped_images=images;
      scoped_forms=forms;
     }
     auto draw_form=[&](uint8_t* target,size_t target_stride,
                        PremultipliedSurface* surface=nullptr)->bool{
      return render_rectangles_box_with_fonts(
         reinterpret_cast<const uint8_t*>(stream.data()),stream.size(),
         width,height,target,target_stride,box,scoped_fonts,properties,
         scoped_images,scoped_forms,false,form_depth+1,form_graphics,surface);
     };
     if(fm.knockout_transparency_group&&!fm.transparency_group)
      return false;
     if(transparent_target&&transparent_target->knockout_nonisolated&&
        !fm.transparency_group){
      // Treat a regular nested Form as one object against the original
      // backdrop, then knock it into the accumulated group once.
      if(blend_mode!="Normal"||
         transparent_target->knockout_backdrop.size()!=
             size_t(width)*height*4)return false;
      PremultipliedSurface object;
      if(!object.resize(width,height))return false;
      object.grayscale_group=transparent_target->grayscale_group;
      object.pixels=transparent_target->knockout_backdrop;
      const size_t pitch=size_t(width)*4;
      std::vector<uint8_t> scratch(pitch*height,0);
      if(!draw_form(scratch.data(),pitch,&object)||
         !transparent_target->composite_nonisolated_knockout_object(object))
       return false;
      continue;
     }
     if(transparent_target&&transparent_target->knockout&&
        !fm.transparency_group){
      // A normal nested Form contributes one painting object to a
      // knockout group. Render its internal paints normally first.
      PremultipliedSurface object;
      if(!object.resize(width,height))return false;
      object.grayscale_group=transparent_target->grayscale_group;
      const size_t rowbytes=size_t(width)*4;
      std::vector<uint8_t> scratch(rowbytes*height,0);
      // The Form is painted as one knockout object, but its own
      // internal blend operations are evaluated before parent knockout.
      // A parent's Multiply/Screen state cannot be applied by the
      // object-level knockout compositor yet.
      if(blend_mode!="Normal")return false;
      if(!draw_form(scratch.data(),rowbytes,&object)||
         !transparent_target->composite_knockout_object(object))return false;
      continue;
     }
     if(transparent_target&&transparent_target->knockout&&
        fm.transparency_group&&!fm.isolated_transparency_group){
      // Non-isolated nested groups are evaluated against the parent's
      // original knockout backdrop and committed as a single object.
      // This avoids exposing prior knockout objects to the child group.
      if(blend_mode!="Normal"||fm.knockout_transparency_group)
       return false;
      PremultipliedSurface object;
      if(!object.resize(width,height))return false;
      object.grayscale_group=transparent_target->grayscale_group;
      if(transparent_target->knockout_nonisolated){
       if(transparent_target->knockout_backdrop.size()!=size_t(width)*height*4)
        return false;
       object.pixels=transparent_target->knockout_backdrop;
      }
      const size_t pitch=size_t(width)*4;
      std::vector<uint8_t> scratch(pitch*height,0);
      if(!draw_form(scratch.data(),pitch,&object))return false;
      if(transparent_target->knockout_nonisolated){
       if(!transparent_target->composite_nonisolated_knockout_object(object))
        return false;
      }else{
       // Isolated parent: the non-isolated child inherits transparent black.
       if(!transparent_target->composite_knockout_object(object))return false;
      }
      continue;
     }
     if(fm.transparency_group&&!fm.isolated_transparency_group){
      // The current non-isolated backdrop reconciliation is RGB/Gray;
      // a CMYK inherited backdrop needs a separate four-channel model.
      if(fm.group_color_space=="DeviceCMYK")return false;
      // Non-isolated DeviceGray may reuse a grayscale parent backdrop.
      // A colored backdrop requires actual group color-space conversion.
      const bool group_gray=fm.group_color_space=="DeviceGray";
      if(group_gray&&!transparent_target)return false;
      if(fm.knockout_transparency_group){
       // Non-isolated knockout requires a transparent parent surface,
       // Normal group blending, and object-local graphics operations.
       // Complex nested Forms need their own backdrop management.
       if(!transparent_target||blend_mode!="Normal")return false;
       PremultipliedSurface backdrop,group;
       if(!backdrop.resize(width,height)||!group.resize(width,height)||
          !backdrop.copy_backdrop_from(*transparent_target)||
          !group.begin_nonisolated_knockout(*transparent_target))return false;
       if(group_gray&&!transparent_target->has_grayscale_backdrop())
        return false;
       group.grayscale_group=group_gray;
       const size_t rowbytes=size_t(width)*4;
       std::vector<uint8_t> scratch(rowbytes*height,0);
       if(!draw_form(scratch.data(),rowbytes,&group)||
          !group.composite_backdrop_difference_over(*transparent_target,
                                                    backdrop))
        return false;
       continue;
      }
      // Normal-only non-isolated groups use the parent's live backdrop.
      // Rendering onto that backdrop directly avoids applying paint twice,
      // including when the parent itself has premultiplied transparency.
      // More complex group blend/shape semantics need dedicated tracking.
      if(blend_mode!="Normal")return false;
      // Internal Multiply/Screen paint is evaluated against the inherited
      // live backdrop by the ordinary rasterizer. Do not reject a group
      // just because its resource dictionary contains those states.
      if(transparent_target){
       PremultipliedSurface backdrop,group;
       if(!backdrop.resize(width,height)||!group.resize(width,height)||
          !backdrop.copy_backdrop_from(*transparent_target))return false;
       if(group_gray){
        PremultipliedSurface projected;
        if(!projected.resize(width,height)||
           !projected.copy_gray_backdrop_from(*transparent_target)||
           !group.copy_gray_backdrop_from(*transparent_target))return false;
        if(!draw_form(rgba,stride,&group)||
           !group.composite_gray_difference_over(*transparent_target,
                                                 backdrop,projected))
         return false;
       }else{
        if(!group.copy_backdrop_from(*transparent_target))return false;
        if(!draw_form(rgba,stride,&group)||
           !group.composite_backdrop_difference_over(*transparent_target,
                                                      backdrop))
         return false;
       }
      }else if(!draw_form(rgba,stride))return false;
      continue;
     }
     if(transparent_target&&!fm.isolated_transparency_group){
      // A regular Form shares its caller's backdrop and graphics state.
      // Render directly into the same premultiplied surface.
      if(!draw_form(rgba,stride,transparent_target))return false;
      continue;
     }
     if(fm.isolated_transparency_group){
      // Render the isolated group exactly once with a transparent backdrop.
      // All supported primitives now use the premultiplied target.
      if(size_t(width)*height>1048576u)return false;
      // Multiply and Screen are handled by PremultipliedSurface within
      // the isolated group. The group's external blend remains Normal.
      PremultipliedSurface group;
      if(!group.resize(width,height))return false;
      group.knockout=fm.knockout_transparency_group;
      group.grayscale_group=fm.group_color_space=="DeviceGray";
      group.cmyk_group=fm.group_color_space=="DeviceCMYK";
      // Knockout object reconstruction currently assumes RGB components.
      if(group.cmyk_group&&group.knockout)return false;
      const size_t rowbytes=size_t(width)*4;
      std::vector<uint8_t> scratch(rowbytes*height,0);
      if(!draw_form(scratch.data(),rowbytes,&group))return false;
      if(group.cmyk_group){
       // Bridge the current RGB rasterizer into four-channel CMYK storage.
       // Once paint operators emit ink directly this conversion is removed.
       PremultipliedCMYKSurface ink_group;
       std::vector<uint8_t> converted;
       if(!ink_group.resize(width,height)||
          !ink_group.import_rgba(group.pixels.data(),rowbytes)||
          !ink_group.to_rgba(converted))return false;
       group.pixels=std::move(converted);
      }
      const unsigned external_mode=transparent_blend_mode();
      if(transparent_target){
       if(transparent_target->knockout_nonisolated){
        // The isolated child is a single painting object. Blend it over
        // the immutable group backdrop, not the earlier knockout objects.
        if(external_mode!=0||
           transparent_target->knockout_backdrop.size()!=rowbytes*height)
         return false;
        PremultipliedSurface object;
        if(!object.resize(width,height))return false;
      object.grayscale_group=transparent_target->grayscale_group;
        object.pixels=transparent_target->knockout_backdrop;
        if(!group.composite_over(object)||
           !transparent_target->composite_nonisolated_knockout_object(object))
         return false;
       }else if(transparent_target->knockout){
        if(external_mode!=0||
           !transparent_target->composite_knockout_object(group))return false;
       }else if(!group.composite_blend_over(*transparent_target,external_mode))
        return false;
      }else if(!group.composite_blend_over_opaque(rgba,stride,external_mode))
       return false;
     }else if(!draw_form(rgba,stride))return false;
     continue;
    }
    const auto found=images.find(pending_image);
    if(found==images.end())return false;
    const auto& im=found->second;
    pending_image.clear();
    if((im.channels!=1&&im.channels!=3)||!im.width||!im.height||
       im.pixels.size()!=size_t(im.width)*im.height*im.channels||
       (!im.alpha.empty()&&im.alpha.size()!=size_t(im.width)*im.height))
     return false;
    const double det=a*d-bx*cx;
    if(!std::isfinite(det)||std::abs(det)<1e-12)return false;
    const Point corners[]={transform(0,0),transform(1,0),
                           transform(0,1),transform(1,1)};
    double xmin=corners[0].x,xmax=xmin,ymin=corners[0].y,ymax=ymin;
    for(const auto& p:corners){
     if(!std::isfinite(p.x)||!std::isfinite(p.y))return false;
     xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);
     ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);
    }
    auto bound=[](double v,unsigned n)->unsigned{
     return v<=0?0:v>=n?n:unsigned(v);
    };
    const unsigned left=bound(std::floor((xmin-box.x0)*sx),width);
    const unsigned right=bound(std::ceil((xmax-box.x0)*sx),width);
    const unsigned top=bound(std::floor((box.y1-ymax)*sy),height);
    const unsigned bottom=bound(std::ceil((box.y1-ymin)*sy),height);
    for(unsigned py=top;py<bottom;++py)for(unsigned px=left;px<right;++px){
     if(px<clip.x0||px>=clip.x1||py<clip.y0||py>=clip.y1)continue;
     if(clip_mask&&(*clip_mask)[size_t(py)*width+px]==0)continue;
     const double ux=box.x0+(px+0.5)/sx-e;
     const double uy=box.y1-(py+0.5)/sy-f;
     const double u=(d*ux-cx*uy)/det;
     const double v=(-bx*ux+a*uy)/det;
     if(u<0||u>=1||v<0||v>=1)continue;
     const unsigned ix=std::min(im.width-1,unsigned(u*im.width));
     const unsigned iy=std::min(im.height-1,unsigned((1-v)*im.height));
     const size_t source=(size_t(iy)*im.width+ix)*im.channels;
     const size_t target=size_t(py)*stride+size_t(px)*4;
     const unsigned base_opacity=im.stencil?
       im.pixels[size_t(iy)*im.width+ix]:
       (im.alpha.empty()?255:im.alpha[size_t(iy)*im.width+ix]);
     const unsigned clip_coverage=clip_mask?
         (*clip_mask)[size_t(py)*width+px]:255u;
     const unsigned painted_opacity=(base_opacity*
         unsigned(std::lround(fill_alpha*255))+127)/255;
     const unsigned opacity=(painted_opacity*clip_coverage+127)/255;
     if(!opacity&&!(transparent_target&&transparent_target->knockout))continue;
     if(transparent_target){
      const auto component=[&](unsigned channel)->uint8_t{
       return uint8_t(im.stencil?
         unsigned(std::lround((channel==0?red:channel==1?green:blue)*255)):
         im.pixels[source+(im.channels==3?channel:0)]);
      };
      // SMask/constant alpha changes opacity, not the geometric shape
      // of a regular image. Image masks use their stencil coverage.
      const unsigned geometry=(im.stencil?base_opacity:255u)*
                              clip_coverage/255u;
      if(!transparent_target->mark_shape(px,py,uint8_t(geometry)))return false;
      if(!transparent_target->blend_pixel(
            px,py,component(0),component(1),component(2),uint8_t(opacity),
            transparent_blend_mode(),uint8_t(geometry)))
       return false;
      continue;
     }
     // Source-over on the renderer's opaque page buffer.
     for(unsigned channel=0;channel<3;++channel){
      const unsigned src=im.stencil?
         unsigned(std::lround((channel==0?red:channel==1?green:blue)*255)):
         im.pixels[source+(im.channels==3?channel:0)];
      const unsigned dst=rgba[target+channel];
      rgba[target+channel]=uint8_t(
        (blend_channel(src,dst)*opacity+dst*(255-opacity)+127)/255);
     }
     rgba[target+3]=255;
    }
    continue;
   }
   if(in_text&&pending_font.empty()&&stack.empty()){
    pending_font=token.value;
    continue;
   }
   if(compatibility_depth>0)continue;
   return false;
  }
  if(token.kind==parser::TokenKind::String||
     token.kind==parser::TokenKind::HexString){
   if(compatibility_depth>0)continue;
   // The double-quote operator has two numeric operands before its string.
   if(!in_text||has_pending_text||
      (!stack.empty()&&stack.size()!=2))return false;
   pending_text=token.value;has_pending_text=true;
   continue;
  }
  const std::string& word=token.value;
  if(word=="EMC"){
   if(marked_languages.empty()||!stack.empty()||has_pending_text)return false;
   active_marked_language=marked_languages.back();
   marked_languages.pop_back();continue;
  }
  if(!pending_font.empty()&&word!="Tf"&&token.kind!=parser::TokenKind::Integer&&token.kind!=parser::TokenKind::Keyword)return false;
  double v=0;
  if(number(word,v)){if(reading_dash){if(dash_values.size()>=32)return false;dash_values.push_back(v);}else{if(dash_array_ready&&stack.size()>=1)return false;if(stack.size()>=16)return false;stack.push_back(v);}continue;}
  if(word=="["&&in_text){
   if(has_pending_text||!stack.empty()||selected_font.empty())return false;
   std::vector<TextRun> runs;
   if(!parse_text_array(lex,runs))return false;
   const auto selected=fonts.find(selected_font);
   if(selected==fonts.end())return false;
   double advance=0;
   if(selected->second.base_font=="RinPDF-Diagnostic")
    {if(!paint_text([&](){return diagnostic_show(runs);}))return false;continue;}
   if(!selected->second.embedded_truetype.empty())
    {if(!paint_text([&](){return truetype_show(runs);}))return false;continue;}
   if(!courier_text_advance(selected->second.base_font,runs,selected_font_size,
                            char_spacing,word_spacing,horizontal_scale,advance)||
      !advance_text_origin(advance,text_a,text_b,text_x,text_y))return false;
   return false; // No glyphs painted yet.
  }
  if(word=="["){if(reading_dash||dash_array_ready||!stack.empty())return false;reading_dash=true;dash_values.clear();continue;}
  if(word=="]"){if(!reading_dash)return false;reading_dash=false;dash_array_ready=true;continue;}
  // Only numeric elements are legal inside a dash array.
  if(reading_dash)return false;
  if(dash_array_ready&&word!="d"&&!(number(word,v)))return false;
  if(word=="q"){
   if((pending_rect_clip||pending_path_clip)||!stack.empty()||saved.size()>=64)return false;
   saved.push_back({red,green,blue,a,bx,cx,d,e,f,line_width,stroke_red,stroke_green,stroke_blue,line_cap,line_join,miter_limit,flatness,dash_pattern,dash_phase});
   saved_alpha.emplace_back(fill_alpha,stroke_alpha);
   saved_blend.push_back(blend_mode);
   clip_stack.push_back(clip);
   clip_mask_stack.push_back(clip_mask);

  }else if(word=="Q"){
   if((pending_rect_clip||pending_path_clip)||!stack.empty()||saved.empty())return false;
   auto color=saved.back();saved.pop_back();
   fill_alpha=saved_alpha.back().first;
   stroke_alpha=saved_alpha.back().second;
   saved_alpha.pop_back();
   blend_mode=saved_blend.back();saved_blend.pop_back();
   red=color.r;green=color.g;blue=color.b;a=color.a;bx=color.bx;cx=color.cx;d=color.d;e=color.e;f=color.f;line_width=color.width;stroke_red=color.sr;stroke_green=color.sg;stroke_blue=color.sb;line_cap=color.cap;line_join=color.join;miter_limit=color.miter;flatness=color.flatness;dash_pattern=color.dash;dash_phase=color.dash_phase;
   clip=clip_stack.back();clip_stack.pop_back();
   clip_mask=std::move(clip_mask_stack.back());clip_mask_stack.pop_back();

  }else if(word=="cm"){
   if(pending_rect_clip||pending_path_clip)return false;
   if(stack.size()!=6)return false;
   const double A=stack[0],B=stack[1],C=stack[2],D=stack[3],E=stack[4],F=stack[5];
   const double na=a*A+cx*B,nb=bx*A+d*B,nc=a*C+cx*D,nd=bx*C+d*D;
   const double ne=a*E+cx*F+e,nf=bx*E+d*F+f;
   for(double value:{na,nb,nc,nd,ne,nf})if(!std::isfinite(value)||std::abs(value)>10000000)return false;
   a=na;bx=nb;cx=nc;d=nd;e=ne;f=nf;stack.clear();
  }else if(word=="d"){
   if(reading_dash||!dash_array_ready||stack.size()!=1||dash_values.size()>32)return false;
   double total=0;
   for(double value:dash_values){if(!std::isfinite(value)||value<0||value>1000000)return false;total+=value;}
   if(!std::isfinite(total)||total>1000000||(!dash_values.empty()&&total<=0)||stack[0]<0)return false;
   dash_pattern=dash_values;dash_phase=total>0?std::fmod(stack[0],total*(dash_values.size()%2?2:1)):0;
   dash_values.clear();dash_array_ready=false;stack.clear();
  }else if(word=="BT"){
   if(in_text||!stack.empty()||!path.empty()||!polygons.empty())return false;
   // BT resets the text and line matrices, not the persistent text-state
   // parameters (TL, Tc, Tw, Tz, or the selected font).
   in_text=true;text_x=text_y=0;
   line_x=line_y=0;
   pending_font.clear();
   text_a=text_d=1;text_b=text_c=0;
  }else if(word=="ET"){
   if(!in_text||!stack.empty())return false;
   in_text=false;
   pending_font.clear();
  }else if(word=="Tf"){
   if(!in_text||pending_font.empty()||stack.size()!=1||
      stack[0]<=0||stack[0]>10000)return false;
   const auto it=fonts.find(pending_font);
   if(it==fonts.end()||it->second.base_font.empty()||
      it->second.subtype.empty())return false;
   selected_font=pending_font;selected_font_size=stack[0];
   pending_font.clear();stack.clear();
  }else if(word=="Tc"||word=="Tw"||word=="Tz"){
   if(!in_text||stack.size()!=1)return false;
   if(word=="Tc")char_spacing=stack[0];
   else if(word=="Tw")word_spacing=stack[0];
   else{
    if(stack[0]<0||stack[0]>1000)return false;
    horizontal_scale=stack[0]/100;
   }
   stack.clear();
  }else if(word=="Tj"){
   if(!in_text||!has_pending_text||selected_font.empty()||!stack.empty())
    return false;
   const auto selected=fonts.find(selected_font);
   if(selected==fonts.end())return false;
   double advance=0;
   if(selected->second.base_font=="RinPDF-Diagnostic"){
    if(!paint_text([&](){return diagnostic_show({{false,pending_text,0}});}))return false;
    pending_text.clear();has_pending_text=false;
    continue;
   }
   if(!selected->second.embedded_truetype.empty()){
    if(!paint_text([&](){return truetype_show({{false,pending_text,0}});}))return false;
    pending_text.clear();has_pending_text=false;
    continue;
   }
   if(!courier_text_advance(selected->second.base_font,
       std::vector<TextRun>{{false,pending_text,0}},selected_font_size,
       char_spacing,word_spacing,horizontal_scale,advance)||
      !advance_text_origin(advance,text_a,text_b,text_x,text_y))return false;
   return false; // Standard Courier outlines are not yet rendered.
  }else if(word=="TL"){
   if(!in_text||stack.size()!=1)return false;
   text_leading=stack[0];stack.clear();
  }else if(word=="Td"||word=="TD"){
   if(!in_text||stack.size()!=2)return false;
   const double dx=stack[0],dy=stack[1];
   const double nx=line_x+text_a*dx+text_c*dy;
   const double ny=line_y+text_b*dx+text_d*dy;
   if(!std::isfinite(nx)||!std::isfinite(ny)||
      std::abs(nx)>10000000||std::abs(ny)>10000000)return false;
   if(word=="TD")text_leading=-dy;
   line_x=text_x=nx;line_y=text_y=ny;
   stack.clear();
  }else if(word=="T*"){
   if(!in_text||!stack.empty())return false;
   const double nx=line_x-text_c*text_leading;
   const double ny=line_y-text_d*text_leading;
   if(!std::isfinite(nx)||!std::isfinite(ny)||
      std::abs(nx)>10000000||std::abs(ny)>10000000)return false;
   line_x=text_x=nx;line_y=text_y=ny;
  }else if(word=="Tm"){
   if(!in_text||stack.size()!=6)return false;
   text_a=stack[0];text_b=stack[1];text_c=stack[2];text_d=stack[3];
   text_x=line_x=stack[4];text_y=line_y=stack[5];stack.clear();
   }else if(word=="'"||(word.size()==1&&word[0]==34)){
   // Quote text-show operators are T* followed by Tj. Double quote
   // additionally sets Tw and Tc before moving to the next line.
   // Render embedded TrueType glyphs rather than merely accepting input.
   const bool double_quote=word!="'";
   if(!in_text||!has_pending_text||selected_font.empty()||
      (!double_quote&&!stack.empty())||
      (double_quote&&stack.size()!=2))return false;
   const auto it=fonts.find(selected_font);
   if(it==fonts.end()||it->second.embedded_truetype.empty())return false;
   const double nx=line_x-text_c*text_leading;
   const double ny=line_y-text_d*text_leading;
   if(!std::isfinite(nx)||!std::isfinite(ny)||
      std::abs(nx)>10000000||std::abs(ny)>10000000)return false;
   if(double_quote){
    word_spacing=stack[0];char_spacing=stack[1];
   }
   stack.clear();
   line_x=text_x=nx;line_y=text_y=ny;
   if(!paint_text([&](){return truetype_show({{false,pending_text,0}});}))return false;
   pending_text.clear();has_pending_text=false;
  }else if(word=="i"){
   // The graphics-state flatness parameter is device dependent.
   // Store and validate it, while retaining adaptive curve tessellation.
   if(stack.size()!=1||stack[0]<0||stack[0]>100)return false;
   flatness=stack[0];stack.clear();
  }else if(word=="BX"){
   if(!stack.empty()||compatibility_depth>=16)return false;
   ++compatibility_depth;
  }else if(word=="EX"){
   if(!stack.empty()||compatibility_depth==0)return false;
   --compatibility_depth;
  }else if(word=="w"){
   if(stack.size()!=1||stack[0]<0||stack[0]>1024)return false;
   line_width=stack[0];stack.clear();
  }else if(word=="J"){
   if(stack.size()!=1||stack[0]<0||stack[0]>2||std::floor(stack[0])!=stack[0])return false;
   line_cap=(int)stack[0];stack.clear();
  }else if(word=="j"){
   if(stack.size()!=1||stack[0]<0||stack[0]>2||std::floor(stack[0])!=stack[0])return false;
   line_join=(int)stack[0];stack.clear();
  }else if(word=="M"){
   if(stack.size()!=1||stack[0]<1||stack[0]>1000)return false;
   miter_limit=stack[0];stack.clear();
  }else if(word=="RG"){
   if(stack.size()!=3)return false;
   for(double channel:stack)if(channel<0||channel>1)return false;
   stroke_red=stack[0];stroke_green=stack[1];stroke_blue=stack[2];stack.clear();
  }else if(word=="G"){
   if(stack.size()!=1||stack[0]<0||stack[0]>1)return false;
   stroke_red=stroke_green=stroke_blue=stack[0];stack.clear();
  }else if(word=="K"){
   if(stack.size()!=4)return false;
   for(double channel:stack)if(channel<0||channel>1)return false;
   stroke_red=(1-stack[0])*(1-stack[3]);
   stroke_green=(1-stack[1])*(1-stack[3]);
   stroke_blue=(1-stack[2])*(1-stack[3]);stack.clear();
  }else if(word=="rg"){
   if(stack.size()!=3)return false;
   for(double channel:stack)if(channel<0||channel>1)return false;
   red=stack[0];green=stack[1];blue=stack[2];stack.clear();
  }else if(word=="k"){
   if(stack.size()!=4)return false;
   for(double channel:stack)if(channel<0||channel>1)return false;
   red=(1-stack[0])*(1-stack[3]);
   green=(1-stack[1])*(1-stack[3]);
   blue=(1-stack[2])*(1-stack[3]);
   stack.clear();
  }else if(word=="g"){
   if(stack.size()!=1||stack[0]<0||stack[0]>1)return false;
   red=green=blue=stack[0];stack.clear();
  }else if(word=="m"){
   if(pending_path_clip)return false;
   if(pending_rect_clip||pending_path_clip)return false;
   if(stack.size()!=2||polygons.size()>=4096)return false;
   const Point point=transform(stack[0],stack[1]);
   if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
   polygons.push_back({point});polygon_matrices.push_back({matrix()});stack.clear();
  }else if(word=="l"){
   if(pending_path_clip)return false;
   if(pending_rect_clip)return false;
   if(stack.size()!=2||polygons.empty()||polygons.back().size()>=4096)return false;
   const Point point=transform(stack[0],stack[1]);
   if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
   polygons.back().push_back(point);polygon_matrices.back().push_back(matrix());stack.clear();
  }else if(word=="c"||word=="v"||word=="y"){
   if(pending_path_clip||pending_rect_clip)return false;
   const size_t required=word=="c"?6:4;
   if(stack.size()!=required||polygons.empty()||polygons.back().empty()||polygons.back().size()>=4096)return false;
   auto& poly=polygons.back();
   Point start=poly.back();
   auto transform_curve=[&](double x,double y){return transform(x,y);};
   Point p1{},p2{},p3{};
   if(word=="c"){
    p1=transform_curve(stack[0],stack[1]);p2=transform_curve(stack[2],stack[3]);p3=transform_curve(stack[4],stack[5]);
   }else if(word=="v"){
    p1=start;p2=transform_curve(stack[0],stack[1]);p3=transform_curve(stack[2],stack[3]);
   }else{
    p1=transform_curve(stack[0],stack[1]);p2=transform_curve(stack[2],stack[3]);p3=p2;
   }
   if(!std::isfinite(p1.x)||!std::isfinite(p1.y)||!std::isfinite(p2.x)||!std::isfinite(p2.y)||!std::isfinite(p3.x)||!std::isfinite(p3.y))return false;
   const bool record_cubic=polygons.size()==1&&path.empty();
   const size_t curve_start=poly.size()-1;
   // Adapt the cubic tessellation to device-space curvature.
   // The maximum distance of both control points to the chord bounds
   // the flattening error; subdivide until the bound is small enough.
   // Arc-length helpers are reserved for stroke-time dash placement.
   // Avoid quadrature during path construction: the path may be filled,
   // and dash state can change before the stroke operator.
   struct Cubic {Point a,b,c,d;unsigned depth;double length_budget;};
   std::vector<Cubic> pending{{start,p1,p2,p3,0,0.01}};
   // PDF graphics-state operators can change dash settings after a
   // path is constructed and before it is stroked. Tessellation must
   // not depend on the dash state at the time of the 'c' operator.
   // Use a consistent geometry and arc-length tolerance for all cubics.
   constexpr double tolerance_pixels=0.0625;
   // Keep the subdivision stack bounded independently of the output
   // vertex limit, including adversarial curves that never flatten.
   constexpr size_t max_pending_cubics=4096;
   // The output vertex limit includes the vertices already in this
   // subpath, so fail before allocating a subdivision that cannot fit.
   if(poly.size()>=4096)return false;
   while(!pending.empty()){
    const Cubic seg=pending.back();pending.pop_back();
    const double dx=(seg.d.x-seg.a.x)*sx,dy=(seg.d.y-seg.a.y)*sy;
    const double bx=(seg.b.x-seg.a.x)*sx,by=(seg.b.y-seg.a.y)*sy;
    const double cx=(seg.c.x-seg.a.x)*sx,cy=(seg.c.y-seg.a.y)*sy;
    const double chord=std::hypot(dx,dy);
    // Use a unit chord normal rather than cross products followed by
    // division. This avoids unnecessary overflow for large but finite
    // device-space control vectors.
    const double deviation=chord>1e-12?
     std::max(std::abs(bx*(dy/chord)-by*(dx/chord)),
              std::abs(cx*(dy/chord)-cy*(dx/chord))):
     std::max(std::hypot(bx,by),std::hypot(cx,cy));
    if(!std::isfinite(deviation))return false;
    // Collinear control points can still overshoot the endpoint chord.
    // A flatness test based only on perpendicular distance would erase
    // that reversal and change both stroke coverage and dash length.
    double overshoot=0;
    if(chord>1e-12){
     // Use normalized chord projections: forming chord squared may
     // overflow even when the input points and their differences are
     // finite. The projected distances are in device pixels.
     const double ux=dx/chord,uy=dy/chord;
     const double pb=bx*ux+by*uy,pc=cx*ux+cy*uy;
     if(!std::isfinite(pb)||!std::isfinite(pc))return false;
     overshoot=std::max({0.0,-pb,pb-chord,-pc,pc-chord,pc-pb});
    }
    if(!std::isfinite(overshoot))return false;
    // For dashed curves, the control polygon provides a rigorous
    // upper bound on arc length and the chord a lower bound.
    // Subdivision continues until the entire gap fits its assigned
    // error budget, so no numerical integration guess is required.
    double length_error=0;
    {
     const double control_length=
      std::hypot(seg.b.x-seg.a.x,seg.b.y-seg.a.y)+
      std::hypot(seg.c.x-seg.b.x,seg.c.y-seg.b.y)+
      std::hypot(seg.d.x-seg.c.x,seg.d.y-seg.c.y);
     const double chord_length=std::hypot(seg.d.x-seg.a.x,seg.d.y-seg.a.y);
     if(!std::isfinite(control_length)||!std::isfinite(chord_length))return false;
     length_error=std::max(0.0,control_length-chord_length);
     // Finite arithmetic can round a positive control-polygon excess
     // down to zero. Retain the conservative bound where representable.
     if(!std::isfinite(length_error))return false;
    }
    const double flatness=std::max(deviation,overshoot);
    // Partition a total 0.01 user-unit length-error budget across
    // the subdivision tree. The sum of all leaf allowances is <=0.01,
    // independent of traversal order and without quadratic rescans.
    const bool length_accurate=length_error<=seg.length_budget;
    // Reaching the subdivision limit is not evidence of sufficient
    // geometric accuracy. Reject unresolved curves rather than
    // silently emitting a potentially visibly incorrect chord.
    if((flatness>tolerance_pixels||!length_accurate)&&seg.depth>=12)return false;
    if(flatness<=tolerance_pixels&&length_accurate){
     if(poly.size()>=4096)return false;
     poly.push_back(seg.d);polygon_matrices.back().push_back(matrix());
     continue;
    }
    auto midpoint=[](Point a,Point b){return Point{(a.x+b.x)*.5,(a.y+b.y)*.5};};
    const Point ab=midpoint(seg.a,seg.b),bc=midpoint(seg.b,seg.c),cd=midpoint(seg.c,seg.d);
    const Point abc=midpoint(ab,bc),bcd=midpoint(bc,cd),mid=midpoint(abc,bcd);
    for(const Point point:{ab,bc,cd,abc,bcd,mid})
     if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
    // A looping cubic can legitimately have its midpoint equal to
    // an endpoint while its control polygon still has nonzero extent.
    // Only reject subdivision when *both* children are geometrically
    // identical to the parent (no representable progress). The depth
    // limit independently bounds repeated degenerate subdivisions.
    const bool no_progress=
      ab.x==seg.a.x&&ab.y==seg.a.y&&
      abc.x==seg.a.x&&abc.y==seg.a.y&&
      mid.x==seg.a.x&&mid.y==seg.a.y&&
      bcd.x==seg.d.x&&bcd.y==seg.d.y&&
      cd.x==seg.d.x&&cd.y==seg.d.y;
    if(no_progress)return false;
    if(pending.size()>max_pending_cubics-2)return false;
    pending.push_back({mid,bcd,cd,seg.d,seg.depth+1,seg.length_budget*.5});
    pending.push_back({seg.a,ab,abc,mid,seg.depth+1,seg.length_budget*.5});
   }
   if(record_cubic)cubic_strokes.push_back({start,p1,p2,p3,0,curve_start,poly.size()-1});
   stack.clear();
  }else if(word=="h"){
   if(pending_path_clip)return false;
   if(pending_rect_clip)return false;
   if(!stack.empty()||polygons.empty())return false;
   // Closing a move-only subpath does not create a drawable segment.
   if(polygons.back().size()==1)continue;
   if(polygons.back().size()>=4096)return false;
   const Point first=polygons.back().front(),last=polygons.back().back();
   if(first.x!=last.x||first.y!=last.y){polygons.back().push_back(first);polygon_matrices.back().push_back(matrix());}
   // Preserve cubic control points for stroke-time closed contour dashes.
  }else if(word=="re"){
   if(pending_path_clip)return false;
   if(pending_rect_clip)return false;
   if(stack.size()!=4||path.size()>=4096||polygons.size()>=4096)return false;
   cubic_strokes.clear();
   {const Point p=transform(stack[0],stack[1]);const Point q=transform(stack[0]+stack[2],stack[1]+stack[3]);
   const Point p1=transform(stack[0]+stack[2],stack[1]),p3=transform(stack[0],stack[1]+stack[3]);
   for(const Point point:{p,p1,q,p3})if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
   if(bx!=0||cx!=0){polygons.push_back({p,p1,q,p3,p});polygon_matrices.push_back(std::vector<Matrix>(5,matrix()));}
   else {path.push_back({p.x,p.y,q.x-p.x,q.y-p.y});rect_matrices.push_back(matrix());}stack.clear();}
  }else if(word=="W"||word=="W*"){
   if(!stack.empty()||pending_rect_clip||pending_path_clip)return false;
   pending_rule=word=="W*"?FillRule::EvenOdd:FillRule::NonZero;
   pending_rect_clip=path.size()==1&&polygons.empty();
   pending_path_clip=!pending_rect_clip;
  }else if(word=="S"||word=="s"){
   if(pending_rect_clip||pending_path_clip||!stack.empty()||width>1024||height>1024)return false;
   if(polygons.empty()&&path.empty())continue;
   // Move-only subpaths contain no drawable segments.
   bool drawable=!path.empty();
   for(const auto& poly:polygons)if(poly.size()>1)drawable=true;
   if(!drawable){path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();cubic_strokes.clear();cubic_strokes.clear();continue;}
   if(word=="s"&&!polygons.empty()){
    auto& poly=polygons.back();
    // A final move-only subpath has no closing segment.
    if(poly.size()>1){
     const Point first=poly.front(),last=poly.back();
     if(first.x!=last.x||first.y!=last.y){
      if(poly.size()>=4096)return false;
      poly.push_back(first);
      polygon_matrices.back().push_back(matrix());
     }
    }
   }
   // Normalize rectangle stroke paths to closed polygons so every join
   // style shares the same vertex processing and closing-corner logic.
   for(size_t k=0;k<path.size();++k){
    const Rect& rect=path[k];
    const Point p0{rect.x,rect.y},p1{rect.x+rect.w,rect.y};
    const Point p2{rect.x+rect.w,rect.y+rect.h},p3{rect.x,rect.y+rect.h};
    polygons.push_back({p0,p1,p2,p3,p0});
    polygon_matrices.push_back(std::vector<Matrix>(5,rect_matrices[k]));
   }
   path.clear();
   rect_matrices.clear();
   const unsigned char cr=(unsigned char)std::lround(stroke_red*255),cg=(unsigned char)std::lround(stroke_green*255),cb=(unsigned char)std::lround(stroke_blue*255);
   size_t stroke_work=0;
   const bool stroke_aa=(size_t)width*height<=262144;
   std::vector<uint8_t> stroke_samples((size_t)width*height,0);
   auto segment=[&](Point p,Point q,Matrix mat,bool join_disk=false,bool start_cap=true,bool end_cap=true){
    double x0=(p.x-box.x0)*sx,y0=(box.y1-p.y)*sy;
    double x1=(q.x-box.x0)*sx,y1=(box.y1-q.y)*sy;
    double dx=x1-x0,dy=y1-y0,denom=dx*dx+dy*dy;
    // A zero-length butt/square segment has no stroke area. Round caps
    // deliberately retain the circular point footprint.
    // A degenerate segment has no exposed square footprint when both
    // endpoint caps are suppressed (e.g. an interior zero-length edge).
    if(denom==0&&!join_disk&&(line_cap==0||
       (!start_cap&&!end_cap)))return true;
    const double metric=std::hypot(std::hypot(mat.a*sx,mat.b*sy),std::hypot(mat.c*sx,mat.d*sy));
    const double effective_width=line_width==0?(metric>0?1.0/metric:0):line_width;
    // Frobenius norm bounds the transformed unit normals. This is a
    // conservative raster bound, not a substitute for exact coverage.
    const double radius=0.5*effective_width*metric;
    const double determinant=mat.a*mat.d-mat.b*mat.c;
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-12)return false;
    const double ia=mat.d/determinant,ib=-mat.b/determinant;
    const double ic=-mat.c/determinant,id=mat.a/determinant;
    if(!std::isfinite(ia)||!std::isfinite(ib)||!std::isfinite(ic)||!std::isfinite(id))return false;
    if(!std::isfinite(denom)||!std::isfinite(radius)||radius>2048)return false;
    // Inverse-transform the segment direction once, not for each pixel.
    const double vx=dx/sx,vy=-dy/sy;
    const double ex=ia*vx+ic*vy;
    const double ey=ib*vx+id*vy;
    const double length2=ex*ex+ey*ey;
    if(!std::isfinite(length2))return false;
    const double inv_length=length2>0?1.0/std::sqrt(length2):0.0;
    const double extension=length2>0?(effective_width*0.5)*inv_length:0.0;
    if(!std::isfinite(inv_length)||!std::isfinite(extension))return false;
    // A square cap extends half a width along and across the segment.
    // Its diagonal reaches sqrt(2) times the half-width from an endpoint.
    // Only exposed square endpoints need the diagonal cap extension.
    // Interior dashed/solid pieces use the ordinary stroke bound.
    const bool exposed_square_cap=!join_disk&&line_cap==2&&(start_cap||end_cap);
    const double extra=radius*(exposed_square_cap?std::sqrt(2.0):1.0)+1;
    double low_x=std::min(x0,x1)-extra,high_x=std::max(x0,x1)+extra;
    double low_y=std::min(y0,y1)-extra,high_y=std::max(y0,y1)+extra;
    if(!std::isfinite(low_x)||!std::isfinite(high_x)||!std::isfinite(low_y)||!std::isfinite(high_y))return false;
    unsigned x_begin=(unsigned)std::clamp(std::floor(low_x),0.0,(double)width);
    unsigned x_end=(unsigned)std::clamp(std::ceil(high_x),0.0,(double)width);
    unsigned y_begin=(unsigned)std::clamp(std::floor(low_y),0.0,(double)height);
    unsigned y_end=(unsigned)std::clamp(std::ceil(high_y),0.0,(double)height);
    size_t work=(size_t)(x_end-x_begin)*(y_end-y_begin);
    if(work>16777216-stroke_work)return false;
    stroke_work+=work;
    for(unsigned yy=y_begin;yy<y_end;++yy)for(unsigned xx=x_begin;xx<x_end;++xx){
     auto covered=[&](double px,double py){
      const double ux=(px-x0)/sx,uy=-(py-y0)/sy;
      const double qx=ia*ux+ic*uy,qy=ib*ux+id*uy;
      if(!std::isfinite(qx)||!std::isfinite(qy))return false;
      if(length2==0&&!join_disk&&line_cap==2)
       return std::abs(qx)<=effective_width*0.5&&std::abs(qy)<=effective_width*0.5;
      double projection=length2>0?(qx*ex+qy*ey)/length2:0;
      if(!std::isfinite(projection))return false;
      if(!join_disk&&((!start_cap&&projection<0)||(!end_cap&&projection>1)||
        (line_cap==0&&(projection<0||projection>1))))return false;
      if(!join_disk&&line_cap==2){
       if(projection<(start_cap?-extension:0)||projection>(end_cap?1+extension:1))return false;
       // Square caps extend the stroke rectangle, not its circular ends.
       const double perpendicular=std::abs(qx*ey-qy*ex)*inv_length;
       return perpendicular<=effective_width*0.5;
      }
      const double t=join_disk?std::clamp(projection,0.0,1.0):
       (line_cap==1?(start_cap&&projection<0?0.0:end_cap&&projection>1?1.0:projection):projection);
      return std::hypot(qx-t*ex,qy-t*ey)<=effective_width*0.5;
     };
     uint8_t bits=0;
     if(stroke_aa){
      if(covered(xx+0.25,yy+0.25))bits|=1;
      if(covered(xx+0.75,yy+0.25))bits|=2;
      if(covered(xx+0.25,yy+0.75))bits|=4;
      if(covered(xx+0.75,yy+0.75))bits|=8;
     }else if(covered(xx+0.5,yy+0.5))bits=15;
     stroke_samples[(size_t)yy*width+xx]|=bits;
    }
    return true;
   };
   struct DashedJoin {Point prev,vertex,next;Matrix incoming,outgoing;};
   std::vector<DashedJoin> dashed_joins;
   std::vector<size_t> fully_painted_closed_paths;
   auto stroke_path=[&](size_t path_index,const std::vector<Point>& points,const std::vector<Matrix>& matrices)->bool{
    if(points.size()!=matrices.size())return false;
    if(dash_pattern.empty()){
     const bool closed=points.size()>2&&points.front().x==points.back().x&&points.front().y==points.back().y;
     for(size_t i=1;i<points.size();++i){
      const bool start_cap=!closed&&i==1,end_cap=!closed&&i+1==points.size();
      if(!segment(points[i-1],points[i],matrices[i],false,start_cap,end_cap))return false;
     }
     return true;
    }
    // Exact arc-length dash boundaries for an open, single-subpath
    // combination of straight segments and original cubic curves.
    const bool open_path=points.size()<2||
      points.front().x!=points.back().x||points.front().y!=points.back().y;
    // Closed contours without a painted seam can use inverse-arc-length
    // positioning safely. Painted seams still use the established join
    // path until cubic tangent joins are implemented.
    bool use_cubic_dashes=open_path;
    if(!open_path&&!cubic_strokes.empty()&&path_index==0&&polygons.size()==1){
     CubicDashCursor probe,initial;
     if(!cubic_dash_start(dash_pattern,dash_phase,probe))return false;
     initial=probe;
     size_t curve_index=0;
     bool last_on=false,any=false;
     for(size_t i=1;i<points.size();){
      const Matrix mat=matrices[i];
      const double det=mat.a*mat.d-mat.b*mat.c;
      if(!std::isfinite(det)||std::abs(det)<1e-12)return false;
      auto user=[&](Point p)->Point{
       return {(mat.d*p.x-mat.c*p.y)/det,(-mat.b*p.x+mat.a*p.y)/det};
      };
      const bool curved=curve_index<cubic_strokes.size()&&cubic_strokes[curve_index].start==i-1;
      size_t last=i;
      if(curved){
       const auto& curve=cubic_strokes[curve_index];
       last=curve.endpoint;
       if(last>=points.size()||last<i)return false;
       for(size_t k=i;k<=last;++k)
        if(matrices[k].a!=mat.a||matrices[k].b!=mat.b||
           matrices[k].c!=mat.c||matrices[k].d!=mat.d)return false;
       std::vector<CubicDashSpan> spans;
       if(!cubic_dash_spans(user(curve.p0),user(curve.p1),user(curve.p2),
                           user(curve.p3),dash_pattern,probe,spans))return false;
       if(!spans.empty()){any=true;last_on=spans.back().painted;}
       ++curve_index;
      }else{
       std::vector<LineDashSpan> spans;
       if(!line_dash_spans(user(points[i-1]),user(points[i]),
                           dash_pattern,probe,spans))return false;
       if(!spans.empty()){any=true;last_on=spans.back().painted;}
      }
      i=last+1;
     }
     CubicDashSeam seam;
     if(!cubic_dash_closed_seam(dash_pattern,initial,probe,
                               {last_on},{any},seam))return false;
     use_cubic_dashes=!seam.joined;
    }
    if(!cubic_strokes.empty()&&path_index==0&&polygons.size()==1&&use_cubic_dashes){
     CubicDashCursor cursor;
     if(!cubic_dash_start(dash_pattern,dash_phase,cursor))return false;
     size_t curve_index=0,emitted=0;
     bool continuing_on=false;
     for(size_t i=1;i<points.size();){
      const Matrix mat=matrices[i];
      const double det=mat.a*mat.d-mat.b*mat.c;
      if(!std::isfinite(det)||std::abs(det)<1e-12)return false;
      auto user=[&](Point p)->Point{
       return {(mat.d*p.x-mat.c*p.y)/det,
               (-mat.b*p.x+mat.a*p.y)/det};
      };
      const bool is_curve=curve_index<cubic_strokes.size()&&
        cubic_strokes[curve_index].start==i-1;
      // A cubic may have been authored under a different CTM. Do not
      // interpret it using a later segment's metric.
      size_t last=i;
      std::vector<LineDashSpan> spans;
      if(is_curve){
       const auto& curve=cubic_strokes[curve_index];
       last=curve.endpoint;
       if(last>=points.size()||last<=i-1)return false;
       for(size_t k=i;k<=last;++k)
        if(matrices[k].a!=mat.a||matrices[k].b!=mat.b||
           matrices[k].c!=mat.c||matrices[k].d!=mat.d)return false;
       std::vector<CubicDashSpan> cubic_spans;
       if(!cubic_dash_spans(user(curve.p0),user(curve.p1),user(curve.p2),
                           user(curve.p3),dash_pattern,cursor,cubic_spans))return false;
       for(const auto& span:cubic_spans)spans.push_back({span.t0,span.t1,span.painted});
      }else{
       if(!line_dash_spans(user(points[i-1]),user(points[i]),
                           dash_pattern,cursor,spans))return false;
      }
      for(size_t k=0;k<spans.size();++k){
       const auto& span=spans[k];
       if(!span.painted||span.t1<=span.t0){
        if(!span.painted)continuing_on=false;
        continue;
       }
       const bool run_starts_here=!continuing_on;
       // A painted dash continuing across an authored line/cubic boundary
       // needs a join rather than two capped stroke endpoints.
       if(!run_starts_here&&k==0&&span.t0==0&&i>1){
        const Point vertex=points[i-1];
        Point previous=points[i-2],following=is_curve?
          cubic_strokes[curve_index].p1:points[i];
        Matrix incoming=matrices[i-1];
        for(const auto& curve:cubic_strokes){
         if(curve.endpoint==i-1){
          Point tangent{};
          if(cubic_endpoint_tangent(curve.p0,curve.p1,curve.p2,curve.p3,true,tangent))
           previous={vertex.x-tangent.x,vertex.y-tangent.y};
          break;
         }
        }
        if(is_curve){
         const auto& curve=cubic_strokes[curve_index];
         Point tangent{};
         if(cubic_endpoint_tangent(curve.p0,curve.p1,curve.p2,curve.p3,false,tangent))
          following={vertex.x+tangent.x,vertex.y+tangent.y};
        }
        if(!std::isfinite(previous.x)||!std::isfinite(previous.y)||
           !std::isfinite(following.x)||!std::isfinite(following.y))return false;
        if(line_join==1){
         if(!segment(vertex,vertex,mat,true))return false;
        }else if(line_join==0||line_join==2){
         if(dashed_joins.size()>=4096)return false;
         dashed_joins.push_back({previous,vertex,following,incoming,mat});
        }
       }
       std::vector<Point> vertices;
       if(is_curve){
        const auto& curve=cubic_strokes[curve_index];
        if(!cubic_dash_flatten_span(curve.p0,curve.p1,curve.p2,curve.p3,
             span.t0,span.t1,sx,sy,vertices))return false;
       }else{
        const Point p=points[i-1],q=points[i];
        vertices.push_back({p.x+(q.x-p.x)*span.t0,p.y+(q.y-p.y)*span.t0});
        vertices.push_back({p.x+(q.x-p.x)*span.t1,p.y+(q.y-p.y)*span.t1});
       }
       const size_t steps=vertices.size()-1;
       for(size_t j=0;j<steps;++j){
        if(++emitted>65536)return false;
        const Point a=vertices[j],b=vertices[j+1];
        if(!std::isfinite(a.x)||!std::isfinite(a.y)||
           !std::isfinite(b.x)||!std::isfinite(b.y))return false;
        // Only exposed on-run endpoints receive caps. The end of one
        // line/curve segment is not necessarily the end of a dash.
        bool next_on=false;
        if(k+1<spans.size())next_on=spans[k+1].painted;
        else if(last+1<points.size()){
         CubicDashCursor probe=cursor;
         if(probe.remaining<=0&&
            !cubic_dash_advance(dash_pattern,probe))return false;
         next_on=probe.index%2==0;
        }
        const bool at_start=j==0,at_end=j+1==steps;
        if(!segment(a,b,mat,false,
           at_start&&run_starts_here,at_end&&!next_on))return false;
        if(at_end)continuing_on=next_on;
       }
      }
      if(is_curve)++curve_index;
      i=last+1;
     }
     return true;
    }
   const size_t count=dash_pattern.size(),cycle=count*(count%2?2:1);
    size_t index=0;
    double remaining=dash_pattern[0],phase=dash_phase;
    // Skip zero-width dash elements without consuming path length.
    auto advance=[&]()->bool{
     for(size_t n=0;n<cycle;++n){
      index=(index+1)%cycle;
      remaining=dash_pattern[index%count];
      if(remaining>0)return true;
     }
     return false;
    };
    // Resolve the phase by bounded traversal of one normalized cycle.
    // Subtracting a very small dash repeatedly can otherwise lose
    // precision and fail to advance a large phase.
    for(size_t step=0;phase>0&&step<=cycle;++step){
     if(remaining<=0){if(!advance())return false;continue;}
     if(phase>=remaining){
      phase-=remaining;
      remaining=0;
     }else{
      remaining-=phase;
      phase=0;
     }
    }
    if(phase>0)return false;
    // Resolve exact phase boundaries before consuming the first segment.
    if(remaining<=0&&!advance())return false;
    // When a single painted dash covers the entire closed path, the
    // seam is a join, not two independent capped dash endpoints.
    const bool closed=points.size()>2&&points.front().x==points.back().x&&points.front().y==points.back().y;
    if(closed&&index%2==0){
     double total_length=0;
     for(size_t i=1;i<points.size();++i){
      const Matrix mat=matrices[i];
      const double det=mat.a*mat.d-mat.b*mat.c;
      if(!std::isfinite(det)||std::abs(det)<1e-12)return false;
      const double dx=points[i].x-points[i-1].x,dy=points[i].y-points[i-1].y;
      total_length+=std::hypot((mat.d*dx-mat.c*dy)/det,(-mat.b*dx+mat.a*dy)/det);
      if(!std::isfinite(total_length))return false;
     }
     if(total_length<=remaining){
      fully_painted_closed_paths.push_back(path_index);
      for(size_t i=1;i<points.size();++i)
       if(!segment(points[i-1],points[i],matrices[i],false,false,false))return false;
      return true;
     }
    }
    // Detect a painted run crossing the closed contour seam.
    // This does not imply that the whole contour is painted.
    const bool first_on=closed&&index%2==0;
    bool seam_on=false;
    if(first_on){
     double length=0;
     for(size_t i=1;i<points.size();++i){
      const Matrix mat=matrices[i];
      const double det=mat.a*mat.d-mat.b*mat.c;
      if(!std::isfinite(det)||std::abs(det)<1e-12)return false;
      const double dx=points[i].x-points[i-1].x,dy=points[i].y-points[i-1].y;
      length+=std::hypot((mat.d*dx-mat.c*dy)/det,(-mat.b*dx+mat.a*dy)/det);
      if(!std::isfinite(length))return false;
     }
     const CubicDashCursor initial_cursor{index,remaining};
     size_t probe=index;
     double left=remaining;
     bool resolved=length<=1e-10;
     // A zero-length contour has no incoming or outgoing tangent.
     if(resolved)seam_on=false;
     for(size_t n=0;n<65536&&length>1e-10;++n){
      if(left<=1e-12){
       bool found=false;
       for(size_t j=0;j<cycle;++j){
        probe=(probe+1)%cycle;
        left=dash_pattern[probe%count];
        if(left>0){found=true;break;}
       }
       if(!found)return false;
      }
      const double take=std::min(left,length);
      if(!std::isfinite(take)||take<=0)return false;
      const double before=length;
      length-=take;left-=take;
      if(length==before)return false;
      if(length<=1e-10){
       // If the perimeter ends exactly at a dash boundary, the
       // preceding on-run ends there; it does not continue across seam.
       // Classify the terminal dash state through the shared closed
       // seam contract; a boundary with no remaining paint is a cap.
       CubicDashSeam seam;
       const CubicDashCursor terminal_cursor{probe,left>1e-10?left:0.0};
       if(!cubic_dash_closed_seam(dash_pattern,initial_cursor,terminal_cursor,
             {probe%2==0},{true},seam))return false;
       seam_on=seam.joined;
       resolved=true;
       break;
      }
     }
     // Do not silently render an incorrect cap when the finite traversal
     // budget cannot resolve the state at the closed contour seam.
     if(!resolved)return false;
    }
    const bool joined_seam=first_on&&seam_on;
    size_t pieces=0;
    bool in_on_run=false;
    Point run_end{};
    Matrix run_matrix{};
    std::vector<std::pair<Point,Matrix>> on_run_vertices;
    auto emit_run=[&]()->bool{
     if(!in_on_run)return true;
     // At a painted closed seam, the final dash endpoint is a join,
     // not a round cap. Interior dash runs retain their normal caps.
     const bool at_seam=joined_seam&&run_end.x==points.front().x&&run_end.y==points.front().y;
     if(line_cap==1&&!at_seam&&!segment(run_end,run_end,run_matrix,true))return false;
     if(line_join==1){
      for(const auto& vertex:on_run_vertices)
       if(!segment(vertex.first,vertex.first,vertex.second,true))return false;
     }
     on_run_vertices.clear();
     in_on_run=false;
     return true;
    };
    for(size_t i=1;i<points.size();++i){
     const Point p=points[i-1],q=points[i];
     // Dash lengths are measured in user-space units, not in the
     // transformed device-space coordinates stored in the current path.
     const Matrix mat=matrices[i];
     const double determinant=mat.a*mat.d-mat.b*mat.c;
     if(!std::isfinite(determinant)||std::abs(determinant)<1e-12)return false;
     const double dx=q.x-p.x,dy=q.y-p.y;
     const double ux=(mat.d*dx-mat.c*dy)/determinant;
     const double uy=(-mat.b*dx+mat.a*dy)/determinant;
     const double len=std::hypot(ux,uy);
     if(!std::isfinite(len))return false;
     if(len==0)continue;
     double pos=0;
     while(pos<len){
      if(++pieces>65536)return false;
      if(remaining<=1e-12){if(!advance())return false;continue;}
      double take=std::min(remaining,len-pos);
      if(take<=0)return false;
      if(index%2==0){
       const double t0=pos/len,t1=(pos+take)/len;
       Point a{p.x+(q.x-p.x)*t0,p.y+(q.y-p.y)*t0};
       Point b{p.x+(q.x-p.x)*t1,p.y+(q.y-p.y)*t1};
       if(!in_on_run){
        if(line_cap==1&&!(joined_seam&&i==1&&pos==0)&&!segment(a,a,matrices[i],true))return false;
       }else if(pos==0){
        if(line_join==1){
         if(on_run_vertices.size()>=4096)return false;
         on_run_vertices.push_back({a,matrices[i]});
        }
        if(i>=2&&line_join!=1){
         if(dashed_joins.size()>=4096)return false;
         dashed_joins.push_back({points[i-2],a,b,matrices[i-1],matrices[i]});
        }
       }
       // A dash run may cross a path vertex without acquiring a cap.
       // Only the start and end of a painted run receive square caps.
       bool breaks_after=false;
       if(remaining-take<=1e-12){
        for(size_t n=1;n<=cycle;++n){
         const size_t next_index=(index+n)%cycle;
         if(dash_pattern[next_index%count]>0){
          breaks_after=(next_index%2)!=0;
          break;
         }
        }
       }
       const bool last_piece=i+1==points.size()&&len-(pos+take)<=1e-12;
       const bool start_cap=!in_on_run&&!(joined_seam&&i==1&&pos==0);
       const bool end_cap=breaks_after||(last_piece&&!joined_seam);
       if(!segment(a,b,matrices[i],false,start_cap,end_cap))return false;
       in_on_run=true;run_end=b;run_matrix=matrices[i];
      }else if(!emit_run())return false;
      pos+=take;remaining-=take;
     }
    }
    if(!emit_run())return false;
    if(joined_seam&&points.size()>3){
     const Point& p=points.front();
     // Skip repeated vertices to recover the actual incoming and outgoing
     // tangents. The previous guard discarded a valid seam join entirely.
     size_t incoming=points.size()-2;
     while(incoming>0&&std::hypot(p.x-points[incoming].x,p.y-points[incoming].y)<=1e-10)--incoming;
     size_t outgoing=1;
     while(outgoing+1<points.size()&&std::hypot(p.x-points[outgoing].x,p.y-points[outgoing].y)<=1e-10)++outgoing;
     if(std::hypot(p.x-points[incoming].x,p.y-points[incoming].y)>1e-10&&
        std::hypot(p.x-points[outgoing].x,p.y-points[outgoing].y)>1e-10){
      if(line_join==1){
       if(!segment(p,p,matrices.back(),true))return false;
      }else if(line_join==0||line_join==2){
       if(dashed_joins.size()>=4096)return false;
       Point previous=points[incoming],following=points[outgoing];
       // Use analytic cubic endpoint tangents at a painted closing seam.
       // These vectors only affect join geometry, not the stroked path.
       if(path_index==0&&polygons.size()==1){
        if(!cubic_strokes.empty()&&cubic_strokes.front().start==0){
         const auto& curve=cubic_strokes.front();
         Point tangent{};
         if(cubic_endpoint_tangent(curve.p0,curve.p1,curve.p2,curve.p3,false,tangent)){
          const Point candidate{p.x+tangent.x,p.y+tangent.y};
          if(!std::isfinite(candidate.x)||!std::isfinite(candidate.y))return false;
          following=candidate;
         }
        }
        if(!cubic_strokes.empty()&&cubic_strokes.back().endpoint==points.size()-1){
         const auto& curve=cubic_strokes.back();
         Point tangent{};
         if(cubic_endpoint_tangent(curve.p0,curve.p1,curve.p2,curve.p3,true,tangent)){
          const Point candidate{p.x-tangent.x,p.y-tangent.y};
          if(!std::isfinite(candidate.x)||!std::isfinite(candidate.y))return false;
          previous=candidate;
         }
        }
       }
       dashed_joins.push_back({previous,p,following,matrices[incoming+1],matrices[outgoing]});
      }
     }
    }
    return true;
   };
   for(size_t k=0;k<polygons.size();++k)if(!stroke_path(k,polygons[k],polygon_matrices[k]))return false;
   // Round joins are disks at connected vertices. Keep the union in
   // stroke_samples so each subsample is blended only once.
   if(line_join==1){
    for(size_t k=0;k<polygons.size();++k){
     if(!dash_pattern.empty()&&std::find(fully_painted_closed_paths.begin(),fully_painted_closed_paths.end(),k)==fully_painted_closed_paths.end())continue;
     const auto& poly=polygons[k];
     for(size_t i=1;i+1<poly.size();++i){
      if(!segment(poly[i],poly[i],polygon_matrices[k][i],true))return false;
     }
     if(poly.size()>2&&poly.front().x==poly.back().x&&poly.front().y==poly.back().y){
      if(!segment(poly.front(),poly.front(),polygon_matrices[k].front(),true))return false;
     }
    }
   }
   // Join geometry in device space. Supported for uniform, axis-aligned CTM.
   auto paint_triangle=[&](Point p0,Point p1,Point p2)->bool{
    const double x0=(p0.x-box.x0)*sx,y0=(box.y1-p0.y)*sy;
    const double x1=(p1.x-box.x0)*sx,y1=(box.y1-p1.y)*sy;
    const double x2=(p2.x-box.x0)*sx,y2=(box.y1-p2.y)*sy;
    const double area=(x1-x0)*(y2-y0)-(y1-y0)*(x2-x0);
    if(!std::isfinite(x0)||!std::isfinite(y0)||!std::isfinite(x1)||!std::isfinite(y1)||!std::isfinite(x2)||!std::isfinite(y2)||!std::isfinite(area))return false;
    if(std::abs(area)<1e-10)return true;
    unsigned xb=(unsigned)std::clamp(std::floor(std::min({x0,x1,x2})-1),0.0,(double)width);
    unsigned xe=(unsigned)std::clamp(std::ceil(std::max({x0,x1,x2})+1),0.0,(double)width);
    unsigned yb=(unsigned)std::clamp(std::floor(std::min({y0,y1,y2})-1),0.0,(double)height);
    unsigned ye=(unsigned)std::clamp(std::ceil(std::max({y0,y1,y2})+1),0.0,(double)height);
    size_t work=(size_t)(xe-xb)*(ye-yb);
    if(work>16777216-stroke_work)return false;
    stroke_work+=work;
    for(unsigned yy=yb;yy<ye;++yy)for(unsigned xx=xb;xx<xe;++xx){
     auto inside=[&](double px,double py){
      double e0=(x1-x0)*(py-y0)-(y1-y0)*(px-x0);
      double e1=(x2-x1)*(py-y1)-(y2-y1)*(px-x1);
      double e2=(x0-x2)*(py-y2)-(y0-y2)*(px-x2);
      return area>0?(e0>=0&&e1>=0&&e2>=0):(e0<=0&&e1<=0&&e2<=0);
     };
     uint8_t bits=0;
     if(stroke_aa){
      if(inside(xx+.25,yy+.25))bits|=1;
      if(inside(xx+.75,yy+.25))bits|=2;
      if(inside(xx+.25,yy+.75))bits|=4;
      if(inside(xx+.75,yy+.75))bits|=8;
     }else if(inside(xx+.5,yy+.5))bits=15;
     stroke_samples[(size_t)yy*width+xx]|=bits;
    }
    return true;
   };
   auto paint_corner=[&](Point prev,Point p,Point next,Matrix ma,Matrix mb)->bool{
    const double ax=p.x-prev.x,ay=p.y-prev.y,bx2=next.x-p.x,by2=next.y-p.y;
    const double al=std::hypot(ax,ay),bl=std::hypot(bx2,by2);
    if(!std::isfinite(al)||!std::isfinite(bl))return false;
    if(al<1e-10||bl<1e-10)return true;
    // Normalize before testing collinearity: the raw cross product
    // changes quadratically with segment length and loses small turns.
    const double ux0=ax/al,uy0=ay/al,ux1=bx2/bl,uy1=by2/bl;
    const double turn=ux0*uy1-uy0*ux1;
    const double alignment=ux0*ux1+uy0*uy1;
    if(!std::isfinite(turn)||!std::isfinite(alignment))return false;
    if(std::abs(turn)<1e-12&&alignment>0)return true;
    // A reversal has no finite miter intersection. Its caps are handled
    // by the stroke rasterizer; avoid producing an unbounded triangle.
    if(std::abs(turn)<1e-12)return true;
    const double side=turn>0?-1.0:1.0;
    // Compute each offset from its own segment CTM. The path points
    // are already transformed; the inverse maps each tangent back to
    // the user-space direction used to define stroke width.
    auto offset=[&](double tx,double ty,Matrix mat,Point& out)->bool{
     const double det=mat.a*mat.d-mat.b*mat.c;
     if(!std::isfinite(det)||std::abs(det)<1e-12)return false;
     const double ux=(mat.d*tx-mat.c*ty)/det;
     const double uy=(-mat.b*tx+mat.a*ty)/det;
     const double ul=std::hypot(ux,uy);
     if(!std::isfinite(ul)||ul<1e-12)return false;
     const double nx=side*(-uy/ul),ny=side*(ux/ul);
     const double half=line_width==0?0.5/std::max(sx,sy):line_width*.5;
     out={half*(mat.a*nx+mat.c*ny),half*(mat.b*nx+mat.d*ny)};
     return std::isfinite(out.x)&&std::isfinite(out.y);
    };
    Point oa{},ob{};
    if(!offset(ax,ay,ma,oa)||!offset(bx2,by2,mb,ob))return false;
    const double radius=std::max(std::hypot(oa.x,oa.y),std::hypot(ob.x,ob.y));
    if(radius<=0)return true;
    const Point u{p.x+oa.x,p.y+oa.y};
    const Point v{p.x+ob.x,p.y+ob.y};
    if(line_join==2)return paint_triangle(p,u,v);
    const double ux=ax/al,uy=ay/al,vx=bx2/bl,vy=by2/bl;
    const double det=ux*vy-uy*vx;
    // Near-parallel offset lines produce an unbounded miter. Use the
    // bevel fallback instead of relying on an arbitrary angle cutoff.
    if(!std::isfinite(det)||std::abs(det)<1e-14)return paint_triangle(p,u,v);
    const double t=((v.x-u.x)*vy-(v.y-u.y)*vx)/det;
    if(!std::isfinite(t))return paint_triangle(p,u,v);
    const Point tip{u.x+t*ux,u.y+t*uy};
    if(!std::isfinite(tip.x)||!std::isfinite(tip.y))return paint_triangle(p,u,v);
    // Miter limit is determined by the angle between the two
    // user-space tangents, not the Euclidean length after CTM.
    // For a turn angle theta, the miter/half-width ratio is
    // 1/cos(theta/2) = sqrt(2/(1+cos(theta))).
    // This also avoids numerical error from a distant intersection.
    double miter_ratio=0;
    if(ma.a==mb.a&&ma.b==mb.b&&ma.c==mb.c&&ma.d==mb.d){
     const double md=ma.a*ma.d-ma.b*ma.c;
     if(!std::isfinite(md)||std::abs(md)<1e-12)return false;
     const double iax=(ma.d*ax-ma.c*ay)/md;
     const double iay=(-ma.b*ax+ma.a*ay)/md;
     const double ibx=(ma.d*bx2-ma.c*by2)/md;
     const double iby=(-ma.b*bx2+ma.a*by2)/md;
     const double la=std::hypot(iax,iay),lb=std::hypot(ibx,iby);
     if(!std::isfinite(la)||!std::isfinite(lb)||la<=0||lb<=0)return false;
     // Normalize first: computing dot/(la*lb) can overflow for
     // finite but strongly anisotropic inverse-transformed tangents.
     const double nax=iax/la,nay=iay/la,nbx=ibx/lb,nby=iby/lb;
     const double cosine=std::clamp(nax*nbx+nay*nby,-1.0,1.0);
     if(!std::isfinite(cosine))return false;
     // Compare squared values to avoid a large intermediate miter ratio.
     const double denominator=1.0+cosine;
     if(denominator<=0||miter_limit*miter_limit*denominator<2.0)
      return paint_triangle(p,u,v);
     miter_ratio=std::sqrt(2.0/denominator);
    }else{
     // A path assembled under different CTMs has no single stroke
     // space. Use the existing device-space conservative fallback.
     miter_ratio=std::hypot(tip.x-p.x,tip.y-p.y)/radius;
    }
    if(!std::isfinite(miter_ratio)||miter_ratio>miter_limit)return paint_triangle(p,u,v);
    return paint_triangle(p,u,v)&&paint_triangle(u,tip,v);
   };
   if(line_join==0||line_join==2){
    {
     for(size_t k=0;k<polygons.size();++k){
      if(!dash_pattern.empty()&&std::find(fully_painted_closed_paths.begin(),fully_painted_closed_paths.end(),k)==fully_painted_closed_paths.end())continue;
      const auto& poly=polygons[k];
      const bool closed=poly.size()>3&&poly.front().x==poly.back().x&&poly.front().y==poly.back().y;
      for(size_t i=closed?0:1;i+1<poly.size();++i){
       Point prev=i==0?poly[poly.size()-2]:poly[i-1];
       Point next=poly[i+1];
       const Matrix ma=polygon_matrices[k][i==0?poly.size()-1:i],mb=polygon_matrices[k][i+1];
       // A fully painted closed contour uses the same analytic seam
       // tangents as the partial dashed-seam path. Do not substitute
       // a cubic tangent if the incoming edge is a separate close line.
       if(i==0&&k==0&&polygons.size()==1&&!cubic_strokes.empty()){
        const Point p=poly.front();
        if(cubic_strokes.front().start==0){
         Point tangent{};
         const auto& curve=cubic_strokes.front();
         if(cubic_endpoint_tangent(curve.p0,curve.p1,curve.p2,curve.p3,false,tangent)){
          next={p.x+tangent.x,p.y+tangent.y};
          if(!std::isfinite(next.x)||!std::isfinite(next.y))return false;
         }
        }
        if(cubic_strokes.back().endpoint==poly.size()-1){
         Point tangent{};
         const auto& curve=cubic_strokes.back();
         if(cubic_endpoint_tangent(curve.p0,curve.p1,curve.p2,curve.p3,true,tangent)){
          prev={p.x-tangent.x,p.y-tangent.y};
          if(!std::isfinite(prev.x)||!std::isfinite(prev.y))return false;
         }
        }
       }
       // Preserve the authored cubic tangents at internal path joins.
       // Vertex indices refer to the flattened polygon and remain stable
       // until the stroke command. Only change the join direction.
       if(k==0&&polygons.size()==1&&!cubic_strokes.empty()&&i>0){
        const Point p=poly[i];
        for(const auto& curve:cubic_strokes){
         Point tangent{};
         if(curve.endpoint==i&&
            cubic_endpoint_tangent(curve.p0,curve.p1,curve.p2,curve.p3,true,tangent)){
          prev={p.x-tangent.x,p.y-tangent.y};
          if(!std::isfinite(prev.x)||!std::isfinite(prev.y))return false;
         }
         if(curve.start==i&&
            cubic_endpoint_tangent(curve.p0,curve.p1,curve.p2,curve.p3,false,tangent)){
          next={p.x+tangent.x,p.y+tangent.y};
          if(!std::isfinite(next.x)||!std::isfinite(next.y))return false;
         }
        }
       }
       if(!paint_corner(prev,poly[i],next,ma,mb))return false;
      }
     }
    }
    for(const auto& join:dashed_joins)
     if(!paint_corner(join.prev,join.vertex,join.next,join.incoming,join.outgoing))return false;
   }
   for(size_t k=0;k<path.size();++k){const Rect& rect=path[k];
    Point p0{rect.x,rect.y},p1{rect.x+rect.w,rect.y},p2{rect.x+rect.w,rect.y+rect.h},p3{rect.x,rect.y+rect.h};
    if(!stroke_path(polygons.size()+k,{p0,p1,p2,p3,p0},std::vector<Matrix>(5,rect_matrices[k])))return false;
   }
   if(line_join==1&&dash_pattern.empty()){
    for(size_t k=0;k<path.size();++k){
     const Rect& rect=path[k];
     for(const Point p:{Point{rect.x,rect.y},Point{rect.x+rect.w,rect.y},Point{rect.x+rect.w,rect.y+rect.h},Point{rect.x,rect.y+rect.h}}){
      if(!segment(p,p,rect_matrices[k],true))return false;
     }
    }
   }
   for(size_t i=0;i<stroke_samples.size();++i){
    unsigned x=(unsigned)(i%width),y=(unsigned)(i/width);
    if(x<clip.x0||x>=clip.x1||y<clip.y0||y>=clip.y1||(clip_mask!=nullptr&&!(*clip_mask)[i]))continue;
    unsigned bits=stroke_samples[i],hits=(unsigned)((bits&1)!=0)+((bits&2)!=0)+((bits&4)!=0)+((bits&8)!=0);
    if(!hits)continue;
    unsigned alpha=(hits*255+2)/4;
    alpha=(alpha*unsigned(std::lround(stroke_alpha*255))+127)/255;
    if(clip_mask!=nullptr)alpha=(alpha*(*clip_mask)[i]+127)/255;
    if(!alpha&&!(transparent_target&&transparent_target->knockout))continue;
    if(transparent_target){
     unsigned geometry=(hits*255+2)/4;
     if(clip_mask)geometry=(geometry*(*clip_mask)[i]+127)/255;
     if(!transparent_target->mark_shape(x,y,uint8_t(geometry))||
        !transparent_target->blend_pixel(x,y,cr,cg,cb,uint8_t(alpha),
                                transparent_blend_mode(),uint8_t(geometry)))
      return false;
     continue;
    }
    uint8_t* pixel=rgba+(size_t)y*stride+(size_t)x*4;
    pixel[0]=(uint8_t)((alpha*blend_channel(cr,pixel[0])+(255-alpha)*pixel[0]+127)/255);
    pixel[1]=(uint8_t)((alpha*blend_channel(cg,pixel[1])+(255-alpha)*pixel[1]+127)/255);
    pixel[2]=(uint8_t)((alpha*blend_channel(cb,pixel[2])+(255-alpha)*pixel[2]+127)/255);
    pixel[3]=255;
   }
   path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();cubic_strokes.clear();
  }else if(word=="f"||word=="F"||word=="f*"){
   if((pending_rect_clip||pending_path_clip)||!stack.empty())return false;
   unsigned char cr=(unsigned char)std::lround(red*255);
   unsigned char cg=(unsigned char)std::lround(green*255);
   unsigned char cb=(unsigned char)std::lround(blue*255);
   if((size_t)width*height>1048576)return false;

   auto shapes=shapes_from_path();
   std::vector<uint8_t> coverage;
   const FillRule rule=word=="f*"?FillRule::EvenOdd:FillRule::NonZero;
   const bool antialias=(size_t)width*height<=262144;
   if(antialias){
    if(!rasterize_path_coverage_2x(shapes,width,height,rule,coverage))return false;
   }else{
    if(!rasterize_path_mask(shapes,width,height,rule,coverage))return false;
   }
   for(size_t i=0;i<coverage.size();++i){
    unsigned x=(unsigned)(i%width),y=(unsigned)(i/width);
    if(!coverage[i]||(clip_mask!=nullptr&&!(*clip_mask)[i])||x<clip.x0||x>=clip.x1||y<clip.y0||y>=clip.y1)continue;
    uint8_t* pixel=rgba+(size_t)y*stride+(size_t)x*4;
    unsigned alpha=antialias?coverage[i]:255;
    alpha=(alpha*unsigned(std::lround(fill_alpha*255))+127)/255;
    if(clip_mask!=nullptr)alpha=(alpha*(*clip_mask)[i]+127)/255;
    if(!alpha&&!(transparent_target&&transparent_target->knockout))continue;
    if(transparent_target){
     unsigned geometry=antialias?coverage[i]:255;
     if(clip_mask)geometry=(geometry*(*clip_mask)[i]+127)/255;
     if(!transparent_target->mark_shape(x,y,uint8_t(geometry))||
        !transparent_target->blend_pixel(x,y,cr,cg,cb,uint8_t(alpha),
                                transparent_blend_mode(),uint8_t(geometry)))
      return false;
     continue;
    }
    pixel[0]=(uint8_t)((alpha*blend_channel(cr,pixel[0])+(255-alpha)*pixel[0]+127)/255);
    pixel[1]=(uint8_t)((alpha*blend_channel(cg,pixel[1])+(255-alpha)*pixel[1]+127)/255);
    pixel[2]=(uint8_t)((alpha*blend_channel(cb,pixel[2])+(255-alpha)*pixel[2]+127)/255);
    pixel[3]=255;
   }
   path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();cubic_strokes.clear();
  }else if(word=="n"){
   if(!stack.empty())return false;
   if(pending_path_clip){
    auto shapes=shapes_from_path();
    std::vector<uint8_t> mask;
    if((size_t)width*height<=262144){
     if(!rasterize_path_coverage_2x(shapes,width,height,pending_rule,mask))return false;
    }else{
     if(!rasterize_path_mask(shapes,width,height,pending_rule,mask))return false;
     for(auto& v:mask)v=v?255:0;
    }
    if(clip_mask){
     if(!intersect_path_masks(mask,*clip_mask))return false;
    }
    clip_mask=std::make_shared<const std::vector<uint8_t>>(std::move(mask));
    pending_path_clip=false;
   }
   if(pending_rect_clip){
    auto shapes=shapes_from_path();
    std::vector<uint8_t> mask;
    if((size_t)width*height<=262144){
     if(!rasterize_path_coverage_2x(shapes,width,height,pending_rule,mask))return false;
    }else{
     if(!rasterize_path_mask(shapes,width,height,pending_rule,mask))return false;
     for(auto& value:mask)value=value?255:0;
    }
    if(clip_mask){
     if(!intersect_path_masks(mask,*clip_mask))return false;
    }
    clip_mask=std::make_shared<const std::vector<uint8_t>>(std::move(mask));
    pending_rect_clip=false;
   }
   path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();cubic_strokes.clear();
  }else if(compatibility_depth>0){
   // PDF compatibility sections may contain unrecognized operators.
   // Drop their operand stack, but do not change rendering state.
   stack.clear();
  }else return false; // Do not silently ignore unknown operators.
 }
 return !has_pending_text&&pending_font.empty()&&!in_text&&compatibility_depth==0&&!reading_dash&&!dash_array_ready&&dash_values.empty()&&stack.empty()&&path.empty()&&polygons.empty()&&saved.empty()&&marked_languages.empty()&&!pending_rect_clip&&!pending_path_clip;
}
bool render_rectangles_box(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride,PageBox box){
 return render_rectangles_box_with_fonts(contents,size,width,height,rgba,stride,box,{});
}
bool render_rectangles(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride){
 return render_rectangles_box(contents,size,width,height,rgba,stride,{0,0,(double)width,(double)height});
}
}
