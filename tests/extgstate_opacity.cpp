#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

int main(){
 rinpdf::renderer::GraphicsStateResources states;
 rinpdf::parser::PageGraphicsState translucent;
 translucent.fill_alpha=0.5;translucent.has_fill_alpha=true;
 states.emplace("Half",translucent);
 std::vector<uint8_t> rgba(8*8*4,0);
 const char content[]="q /Half gs 0 g 0 0 8 8 re f Q 0 g 0 0 4 4 re f";
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(content),sizeof(content)-1,
      8,8,rgba.data(),32,{0,0,8,8},{},{},{},{},true,0,states))
  return 1;
 // Top left is covered by the restored opaque black rectangle;
 // bottom right is painted with 50% opacity over white.
 const auto value=[&](unsigned x,unsigned y){
  return rgba[(size_t(y)*8+x)*4];
 };
 // Opaque black should cover the bottom-left quarter after Q restores
 // normal opacity. The other regions remain half black over white.
 if(value(1,6)!=0||value(6,1)!=127||value(6,6)!=127)return 2;
 // Diagnostic text must blend with the nonstroking ca value, while
 // a stroke-only CA setting must not change glyph opacity.
 const rinpdf::renderer::FontResources fonts{{"FD",{"RinPDF-Diagnostic","Type1"}}};
 const char text_content[]="q /Half gs BT /FD 10 Tf 1 0 0 1 2 10 Tm (HI) Tj ET Q";
 std::vector<uint8_t> faded(32*16*4),opaque(32*16*4);
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(text_content),sizeof(text_content)-1,
      32,16,faded.data(),32*4,{0,0,32,16},fonts,{},{},{},true,0,states))
  return 5;
 const char solid_text[]="BT /FD 10 Tf 1 0 0 1 2 10 Tm (HI) Tj ET";
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(solid_text),sizeof(solid_text)-1,
      32,16,opaque.data(),32*4,{0,0,32,16},fonts))
  return 6;
 bool saw_ink=false;
 for(size_t i=0;i<opaque.size();i+=4){
  if(opaque[i]==255)continue;
  saw_ink=true;
  const unsigned expected=(unsigned(opaque[i])*128+255u*127u+127u)/255u;
  if(faded[i]!=expected)return 7;
 }
 if(!saw_ink)return 8;
 // A Form XObject inherits the current graphics state, including ca.
 rinpdf::parser::PageForm form;
 form.bbox[0]=0;form.bbox[1]=0;form.bbox[2]=8;form.bbox[3]=8;
 const char form_content[]="0 g 0 0 8 8 re f";
 form.content.assign(form_content,form_content+sizeof(form_content)-1);
 rinpdf::renderer::FormResources forms;
 forms.emplace("Form1",form);
 const char with_form[]="q /Half gs /Form1 Do Q";
 std::vector<uint8_t> form_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(with_form),sizeof(with_form)-1,
      8,8,form_pixels.data(),32,{0,0,8,8},{},{},{},forms,true,0,states))
  return 9;
 if(form_pixels[0]!=127||form_pixels[1]!=127||
    form_pixels[2]!=127)return 10;
 // Verify the public page-resource resolver, including parent inheritance
 // and partial ExtGState keys that must not reset unrelated opacity.
 std::string pdf="%PDF-1.4\n";
 size_t offset[6]{};
 auto add=[&](unsigned id,const std::string& body){
  offset[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 add(1,"<< /Type /Catalog /Pages 2 0 R >>");
 add(2,"<< /Type /Pages /Kids [3 0 R] /Count 1 /Resources << /ExtGState << /Half 4 0 R /StrokeOnly 5 0 R >> >> >>");
 add(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 8 8] >>");
 add(4,"<< /Type /ExtGState /ca 0.5 /BM /Normal /SMask /None >>");
 add(5,"<< /Type /ExtGState /CA 0.25 >>");
 const size_t startxref=pdf.size();
 pdf+="xref\n0 6\n0000000000 65535 f \n";
 for(unsigned id=1;id<=5;++id){
  char entry[32];
  std::snprintf(entry,sizeof(entry),"%010zu 00000 n \n",offset[id]);
  pdf+=entry;
 }
 pdf+="trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(startxref)+"\n%%EOF\n";
 rinpdf::parser::PageGraphicsStates parsed;
 if(!rinpdf::parser::read_page_graphics_states(
       reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,parsed))
  return 3;
 const auto half=parsed.find("Half");
 const auto stroke=parsed.find("StrokeOnly");
 if(half==parsed.end()||stroke==parsed.end()||
    !half->second.has_fill_alpha||half->second.has_stroke_alpha||
    half->second.fill_alpha!=0.5||
    !stroke->second.has_stroke_alpha||stroke->second.has_fill_alpha||
    stroke->second.stroke_alpha!=0.25)return 4;
 return 0;
}
