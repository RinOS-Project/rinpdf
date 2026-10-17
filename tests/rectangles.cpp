#include "../src/renderer/rectangles.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
int main(){
 std::vector<unsigned char> pixels(8*8*4);
 const char content[]="0 g 2 2 4 4 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(content),sizeof(content)-1,8,8,pixels.data(),8*4))return 1;
 auto pixel=[&](unsigned x,unsigned y){return pixels[(y*8+x)*4];};
 if(pixel(0,0)!=255||pixel(3,3)!=0||pixel(7,7)!=255)return 2;
 const char flatness[]="q 10 i Q 0 g 2 2 4 4 re f";
 if(!rinpdf::renderer::render_rectangles(
     reinterpret_cast<const unsigned char*>(flatness),sizeof(flatness)-1,
     8,8,pixels.data(),32))return 200;
 const char bad_flatness[]="101 i";
 if(rinpdf::renderer::render_rectangles(
     reinterpret_cast<const unsigned char*>(bad_flatness),sizeof(bad_flatness)-1,
     8,8,pixels.data(),32))return 201;
 const char compatible[]="BX 42 FutureOperator EX 0 g 2 2 4 4 re f";
 if(!rinpdf::renderer::render_rectangles(
     reinterpret_cast<const unsigned char*>(compatible),sizeof(compatible)-1,
     8,8,pixels.data(),32))return 202;
 const char unclosed[]="BX 42 FutureOperator";
 if(rinpdf::renderer::render_rectangles(
     reinterpret_cast<const unsigned char*>(unclosed),sizeof(unclosed)-1,
     8,8,pixels.data(),32))return 203;
 const char text_state[]="BT 12 TL 10 20 Td 1 0 0 1 15 25 Tm T* ET";
 if(!rinpdf::renderer::render_rectangles(
    reinterpret_cast<const unsigned char*>(text_state),sizeof(text_state)-1,
    8,8,pixels.data(),32))return 204;
 const char nested_text[]="BT BT ET";
 if(rinpdf::renderer::render_rectangles(
    reinterpret_cast<const unsigned char*>(nested_text),sizeof(nested_text)-1,
    8,8,pixels.data(),32))return 205;
 const char missing_et[]="BT";
 if(rinpdf::renderer::render_rectangles(
    reinterpret_cast<const unsigned char*>(missing_et),sizeof(missing_et)-1,
    8,8,pixels.data(),32))return 206;
 const char compat_strings[]="BX (q Q [1 2] /Font) <4254> /F1 UnknownOperator EX 0 g 2 2 4 4 re f";
 if(!rinpdf::renderer::render_rectangles(
     reinterpret_cast<const unsigned char*>(compat_strings),sizeof(compat_strings)-1,
     8,8,pixels.data(),32))return 207;
 const char incomplete_literal[]="BX (unterminated";
 if(rinpdf::renderer::render_rectangles(
     reinterpret_cast<const unsigned char*>(incomplete_literal),sizeof(incomplete_literal)-1,
     8,8,pixels.data(),32))return 208;
 const char text_operators_in_string[]="(0 g 0 0 8 8 re f)";
 if(rinpdf::renderer::render_rectangles(
     reinterpret_cast<const unsigned char*>(text_operators_in_string),
     sizeof(text_operators_in_string)-1,8,8,pixels.data(),32))return 209;
 const rinpdf::renderer::FontResources diagnostic{{"FD",{"RinPDF-Diagnostic","Type1"}}};
 const char sample_glyphs[]="BT /FD 8 Tf 1 0 0 1 0 0 Tm (HI!) Tj ET";
 std::vector<unsigned char> glyph_pixels(64*64*4,0);
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
    reinterpret_cast<const unsigned char*>(sample_glyphs),sizeof(sample_glyphs)-1,
    64,64,glyph_pixels.data(),64*4,{0,0,64,64},diagnostic))return 213;
 bool has_ink=false;
 for(unsigned y=55;y<64;++y)for(unsigned x=0;x<20;++x)
  if(glyph_pixels[(y*64+x)*4]<255)has_ink=true;
 if(!has_ink)return 214;
 const char multiline[]="BT /FD 8 Tf 1 0 0 1 1 26 Tm (H) Tj 10 TL T* (I) Tj ET";
 std::vector<unsigned char> lines(64*64*4,255);
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
    reinterpret_cast<const unsigned char*>(multiline),sizeof(multiline)-1,
    64,64,lines.data(),64*4,{0,0,64,64},diagnostic))return 216;
 bool first_line=false,second_line=false;
 for(unsigned y=31;y<40;++y)for(unsigned x=0;x<5;++x)
  if(lines[(y*64+x)*4]<255)first_line=true;
 for(unsigned y=41;y<50;++y)for(unsigned x=0;x<5;++x)
  if(lines[(y*64+x)*4]<255)second_line=true;
 if(!first_line||!second_line)return 217;
 const char unknown_glyph[]="BT /FD 8 Tf (Z) Tj ET";
 if(rinpdf::renderer::render_rectangles_box_with_fonts(
    reinterpret_cast<const unsigned char*>(unknown_glyph),sizeof(unknown_glyph)-1,
    64,64,glyph_pixels.data(),64*4,{0,0,64,64},diagnostic))return 215;
 const char selected[]="BT /F1 12 Tf ET";
 const rinpdf::renderer::FontResources fonts{{"F1",{"Helvetica","Type1"}}};
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
     reinterpret_cast<const unsigned char*>(selected),sizeof(selected)-1,
     8,8,pixels.data(),32,{0,0,8,8},fonts))return 210;
 const char unknown_font[]="BT /F2 12 Tf ET";
 if(rinpdf::renderer::render_rectangles_box_with_fonts(
     reinterpret_cast<const unsigned char*>(unknown_font),sizeof(unknown_font)-1,
     8,8,pixels.data(),32,{0,0,8,8},fonts))return 211;
 const char invalid_font_size[]="BT /F1 -12 Tf ET";
 if(rinpdf::renderer::render_rectangles_box_with_fonts(
     reinterpret_cast<const unsigned char*>(invalid_font_size),sizeof(invalid_font_size)-1,
     8,8,pixels.data(),32,{0,0,8,8},fonts))return 212;
 const char unsupported[]="BT /F1 12 Tf ET";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(unsupported),sizeof(unsupported)-1,8,8,pixels.data(),32))return 3;
 const char rgb[]="q 1 0 0 rg 1 1 2 2 re f Q 0 g 4 4 2 2 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(rgb),sizeof(rgb)-1,8,8,pixels.data(),32))return 4;
 auto channel=[&](unsigned x,unsigned y,unsigned k){return pixels[(y*8+x)*4+k];};
 if(channel(1,5,0)!=255||channel(1,5,1)!=0||channel(1,5,2)!=0)return 5;
 if(channel(4,2,0)!=0||channel(4,2,1)!=0||channel(4,2,2)!=0)return 6;
 const char invalid[]="Q";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(invalid),sizeof(invalid)-1,8,8,pixels.data(),32))return 7;
 const char cmyk[]="0 1 1 0 k 1 1 2 2 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(cmyk),sizeof(cmyk)-1,8,8,pixels.data(),32))return 8;
 if(channel(1,5,0)!=255||channel(1,5,1)!=0||channel(1,5,2)!=0)return 9;
 const char invalid_cmyk[]="0 0 0 2 k";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(invalid_cmyk),sizeof(invalid_cmyk)-1,8,8,pixels.data(),32))return 10;
 const char scaled[]="0 g 100 200 20 20 re f";
 if(!rinpdf::renderer::render_rectangles_box(reinterpret_cast<const unsigned char*>(scaled),sizeof(scaled)-1,8,8,pixels.data(),32,{100,200,140,240}))return 11;
 if(channel(1,5,0)!=0||channel(6,6,0)!=255)return 12;
 if(rinpdf::renderer::render_rectangles_box(reinterpret_cast<const unsigned char*>(scaled),sizeof(scaled)-1,8,8,pixels.data(),32,{0,0,0,10}))return 13;
 const char transform[]="q 2 0 0 2 1 1 cm 0 g 1 1 2 2 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(transform),sizeof(transform)-1,8,8,pixels.data(),32))return 14;
 if(channel(4,3,0)!=0||channel(0,0,0)!=255)return 15;
 const char shear[]="1 1 0 1 0 0 cm 0 g 1 1 2 2 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(shear),sizeof(shear)-1,8,8,pixels.data(),32))return 16;
 const char triangle[]="0 g 1 1 m 7 1 l 4 7 l h f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(triangle),sizeof(triangle)-1,8,8,pixels.data(),32))return 17;
 if(channel(4,3,0)!=0||channel(0,0,0)!=255)return 18;
 const char broken[]="0 0 l f";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(broken),sizeof(broken)-1,8,8,pixels.data(),32))return 19;
 const char cubic[]="0 g 1 1 m 1 6 6 6 6 1 c h f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(cubic),sizeof(cubic)-1,8,8,pixels.data(),32))return 20;
 if(channel(3,4,0)!=0||channel(0,0,0)!=255)return 21;
 const char shorthand_v[]="0 g 1 1 m 4 7 7 1 v h f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(shorthand_v),sizeof(shorthand_v)-1,8,8,pixels.data(),32))return 22;
 const char shorthand_y[]="0 g 1 1 m 4 7 7 1 y h f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(shorthand_y),sizeof(shorthand_y)-1,8,8,pixels.data(),32))return 23;
 const char malformed_curve[]="0 0 m 1 2 3 c";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(malformed_curve),sizeof(malformed_curve)-1,8,8,pixels.data(),32))return 24;
 const char clipped[]="0 g -100 -100 m 4 2 l 4 6 l h f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(clipped),sizeof(clipped)-1,8,8,pixels.data(),32))return 25;
 std::vector<unsigned char> large(2048*1024*4);
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(triangle),sizeof(triangle)-1,2048,1024,large.data(),2048*4))return 26;
 const char stroke[]="1 w 0 g 1 1 m 6 1 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(stroke),sizeof(stroke)-1,8,8,pixels.data(),32))return 27;
 if(channel(3,6,0)>=255||channel(3,3,0)!=255)return 28;
 const char invalid_width[]="-1 w";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(invalid_width),sizeof(invalid_width)-1,8,8,pixels.data(),32))return 29;
 const char colors[]="1 0 0 rg 0 0 1 RG 2 w 1 1 m 6 1 l S 1 4 2 2 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(colors),sizeof(colors)-1,8,8,pixels.data(),32))return 30;
 if(channel(3,6,0)>=255||channel(3,6,2)!=255)return 31;
 if(channel(1,2,0)!=255||channel(1,2,2)!=0)return 32;
 const char state[]="q 3 w 1 0 0 RG Q 0 g 1 1 m 6 1 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(state),sizeof(state)-1,8,8,pixels.data(),32))return 33;
 const char round[]="1 J 1 j 10 M 2 w 1 1 m 6 1 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(round),sizeof(round)-1,8,8,pixels.data(),32))return 34;
 const char butt[]="0 J 2 w 0 G 2 3 m 5 3 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(butt),sizeof(butt)-1,8,8,pixels.data(),32))return 35;
 const char square[]="2 J 2 w 0 G 2 3 m 5 3 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(square),sizeof(square)-1,8,8,pixels.data(),32))return 38;
 const char invalid_cap[]="3 J";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(invalid_cap),sizeof(invalid_cap)-1,8,8,pixels.data(),32))return 39;
 const char bevel_join[]="2 j 0 G 1 w 1 1 m 6 1 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(bevel_join),sizeof(bevel_join)-1,8,8,pixels.data(),32))return 36;
 const char invalid_join[]="3 j";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(invalid_join),sizeof(invalid_join)-1,8,8,pixels.data(),32))return 93;
 const char invalid_miter[]="0 M";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(invalid_miter),sizeof(invalid_miter)-1,8,8,pixels.data(),32))return 37;
 std::string excessive="1 J 1 j 1 w 0 G ";
 excessive+="0 0 m ";
 for(int i=0;i<300;++i)excessive+=std::to_string(i%8)+" 1 l ";
 excessive+="S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(excessive.data()),excessive.size(),256,256,large.data(),256*4))return 40;
 const char offscreen[]="1 J 1 w 0 G -1000 -1000 m -900 -900 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(offscreen),sizeof(offscreen)-1,8,8,pixels.data(),32))return 41;
 const char evenodd[]="0 g 1 1 m 7 1 l 4 7 l h f*";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(evenodd),sizeof(evenodd)-1,8,8,pixels.data(),32))return 42;
 const char compound[]="0 g 1 1 m 3 1 l 2 3 l h 4 4 m 6 4 l 5 6 l h f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(compound),sizeof(compound)-1,8,8,pixels.data(),32))return 43;
 const char hole[]="0 g 0 0 m 8 0 l 8 8 l 0 8 l h 2 2 m 6 2 l 6 6 l 2 6 l h f*";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(hole),sizeof(hole)-1,8,8,pixels.data(),32))return 44;
 if(channel(0,0,0)!=0||channel(3,3,0)!=255)return 45;
 const char nonzero[]="0 g 0 0 m 8 0 l 8 8 l 0 8 l h 2 2 m 6 2 l 6 6 l 2 6 l h f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(nonzero),sizeof(nonzero)-1,8,8,pixels.data(),32))return 46;
 if(channel(3,3,0)!=0)return 47;
 const char unsupported_clip[]="0 0 4 4 re W n";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(unsupported_clip),sizeof(unsupported_clip)-1,8,8,pixels.data(),32))return 48;
 const char unsupported_clip_evenodd[]="0 0 4 4 re W* n";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(unsupported_clip_evenodd),sizeof(unsupported_clip_evenodd)-1,8,8,pixels.data(),32))return 49;
 const char clip_fill[]="q 2 2 3 3 re W n 0 g 0 0 8 8 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(clip_fill),sizeof(clip_fill)-1,8,8,pixels.data(),32))return 50;
 if(channel(0,0,0)!=255||channel(3,3,0)!=0||channel(7,7,0)!=255)return 51;
 const char clip_restore[]="q 2 2 3 3 re W n Q 0 g 0 0 8 8 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(clip_restore),sizeof(clip_restore)-1,8,8,pixels.data(),32))return 52;
 if(channel(0,0,0)!=0)return 53;
 const char bad_clip[]="0 0 m 4 0 l 4 4 l h W n";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(bad_clip),sizeof(bad_clip)-1,8,8,pixels.data(),32))return 54;
 const char evenodd_clip_fill[]="q 2 2 3 3 re W* n 0 g 0 0 8 8 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(evenodd_clip_fill),sizeof(evenodd_clip_fill)-1,8,8,pixels.data(),32))return 55;
 if(channel(0,0,0)!=255||channel(3,3,0)!=0)return 56;
 const char pending_state[]="0 0 4 4 re W q n";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(pending_state),sizeof(pending_state)-1,8,8,pixels.data(),32))return 57;
 const char modified_clip[]="0 0 4 4 re W 1 1 2 2 re n";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(modified_clip),sizeof(modified_clip)-1,8,8,pixels.data(),32))return 58;
 const char nested_clip[]="q 1 1 6 6 re W n q 2 2 2 2 re W* n 0 g 0 0 8 8 re f Q 0 0 8 8 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(nested_clip),sizeof(nested_clip)-1,8,8,pixels.data(),32))return 59;
 if(channel(0,0,0)!=255||channel(1,1,0)!=0||channel(3,3,0)!=0)return 60;
 const char empty_clip[]="q 20 20 4 4 re W n 0 g 0 0 8 8 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(empty_clip),sizeof(empty_clip)-1,8,8,pixels.data(),32))return 61;
 if(channel(3,3,0)!=255)return 62;
 const char triangle_clip[]="q 1 1 m 7 1 l 4 7 l h W n 0 g 0 0 8 8 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(triangle_clip),sizeof(triangle_clip)-1,8,8,pixels.data(),32))return 63;
 if(channel(4,3,0)!=0||channel(0,0,0)!=255)return 64;
 const char hole_clip[]="q 0 0 m 8 0 l 8 8 l 0 8 l h 2 2 m 6 2 l 6 6 l 2 6 l h W* n 0 g 0 0 8 8 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(hole_clip),sizeof(hole_clip)-1,8,8,pixels.data(),32))return 65;
 if(channel(0,0,0)!=0||channel(3,3,0)!=255)return 66;
 const char restore_path_clip[]="q 1 1 m 7 1 l 4 7 l h W n Q 0 g 0 0 8 8 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(restore_path_clip),sizeof(restore_path_clip)-1,8,8,pixels.data(),32))return 67;
 if(channel(0,0,0)!=0)return 68;
 const char pending_path_mutation[]="1 1 m 7 1 l 4 7 l h W 0 0 m n";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(pending_path_mutation),sizeof(pending_path_mutation)-1,8,8,pixels.data(),32))return 69;
 const char empty_path_clip[]="q W n 0 g 0 0 8 8 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(empty_path_clip),sizeof(empty_path_clip)-1,8,8,pixels.data(),32))return 74;
 if(channel(3,3,0)!=255)return 75;
 const char empty_evenodd_clip[]="q W* n 0 g 0 0 8 8 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(empty_evenodd_clip),sizeof(empty_evenodd_clip)-1,8,8,pixels.data(),32))return 76;
 if(channel(3,3,0)!=255)return 77;
 const char move_only_clip[]="q 3 3 m W n 0 g 0 0 8 8 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(move_only_clip),sizeof(move_only_clip)-1,8,8,pixels.data(),32))return 78;
 if(channel(3,3,0)!=255)return 79;
 const char restored_empty_clip[]="q W n Q 0 g 0 0 8 8 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(restored_empty_clip),sizeof(restored_empty_clip)-1,8,8,pixels.data(),32))return 80;
 if(channel(3,3,0)!=0)return 81;
 const char rotated_rect[]="0 1 -1 0 8 0 cm 0 g 1 1 2 2 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(rotated_rect),sizeof(rotated_rect)-1,8,8,pixels.data(),32))return 82;
 if(channel(5,5,0)!=0||channel(1,1,0)!=255)return 83;
 const char rotated_stroke[]="0 1 -1 0 8 0 cm 1 1 m 3 3 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(rotated_stroke),sizeof(rotated_stroke)-1,8,8,pixels.data(),32))return 84;
 const char singular_stroke[]="1 0 0 0 0 0 cm 0 0 m 4 0 l S";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(singular_stroke),sizeof(singular_stroke)-1,8,8,pixels.data(),32))return 85;
 const char mixed_ctm_fill[]="0 g 1 1 m 2 0 0 2 0 0 cm 3 1 l 3 3 l h f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(mixed_ctm_fill),sizeof(mixed_ctm_fill)-1,8,8,pixels.data(),32))return 86;
 const char mixed_ctm_stroke[]="1 1 m 2 0 0 2 0 0 cm 3 1 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(mixed_ctm_stroke),sizeof(mixed_ctm_stroke)-1,8,8,pixels.data(),32))return 87;
 const char reset_ctm[]="1 1 m 2 0 0 2 0 0 cm 3 1 l n 0 g 1 1 2 2 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(reset_ctm),sizeof(reset_ctm)-1,8,8,pixels.data(),32))return 88;
 const char path_across_q[]="0 g 1 1 m q 2 0 0 2 0 0 cm 3 1 l Q 3 3 l h f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(path_across_q),sizeof(path_across_q)-1,8,8,pixels.data(),32))return 89;
 const char path_across_q_stroke[]="1 1 m q 2 0 0 2 0 0 cm 3 1 l Q S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(path_across_q_stroke),sizeof(path_across_q_stroke)-1,8,8,pixels.data(),32))return 90;
 const char rotated_rect_stroke[]="0 1 -1 0 8 0 cm 0 G 1 w 1 1 4 4 re S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(rotated_rect_stroke),sizeof(rotated_rect_stroke)-1,8,8,pixels.data(),32))return 91;
 if(channel(3,4,0)==255||channel(5,4,0)!=255)return 92;
 const char round_corner[]="1 j 0 J 2 w 0 G 2 2 m 4 2 l 4 4 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(round_corner),sizeof(round_corner)-1,8,8,pixels.data(),32))return 94;
 if(channel(4,5,0)==255)return 95;
 const char default_join[]="0 G 1 w 1 1 m 5 1 l S";
 const char explicit_miter[]="0 j 0 G 1 w 1 1 m 5 1 l S";
 std::vector<unsigned char> first(8*8*4),second(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(default_join),sizeof(default_join)-1,8,8,first.data(),32))return 96;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(explicit_miter),sizeof(explicit_miter)-1,8,8,second.data(),32))return 97;
 if(first!=second)return 98;
 const char round_rect[]="1 j 0 J 2 w 0 G 2 2 3 3 re S";
 const char round_poly[]="1 j 0 J 2 w 0 G 2 2 m 5 2 l 5 5 l 2 5 l h S";
 std::vector<unsigned char> rect_join_pixels(8*8*4),poly_join_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(round_rect),sizeof(round_rect)-1,8,8,rect_join_pixels.data(),32))return 99;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(round_poly),sizeof(round_poly)-1,8,8,poly_join_pixels.data(),32))return 100;
 if(rect_join_pixels!=poly_join_pixels)return 101;
 const char close_with_s[]="0 G 1 w 1 1 m 5 1 l 2 0 0 2 0 0 cm 5 5 l s";
 const char close_with_h[]="0 G 1 w 1 1 m 5 1 l 2 0 0 2 0 0 cm 5 5 l h S";
 std::vector<unsigned char> s_pixels(8*8*4),h_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(close_with_s),sizeof(close_with_s)-1,8,8,s_pixels.data(),32))return 102;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(close_with_h),sizeof(close_with_h)-1,8,8,h_pixels.data(),32))return 103;
 if(s_pixels!=h_pixels)return 104;
 const char once_closed[]="1 j 0 G 1 w 1 1 m 5 1 l 5 5 l h S";
 const char twice_closed[]="1 j 0 G 1 w 1 1 m 5 1 l 5 5 l h h S";
 std::vector<unsigned char> once_pixels(8*8*4),twice_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(once_closed),sizeof(once_closed)-1,8,8,once_pixels.data(),32))return 105;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(twice_closed),sizeof(twice_closed)-1,8,8,twice_pixels.data(),32))return 106;
 if(once_pixels!=twice_pixels)return 107;
 const char empty_stroke[]="0 G S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(empty_stroke),sizeof(empty_stroke)-1,8,8,pixels.data(),32))return 108;
 if(channel(3,3,0)!=255)return 109;
 const char move_only_stroke[]="1 1 m S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(move_only_stroke),sizeof(move_only_stroke)-1,8,8,pixels.data(),32))return 110;
 if(channel(3,3,0)!=255)return 111;
 const char trailing_move_s[]="0 G 1 w 1 1 m 5 1 l 2 2 m s";
 const char trailing_move_hs[]="0 G 1 w 1 1 m 5 1 l 2 2 m S";
 std::vector<unsigned char> trailing_s_pixels(8*8*4),trailing_hs_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(trailing_move_s),sizeof(trailing_move_s)-1,8,8,trailing_s_pixels.data(),32))return 112;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(trailing_move_hs),sizeof(trailing_move_hs)-1,8,8,trailing_hs_pixels.data(),32))return 113;
 if(trailing_s_pixels!=trailing_hs_pixels)return 114;
 const char zero_butt[]="0 J 0 G 2 w 3 3 m 3 3 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(zero_butt),sizeof(zero_butt)-1,8,8,pixels.data(),32))return 115;
 if(channel(3,4,0)!=255)return 116;
 const char zero_square[]="2 J 0 G 2 w 3 3 m 3 3 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(zero_square),sizeof(zero_square)-1,8,8,pixels.data(),32))return 117;
 if(channel(3,4,0)==255)return 118;
 const char zero_round[]="1 J 0 G 2 w 3 3 m 3 3 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(zero_round),sizeof(zero_round)-1,8,8,pixels.data(),32))return 119;
 if(channel(3,4,0)==255)return 120;
 const char move_only_close[]="1 1 m h S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(move_only_close),sizeof(move_only_close)-1,8,8,pixels.data(),32))return 121;
 if(channel(3,3,0)!=255)return 122;
 const char trailing_move_close[]="0 G 1 w 1 1 m 5 1 l 2 2 m h S";
 const char trailing_move_plain[]="0 G 1 w 1 1 m 5 1 l 2 2 m S";
 std::vector<unsigned char> closed_pixels(8*8*4),plain_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(trailing_move_close),sizeof(trailing_move_close)-1,8,8,closed_pixels.data(),32))return 123;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(trailing_move_plain),sizeof(trailing_move_plain)-1,8,8,plain_pixels.data(),32))return 124;
 if(closed_pixels!=plain_pixels)return 125;
 std::string full_closed="0 G 1 w 1 1 m ";
 for(int i=0;i<4094;++i)full_closed+="1 1 l ";
 full_closed+="h ";
 const std::string full_closed_s=full_closed+"s";
 const std::string full_closed_S=full_closed+"S";
 std::vector<unsigned char> full_s_pixels(8*8*4),full_S_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(full_closed_s.data()),full_closed_s.size(),8,8,full_s_pixels.data(),32))return 126;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(full_closed_S.data()),full_closed_S.size(),8,8,full_S_pixels.data(),32))return 127;
 if(full_s_pixels!=full_S_pixels)return 128;
 const char bevel_corner[]="2 j 0 J 3 w 0 G 2 2 m 5 2 l 5 5 l S";
 const char miter_corner[]="0 j 0 J 3 w 0 G 2 2 m 5 2 l 5 5 l S";
 std::vector<unsigned char> bevel_pixels(8*8*4),miter_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(bevel_corner),sizeof(bevel_corner)-1,8,8,bevel_pixels.data(),32))return 129;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(miter_corner),sizeof(miter_corner)-1,8,8,miter_pixels.data(),32))return 130;
 const char invalid_join_ctm[]="1 1 0 1 0 0 cm 0 j 2 w 1 1 m 5 1 l 5 5 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(invalid_join_ctm),sizeof(invalid_join_ctm)-1,8,8,pixels.data(),32))return 131;
 bool shear_painted=false;
 for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]<255){shear_painted=true;break;}
 if(!shear_painted)return 227;
 const char dash_basic[]="[2 2] 0 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_basic),sizeof(dash_basic)-1,8,8,pixels.data(),32))return 132;
 if(channel(1,3,0)==255||channel(3,3,0)!=255||channel(5,3,0)==255||channel(7,3,0)!=255)return 133;
 const char dash_offset[]="[2 2] 2 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_offset),sizeof(dash_offset)-1,8,8,pixels.data(),32))return 134;
 if(channel(1,3,0)!=255||channel(3,3,0)==255)return 135;
 const char bad_dash[]="[0 0] 0 d";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(bad_dash),sizeof(bad_dash)-1,8,8,pixels.data(),32))return 136;
 const char solid_dash[]="[] 0 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(solid_dash),sizeof(solid_dash)-1,8,8,pixels.data(),32))return 137;
 const char dash_even_phase[]="[2 2] 6 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 std::vector<unsigned char> dash_phase_pixels(8*8*4),dash_even_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_even_phase),sizeof(dash_even_phase)-1,8,8,dash_phase_pixels.data(),32))return 138;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_offset),sizeof(dash_offset)-1,8,8,dash_even_pixels.data(),32))return 139;
 if(dash_phase_pixels!=dash_even_pixels)return 140;
 const char dash_zero_on[]="[0 2] 0 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_zero_on),sizeof(dash_zero_on)-1,8,8,pixels.data(),32))return 141;
 if(channel(1,3,0)!=255||channel(5,3,0)!=255)return 142;
 const char dash_odd[]="[2 1 1] 0 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_odd),sizeof(dash_odd)-1,8,8,pixels.data(),32))return 143;
 const char dash_odd_shift[]="[2 1 1] 8 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 std::vector<unsigned char> odd0(8*8*4),odd8(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_odd),sizeof(dash_odd)-1,8,8,odd0.data(),32))return 144;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_odd_shift),sizeof(dash_odd_shift)-1,8,8,odd8.data(),32))return 145;
 if(odd0!=odd8)return 146;
 const char dash_restored[]="q [2 2] 0 d q [1 3] 0 d Q 0 G 1 w 0 J 0 4 m 8 4 l S Q";
 std::vector<unsigned char> restored_dash_pixels(8*8*4),baseline_dash_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_restored),sizeof(dash_restored)-1,8,8,restored_dash_pixels.data(),32))return 147;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_basic),sizeof(dash_basic)-1,8,8,baseline_dash_pixels.data(),32))return 148;
 if(restored_dash_pixels!=baseline_dash_pixels)return 149;
 const char dash_compact[]="[2 2]0 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_compact),sizeof(dash_compact)-1,8,8,pixels.data(),32))return 150;
 if(channel(1,3,0)==255||channel(3,3,0)!=255)return 151;
 const char miter_wedge[]="0 j 10 M 0 J 4 w 0 G 4 4 m 10 4 l 10 10 l S";
 const char bevel_wedge[]="2 j 0 J 4 w 0 G 4 4 m 10 4 l 10 10 l S";
 const char limited_miter[]="0 j 1 M 0 J 4 w 0 G 4 4 m 10 4 l 10 10 l S";
 std::vector<unsigned char> mw(16*16*4),bw(16*16*4),lw(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(miter_wedge),sizeof(miter_wedge)-1,16,16,mw.data(),64))return 152;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(bevel_wedge),sizeof(bevel_wedge)-1,16,16,bw.data(),64))return 153;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(limited_miter),sizeof(limited_miter)-1,16,16,lw.data(),64))return 154;
 if(mw==bw)return 155;
 if(lw!=bw)return 156;
 const char closed_miter[]="0 j 0 J 3 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 const char closed_bevel[]="2 j 0 J 3 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> cm_pixels(16*16*4),cb_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_miter),sizeof(closed_miter)-1,16,16,cm_pixels.data(),64))return 157;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_bevel),sizeof(closed_bevel)-1,16,16,cb_pixels.data(),64))return 158;
 if(cm_pixels==cb_pixels)return 159;
 const char rect_miter[]="0 j 0 J 3 w 0 G 3 3 8 8 re S";
 const char poly_miter[]="0 j 0 J 3 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 const char rect_bevel[]="2 j 0 J 3 w 0 G 3 3 8 8 re S";
 const char poly_bevel[]="2 j 0 J 3 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> rm(16*16*4),pm(16*16*4),rb(16*16*4),pb(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(rect_miter),sizeof(rect_miter)-1,16,16,rm.data(),64))return 160;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(poly_miter),sizeof(poly_miter)-1,16,16,pm.data(),64))return 161;
 if(rm!=pm)return 162;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(rect_bevel),sizeof(rect_bevel)-1,16,16,rb.data(),64))return 163;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(poly_bevel),sizeof(poly_bevel)-1,16,16,pb.data(),64))return 164;
 if(rb!=pb)return 165;
 const char dash_butt_cap[]="[2 2] 0 d 0 J 0 G 2 w 2 4 m 10 4 l S";
 const char dash_round_cap[]="[2 2] 0 d 1 J 0 G 2 w 2 4 m 10 4 l S";
 std::vector<unsigned char> butt_dash(16*16*4),round_dash(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_butt_cap),sizeof(dash_butt_cap)-1,16,16,butt_dash.data(),64))return 166;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_round_cap),sizeof(dash_round_cap)-1,16,16,round_dash.data(),64))return 167;
 if(butt_dash==round_dash)return 168;
 const char dash_round_corner[]="[20 2] 0 d 1 j 0 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 const char solid_round_corner[]="1 j 0 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 std::vector<unsigned char> dashed_corner(16*16*4),solid_corner(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_round_corner),sizeof(dash_round_corner)-1,16,16,dashed_corner.data(),64))return 169;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(solid_round_corner),sizeof(solid_round_corner)-1,16,16,solid_corner.data(),64))return 170;
 if(dashed_corner!=solid_corner)return 171;
 const char boundary_phase[]="[2 2] 2 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 const char equivalent_phase[]="[2 2] 6 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 std::vector<unsigned char> phase_a(8*8*4),phase_b(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(boundary_phase),sizeof(boundary_phase)-1,8,8,phase_a.data(),32))return 172;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(equivalent_phase),sizeof(equivalent_phase)-1,8,8,phase_b.data(),32))return 173;
 if(phase_a!=phase_b)return 174;
 const char missing_dash_array[]="2 d";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(missing_dash_array),sizeof(missing_dash_array)-1,8,8,pixels.data(),32))return 175;
 const char malformed_dash_array[]="[2 q 2] 0 d";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(malformed_dash_array),sizeof(malformed_dash_array)-1,8,8,pixels.data(),32))return 176;
 const char unterminated_dash_array[]="[2 2";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(unterminated_dash_array),sizeof(unterminated_dash_array)-1,8,8,pixels.data(),32))return 177;
 const char no_dash_operator[]="[2 2] 0 w";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(no_dash_operator),sizeof(no_dash_operator)-1,8,8,pixels.data(),32))return 178;
 const char extra_dash_phase[]="[2 2] 0 1 d";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(extra_dash_phase),sizeof(extra_dash_phase)-1,8,8,pixels.data(),32))return 179;
 const char missing_dash_phase[]="[2 2] d";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(missing_dash_phase),sizeof(missing_dash_phase)-1,8,8,pixels.data(),32))return 180;
 const char scaled_dash[]="2 0 0 2 0 0 cm [2 2] 0 d 0 G 1 w 0 J 1 3 m 5 3 l S";
 const char device_dash[]="[4 4] 0 d 0 G 2 w 0 J 2 6 m 10 6 l S";
 std::vector<unsigned char> scaled_pixels(16*16*4),device_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(scaled_dash),sizeof(scaled_dash)-1,16,16,scaled_pixels.data(),64))return 181;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(device_dash),sizeof(device_dash)-1,16,16,device_pixels.data(),64))return 182;
 if(scaled_pixels!=device_pixels)return 183;
 const char dashed_miter_corner[]="[20 2] 0 d 0 j 0 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 const char solid_miter_corner[]="0 j 0 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 const char dashed_bevel_corner[]="[20 2] 0 d 2 j 0 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 const char solid_bevel_corner[]="2 j 0 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 std::vector<unsigned char> dmc(16*16*4),smc(16*16*4),dbc(16*16*4),sbc(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_miter_corner),sizeof(dashed_miter_corner)-1,16,16,dmc.data(),64))return 184;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(solid_miter_corner),sizeof(solid_miter_corner)-1,16,16,smc.data(),64))return 185;
 if(dmc!=smc)return 186;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_bevel_corner),sizeof(dashed_bevel_corner)-1,16,16,dbc.data(),64))return 187;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(solid_bevel_corner),sizeof(solid_bevel_corner)-1,16,16,sbc.data(),64))return 188;
 if(dbc!=sbc)return 189;
 const char rotated_miter[]="0 1 -1 0 16 0 cm 0 j 0 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 const char explicit_rotated_miter[]="0 j 0 J 2 w 0 G 14 2 m 14 8 l 8 8 l S";
 const char rotated_bevel[]="0 1 -1 0 16 0 cm 2 j 0 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 const char explicit_rotated_bevel[]="2 j 0 J 2 w 0 G 14 2 m 14 8 l 8 8 l S";
 std::vector<unsigned char> rot_a(16*16*4),rot_b(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(rotated_miter),sizeof(rotated_miter)-1,16,16,rot_a.data(),64))return 190;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(explicit_rotated_miter),sizeof(explicit_rotated_miter)-1,16,16,rot_b.data(),64))return 191;
 if(rot_a!=rot_b)return 192;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(rotated_bevel),sizeof(rotated_bevel)-1,16,16,rot_a.data(),64))return 193;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(explicit_rotated_bevel),sizeof(explicit_rotated_bevel)-1,16,16,rot_b.data(),64))return 194;
 if(rot_a!=rot_b)return 195;
 const char square_end[]="2 J 0 G 2 w 4 8 m 10 8 l S";
 const char round_end[]="1 J 0 G 2 w 4 8 m 10 8 l S";
 std::vector<unsigned char> square_pixels(16*16*4),round_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(square_end),sizeof(square_end)-1,16,16,square_pixels.data(),64))return 196;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(round_end),sizeof(round_end)-1,16,16,round_pixels.data(),64))return 197;
 if(square_pixels==round_pixels)return 198;
 std::string long_dash="[100000 1] 0 d 1 j 0 J 1 w 0 G 1 1 m ";
 std::vector<unsigned char> long_dash_pixels(8*8*4);
 for(int i=0;i<4095;++i){
  long_dash+=(i%2?"1 1 l ":"2 1 l ");
  // Probe representative sizes to identify the first failing path length.
  if(i==63||i==255||i==1023||i==2047||i==3071||i==4094){
   const std::string complete_path=long_dash+"S";
   if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(complete_path.data()),complete_path.size(),8,8,long_dash_pixels.data(),32)){
    std::fprintf(stderr,"long dashed round join failed at %d segments\n",i+1);
    return 199;
   }
  }
 }
 const char phase_odd_zero[]="[2 1 1] 0 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 const char phase_odd_cycle[]="[2 1 1] 800000 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 std::vector<unsigned char> phase_odd_a(8*8*4),phase_odd_b(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(phase_odd_zero),sizeof(phase_odd_zero)-1,8,8,phase_odd_a.data(),32))return 200;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(phase_odd_cycle),sizeof(phase_odd_cycle)-1,8,8,phase_odd_b.data(),32))return 201;
 if(phase_odd_a!=phase_odd_b)return 202;
 const char zero_first_dash[]="[0 2] 0 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 const char zero_first_shifted[]="[0 2] 2 d 0 G 1 w 0 J 0 4 m 8 4 l S";
 std::vector<unsigned char> zero_first_a(8*8*4),zero_first_b(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(zero_first_dash),sizeof(zero_first_dash)-1,8,8,zero_first_a.data(),32))return 203;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(zero_first_shifted),sizeof(zero_first_shifted)-1,8,8,zero_first_b.data(),32))return 204;
 if(zero_first_a!=zero_first_b)return 205;
 const char closed_butt_caps[]="1 j 0 J 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 const char closed_square_caps[]="1 j 2 J 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 const char closed_round_caps[]="1 j 1 J 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> closed_butt_pixels(16*16*4),closed_square_pixels(16*16*4),closed_round_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_butt_caps),sizeof(closed_butt_caps)-1,16,16,closed_butt_pixels.data(),64))return 206;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_square_caps),sizeof(closed_square_caps)-1,16,16,closed_square_pixels.data(),64))return 207;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_round_caps),sizeof(closed_round_caps)-1,16,16,closed_round_pixels.data(),64))return 208;
 if(closed_butt_pixels!=closed_square_pixels||closed_butt_pixels!=closed_round_pixels)return 209;
 const char round_open_corner[]="1 j 1 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 const char butt_open_corner[]="1 j 0 J 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 std::vector<unsigned char> round_open_pixels(16*16*4),butt_open_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(round_open_corner),sizeof(round_open_corner)-1,16,16,round_open_pixels.data(),64))return 210;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(butt_open_corner),sizeof(butt_open_corner)-1,16,16,butt_open_pixels.data(),64))return 211;
 if(round_open_pixels==butt_open_pixels)return 212;
 const char dashed_square_continuous[]="[20 2] 0 d 2 J 1 j 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 const char solid_square_continuous[]="2 J 1 j 2 w 0 G 2 2 m 8 2 l 8 8 l S";
 std::vector<unsigned char> dashed_square_pixels(16*16*4),solid_square_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_square_continuous),sizeof(dashed_square_continuous)-1,16,16,dashed_square_pixels.data(),64))return 213;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(solid_square_continuous),sizeof(solid_square_continuous)-1,16,16,solid_square_pixels.data(),64))return 214;
 if(dashed_square_pixels!=solid_square_pixels)return 215;
 const char dashed_square_gaps[]="[2 2] 0 d 2 J 0 G 2 w 2 8 m 14 8 l S";
 const char dashed_butt_gaps[]="[2 2] 0 d 0 J 0 G 2 w 2 8 m 14 8 l S";
 std::vector<unsigned char> square_gap_pixels(16*16*4),butt_gap_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_square_gaps),sizeof(dashed_square_gaps)-1,16,16,square_gap_pixels.data(),64))return 216;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_butt_gaps),sizeof(dashed_butt_gaps)-1,16,16,butt_gap_pixels.data(),64))return 217;
 if(square_gap_pixels==butt_gap_pixels)return 218;
 const char closed_full_dash[]="[100 2] 0 d 0 j 2 J 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 const char closed_solid_miter[]="0 j 2 J 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> full_dash_pixels(16*16*4),solid_miter_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_full_dash),sizeof(closed_full_dash)-1,16,16,full_dash_pixels.data(),64))return 219;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_solid_miter),sizeof(closed_solid_miter)-1,16,16,solid_miter_pixels.data(),64))return 220;
 if(full_dash_pixels!=solid_miter_pixels)return 221;
 const char closed_exact_dash[]="[32 2] 0 d 0 j 2 J 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> exact_dash_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_exact_dash),sizeof(closed_exact_dash)-1,16,16,exact_dash_pixels.data(),64))return 222;
 if(exact_dash_pixels!=solid_miter_pixels)return 223;
 const char diagonal_square[]="2 J 0 G 6 w 8 8 m 12 12 l S";
 const char diagonal_butt[]="0 J 0 G 6 w 8 8 m 12 12 l S";
 std::vector<unsigned char> diagonal_square_pixels(20*20*4),diagonal_butt_pixels(20*20*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(diagonal_square),sizeof(diagonal_square)-1,20,20,diagonal_square_pixels.data(),80))return 224;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(diagonal_butt),sizeof(diagonal_butt)-1,20,20,diagonal_butt_pixels.data(),80))return 225;
 if(diagonal_square_pixels==diagonal_butt_pixels)return 226;
 // The extra square-cap footprint must include pixels that butt caps
 // leave untouched; checking inequality alone could hide a regression.
 bool square_extension=false;
 for(size_t i=0;i<diagonal_square_pixels.size();i+=4){
  if(diagonal_square_pixels[i]<255&&diagonal_butt_pixels[i]==255){
   square_extension=true;
   break;
  }
 }
 if(!square_extension)return 227;
 const char closed_multi_dash[]="[7 3] 2 d 2 J 1 j 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> closed_multi_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_multi_dash),sizeof(closed_multi_dash)-1,16,16,closed_multi_pixels.data(),64))return 228;
 bool closed_multi_ink=false;
 for(size_t i=0;i<closed_multi_pixels.size();i+=4)if(closed_multi_pixels[i]<255){closed_multi_ink=true;break;}
 if(!closed_multi_ink)return 229;
 const char closed_round_seam[]="[7 3] 2 d 1 J 1 j 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 const char closed_miter_seam[]="[7 3] 2 d 2 J 0 j 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> seam_round_pixels(16*16*4),seam_miter_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_round_seam),sizeof(closed_round_seam)-1,16,16,seam_round_pixels.data(),64))return 230;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_miter_seam),sizeof(closed_miter_seam)-1,16,16,seam_miter_pixels.data(),64))return 231;
 const char closed_zero_perimeter[]="[2 2] 0 d 1 j 1 J 2 w 0 G 4 4 m 4 4 l 4 4 l h S";
 std::vector<unsigned char> zero_perimeter_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_zero_perimeter),sizeof(closed_zero_perimeter)-1,8,8,zero_perimeter_pixels.data(),32))return 232;
 const char closed_seam_gap[]="[2 2] 2 d 0 j 2 J 2 w 0 G 2 2 m 6 2 l 6 6 l 2 6 l h S";
 std::vector<unsigned char> seam_gap_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_seam_gap),sizeof(closed_seam_gap)-1,8,8,seam_gap_pixels.data(),32))return 233;
 const char closed_seam_round[]="[7 3] 2 d 1 j 1 J 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 const char closed_seam_bevel[]="[7 3] 2 d 2 j 1 J 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> closed_seam_round_pixels(16*16*4),closed_seam_bevel_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_seam_round),sizeof(closed_seam_round)-1,16,16,closed_seam_round_pixels.data(),64))return 234;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_seam_bevel),sizeof(closed_seam_bevel)-1,16,16,closed_seam_bevel_pixels.data(),64))return 235;
 const char closed_repeated_seam[]="[7 3] 2 d 1 j 1 J 2 w 0 G 3 3 m 3 3 l 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> repeated_seam_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(closed_repeated_seam),sizeof(closed_repeated_seam)-1,16,16,repeated_seam_pixels.data(),64))return 236;
 const char repeated_closed_miter[]="[7 3] 2 d 0 j 2 J 2 w 0 G 3 3 m 3 3 l 11 3 l 11 11 l 3 11 l h S";
 const char ordinary_closed_miter[]="[7 3] 2 d 0 j 2 J 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> repeated_miter_pixels(16*16*4),ordinary_miter_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(repeated_closed_miter),sizeof(repeated_closed_miter)-1,16,16,repeated_miter_pixels.data(),64))return 237;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(ordinary_closed_miter),sizeof(ordinary_closed_miter)-1,16,16,ordinary_miter_pixels.data(),64))return 238;
 if(repeated_miter_pixels!=ordinary_miter_pixels)return 239;
 const char small_miter[]="0 j 10 M 0 J 0 G 0.1 w 2 2 m 2.01 2 l 2.01 2.01 l S";
 const char large_miter[]="0 j 10 M 0 J 0 G 1 w 2 2 m 2.1 2 l 2.1 2.1 l S";
 std::vector<unsigned char> small_miter_pixels(16*16*4),large_miter_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(small_miter),sizeof(small_miter)-1,16,16,small_miter_pixels.data(),64))return 240;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(large_miter),sizeof(large_miter)-1,16,16,large_miter_pixels.data(),64))return 241;
 const char fallback_limited_miter[]="0 j 1 M 0 J 4 w 0 G 3 3 m 10 3 l 10 10 l S";
 const char matching_bevel[]="2 j 1 M 0 J 4 w 0 G 3 3 m 10 3 l 10 10 l S";
 std::vector<unsigned char> limited_miter_pixels(16*16*4),matching_bevel_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(fallback_limited_miter),sizeof(fallback_limited_miter)-1,16,16,limited_miter_pixels.data(),64))return 242;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(matching_bevel),sizeof(matching_bevel)-1,16,16,matching_bevel_pixels.data(),64))return 243;
 if(limited_miter_pixels!=matching_bevel_pixels)return 244;
 const char scaled_limited_miter[]="3 0 0 1 0 0 cm 0 j 1 M 0 J 2 w 0 G 2 2 m 5 2 l 5 7 l S";
 const char scaled_bevel[]="3 0 0 1 0 0 cm 2 j 1 M 0 J 2 w 0 G 2 2 m 5 2 l 5 7 l S";
 std::vector<unsigned char> scaled_limit_pixels(32*16*4),scaled_bevel_pixels(32*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(scaled_limited_miter),sizeof(scaled_limited_miter)-1,32,16,scaled_limit_pixels.data(),128))return 245;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(scaled_bevel),sizeof(scaled_bevel)-1,32,16,scaled_bevel_pixels.data(),128))return 246;
 if(scaled_limit_pixels!=scaled_bevel_pixels)return 247;
 const char angle_limited_miter[]="0 j 1.4 M 0 J 4 w 0 G 3 3 m 10 3 l 10 10 l S";
 const char angle_bevel[]="2 j 1.4 M 0 J 4 w 0 G 3 3 m 10 3 l 10 10 l S";
 std::vector<unsigned char> angle_limited_pixels(16*16*4),angle_bevel_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(angle_limited_miter),sizeof(angle_limited_miter)-1,16,16,angle_limited_pixels.data(),64))return 248;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(angle_bevel),sizeof(angle_bevel)-1,16,16,angle_bevel_pixels.data(),64))return 249;
 if(angle_limited_pixels!=angle_bevel_pixels)return 250;
 const char permitted_miter[]="0 j 1.5 M 0 J 6 w 0 G 4 4 m 12 4 l 12 12 l S";
 const char permitted_bevel[]="2 j 1.5 M 0 J 6 w 0 G 4 4 m 12 4 l 12 12 l S";
 std::vector<unsigned char> permitted_miter_pixels(20*20*4),permitted_bevel_pixels(20*20*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(permitted_miter),sizeof(permitted_miter)-1,20,20,permitted_miter_pixels.data(),80))return 251;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(permitted_bevel),sizeof(permitted_bevel)-1,20,20,permitted_bevel_pixels.data(),80))return 252;
 if(permitted_miter_pixels==permitted_bevel_pixels)return 253;
 const char anisotropic_square[]="3 0 0 1 0 0 cm 2 J 0 G 2 w 2 4 m 5 4 l S";
 const char anisotropic_butt[]="3 0 0 1 0 0 cm 0 J 0 G 2 w 2 4 m 5 4 l S";
 std::vector<unsigned char> anisotropic_square_pixels(32*16*4),anisotropic_butt_pixels(32*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(anisotropic_square),sizeof(anisotropic_square)-1,32,16,anisotropic_square_pixels.data(),128))return 254;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(anisotropic_butt),sizeof(anisotropic_butt)-1,32,16,anisotropic_butt_pixels.data(),128))return 255;
 if(anisotropic_square_pixels==anisotropic_butt_pixels)return 256;
 const char sheared_square_cap[]="1 0.5 0 1 0 0 cm 2 J 0 G 2 w 4 4 m 10 4 l S";
 const char explicit_sheared_square_cap[]="2 J 0 G 2 w 4 6 m 10 9 l S";
 std::vector<unsigned char> sheared_square_pixels(16*16*4),explicit_sheared_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(sheared_square_cap),sizeof(sheared_square_cap)-1,16,16,sheared_square_pixels.data(),64))return 254;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(explicit_sheared_square_cap),sizeof(explicit_sheared_square_cap)-1,16,16,explicit_sheared_pixels.data(),64))return 255;
 const char square_closed_loop[]="2 J 1 j 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 const char butt_closed_loop[]="0 J 1 j 2 w 0 G 3 3 m 11 3 l 11 11 l 3 11 l h S";
 std::vector<unsigned char> square_closed_pixels(16*16*4),butt_closed_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(square_closed_loop),sizeof(square_closed_loop)-1,16,16,square_closed_pixels.data(),64))return 256;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(butt_closed_loop),sizeof(butt_closed_loop)-1,16,16,butt_closed_pixels.data(),64))return 257;
 if(square_closed_pixels!=butt_closed_pixels)return 258;
 const char square_with_zero_interior[]="2 J 1 j 2 w 0 G 3 3 m 11 3 l 11 3 l 11 11 l S";
 const char square_without_zero_interior[]="2 J 1 j 2 w 0 G 3 3 m 11 3 l 11 11 l S";
 std::vector<unsigned char> square_zero_pixels(16*16*4),square_normal_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(square_with_zero_interior),sizeof(square_with_zero_interior)-1,16,16,square_zero_pixels.data(),64))return 259;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(square_without_zero_interior),sizeof(square_without_zero_interior)-1,16,16,square_normal_pixels.data(),64))return 260;
 if(square_zero_pixels!=square_normal_pixels)return 261;
 const char round_with_zero_interior[]="1 J 1 j 2 w 0 G 3 3 m 11 3 l 11 3 l 11 11 l S";
 const char round_without_zero_interior[]="1 J 1 j 2 w 0 G 3 3 m 11 3 l 11 11 l S";
 std::vector<unsigned char> round_zero_pixels(16*16*4),round_normal_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(round_with_zero_interior),sizeof(round_with_zero_interior)-1,16,16,round_zero_pixels.data(),64))return 262;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(round_without_zero_interior),sizeof(round_without_zero_interior)-1,16,16,round_normal_pixels.data(),64))return 263;
 if(round_zero_pixels!=round_normal_pixels)return 264;
 const char high_curvature_cubic[]="0 G 1 w 1 J 2 2 m 2 14 14 2 14 14 c S";
 const char flat_cubic[]="0 G 1 w 1 J 2 2 m 6 6 10 10 14 14 c S";
 std::vector<unsigned char> high_curve_pixels(16*16*4),flat_curve_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(high_curvature_cubic),sizeof(high_curvature_cubic)-1,16,16,high_curve_pixels.data(),64))return 265;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(flat_cubic),sizeof(flat_cubic)-1,16,16,flat_curve_pixels.data(),64))return 266;
 if(high_curve_pixels==flat_curve_pixels)return 267;
 const char anisotropic_curve[]="0 G 1 w 2 2 m 2 14 14 2 14 14 c S";
 std::vector<unsigned char> anisotropic_curve_pixels(64*16*4);
 if(!rinpdf::renderer::render_rectangles_box(reinterpret_cast<const unsigned char*>(anisotropic_curve),sizeof(anisotropic_curve)-1,64,16,anisotropic_curve_pixels.data(),256,{0,0,16,16}))return 268;
 bool anisotropic_curve_ink=false;
 for(size_t i=0;i<anisotropic_curve_pixels.size();i+=4)if(anisotropic_curve_pixels[i]<255){anisotropic_curve_ink=true;break;}
 if(!anisotropic_curve_ink)return 269;
 const char straight_cubic[]="0 G 1 w 2 2 m 6 6 10 10 14 14 c S";
 const char explicit_straight[]="0 G 1 w 2 2 m 14 14 l S";
 std::vector<unsigned char> straight_curve_pixels(16*16*4),explicit_straight_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(straight_cubic),sizeof(straight_cubic)-1,16,16,straight_curve_pixels.data(),64))return 270;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(explicit_straight),sizeof(explicit_straight)-1,16,16,explicit_straight_pixels.data(),64))return 271;
 if(straight_curve_pixels!=explicit_straight_pixels)return 272;
 // The cubic must actually leave the endpoint chord; the old 15/-5
 // controls reversed within [3,7] and could rasterize like a straight line.
 const char overshoot_cubic[]="0 G 1 w 0 J 3 8 m 20 8 -10 8 7 8 c S";
 const char overshoot_chord[]="0 G 1 w 0 J 3 8 m 7 8 l S";
 std::vector<unsigned char> overshoot_curve_pixels(20*16*4),overshoot_chord_pixels(20*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(overshoot_cubic),sizeof(overshoot_cubic)-1,20,16,overshoot_curve_pixels.data(),80))return 73;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(overshoot_chord),sizeof(overshoot_chord)-1,20,16,overshoot_chord_pixels.data(),80))return 274;
 if(overshoot_curve_pixels==overshoot_chord_pixels)return 275;
 const char reversed_collinear_cubic[]="0 G 1 w 0 J 2 8 m 11 8 3 8 12 8 c S";
 const char reversed_collinear_line[]="0 G 1 w 0 J 2 8 m 12 8 l S";
 std::vector<unsigned char> reversed_cubic_pixels(16*16*4),reversed_line_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(reversed_collinear_cubic),sizeof(reversed_collinear_cubic)-1,16,16,reversed_cubic_pixels.data(),64))return 276;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(reversed_collinear_line),sizeof(reversed_collinear_line)-1,16,16,reversed_line_pixels.data(),64))return 277;
 const char dashed_cubic[]="[2 1] 0 d 0 G 1 w 0 J 2 2 m 2 14 14 2 14 14 c S";
 const char solid_cubic[]="0 G 1 w 0 J 2 2 m 2 14 14 2 14 14 c S";
 std::vector<unsigned char> dashed_cubic_pixels(16*16*4),solid_cubic_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_cubic),sizeof(dashed_cubic)-1,16,16,dashed_cubic_pixels.data(),64))return 278;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(solid_cubic),sizeof(solid_cubic)-1,16,16,solid_cubic_pixels.data(),64))return 279;
 if(dashed_cubic_pixels==solid_cubic_pixels)return 280;
 const char dashed_collinear[]="[2 1] 0 d 0 G 1 w 0 J 2 8 m 5 8 9 8 12 8 c S";
 const char dashed_straight[]="[2 1] 0 d 0 G 1 w 0 J 2 8 m 12 8 l S";
 std::vector<unsigned char> dashed_collinear_pixels(16*16*4),dashed_straight_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_collinear),sizeof(dashed_collinear)-1,16,16,dashed_collinear_pixels.data(),64))return 281;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_straight),sizeof(dashed_straight)-1,16,16,dashed_straight_pixels.data(),64))return 282;
 if(dashed_collinear_pixels!=dashed_straight_pixels)return 283;
 const char dashed_curved_phase[]="[3 2] 1 d 0 G 1 w 0 J 2 2 m 2 14 14 2 14 14 c S";
 std::vector<unsigned char> dashed_curved_phase_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_curved_phase),sizeof(dashed_curved_phase)-1,16,16,dashed_curved_phase_pixels.data(),64))return 284;
 const char chained_dashed_curves[]="[3 2] 1 d 0 G 1 w 0 J 2 2 m 2 14 14 2 14 14 c 14 2 2 14 2 2 c S";
 std::vector<unsigned char> chained_dash_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(chained_dashed_curves),sizeof(chained_dashed_curves)-1,16,16,chained_dash_pixels.data(),64))return 285;
 const char fine_dash_curve[]="[0.5 0.5] 0.25 d 0 G 1 w 0 J 2 2 m 2 14 14 2 14 14 c S";
 std::vector<unsigned char> fine_dash_curve_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(fine_dash_curve),sizeof(fine_dash_curve)-1,16,16,fine_dash_curve_pixels.data(),64))return 286;
 bool fine_dash_ink=false;
 for(size_t i=0;i<fine_dash_curve_pixels.size();i+=4)if(fine_dash_curve_pixels[i]<255){fine_dash_ink=true;break;}
 if(!fine_dash_ink)return 287;
 const char high_curvature_dashed[]="[1 1] 0 d 0 G 1 w 0 J 1 1 m 1 15 15 1 15 15 c S";
 std::vector<unsigned char> high_curvature_dashed_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(high_curvature_dashed),sizeof(high_curvature_dashed)-1,16,16,high_curvature_dashed_pixels.data(),64))return 288;
 const char scaled_collinear_cubic[]="0 G 1 w 0 J 1 8 m 5 8 11 8 15 8 c S";
 const char scaled_collinear_line[]="0 G 1 w 0 J 1 8 m 15 8 l S";
 std::vector<unsigned char> scaled_collinear_pixels(16*16*4),scaled_collinear_line_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(scaled_collinear_cubic),sizeof(scaled_collinear_cubic)-1,16,16,scaled_collinear_pixels.data(),64))return 289;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(scaled_collinear_line),sizeof(scaled_collinear_line)-1,16,16,scaled_collinear_line_pixels.data(),64))return 290;
 if(scaled_collinear_pixels!=scaled_collinear_line_pixels)return 291;
 const char dashed_symmetric_cubic[]="[2 1] 0 d 0 G 1 w 0 J 2 8 m 2 14 14 14 14 8 c S";
 const char dashed_symmetric_cubic_reversed[]="[2 1] 0 d 0 G 1 w 0 J 14 8 m 14 14 2 14 2 8 c S";
 std::vector<unsigned char> symmetric_dash_pixels(16*16*4),reversed_dash_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_symmetric_cubic),sizeof(dashed_symmetric_cubic)-1,16,16,symmetric_dash_pixels.data(),64))return 292;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dashed_symmetric_cubic_reversed),sizeof(dashed_symmetric_cubic_reversed)-1,16,16,reversed_dash_pixels.data(),64))return 293;
 const char loop_cubic[]="0 G 1 w 1 J 8 8 m 14 14 2 14 8 8 c S";
 std::vector<unsigned char> loop_cubic_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(loop_cubic),sizeof(loop_cubic)-1,16,16,loop_cubic_pixels.data(),64))return 294;
 bool loop_ink=false;
 for(size_t i=0;i<loop_cubic_pixels.size();i+=4)if(loop_cubic_pixels[i]<255){loop_ink=true;break;}
 if(!loop_ink)return 295;
 const char scaled_curved_stroke[]="0 G 1 w 1 J 2 2 m 2 14 14 2 14 14 c S";
 std::vector<unsigned char> scaled_curved_pixels(128*32*4);
 if(!rinpdf::renderer::render_rectangles_box(
     reinterpret_cast<const unsigned char*>(scaled_curved_stroke),
     sizeof(scaled_curved_stroke)-1,128,32,scaled_curved_pixels.data(),512,{0,0,16,16}))return 296;
 bool scaled_curved_ink=false;
 for(size_t i=0;i<scaled_curved_pixels.size();i+=4)
  if(scaled_curved_pixels[i]<255){scaled_curved_ink=true;break;}
 if(!scaled_curved_ink)return 297;
 const char dash_before_curve[]="[2 1] 0 d 0 G 1 w 2 2 m 2 14 14 2 14 14 c S";
 const char dash_after_curve[]="0 G 1 w 2 2 m 2 14 14 2 14 14 c [2 1] 0 d S";
 std::vector<unsigned char> dash_before_pixels(16*16*4),dash_after_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_before_curve),sizeof(dash_before_curve)-1,16,16,dash_before_pixels.data(),64))return 298;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(dash_after_curve),sizeof(dash_after_curve)-1,16,16,dash_after_pixels.data(),64))return 299;
 if(dash_before_pixels!=dash_after_pixels)return 300;
 const char cubic_after_line[]="0 G 1 w 2 2 m 4 2 l 6 2 8 2 10 2 c S";
 std::vector<unsigned char> cubic_after_line_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(cubic_after_line),sizeof(cubic_after_line)-1,16,16,cubic_after_line_pixels.data(),64))return 301;
 const char translated_stroke_ctm[]="1 0 0 1 2 3 cm 0 G 2 w 1 J 2 2 m 10 2 l S";
 const char translated_stroke_explicit[]="0 G 2 w 1 J 4 5 m 12 5 l S";
 std::vector<unsigned char> translated_ctm_pixels(16*16*4),translated_explicit_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(translated_stroke_ctm),sizeof(translated_stroke_ctm)-1,16,16,translated_ctm_pixels.data(),64))return 302;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(translated_stroke_explicit),sizeof(translated_stroke_explicit)-1,16,16,translated_explicit_pixels.data(),64))return 303;
 if(translated_ctm_pixels!=translated_explicit_pixels)return 304;
 const char arc_length_loop[]="0 G 1 w 1 J 8 8 m 15 15 1 15 8 8 c S";
 std::vector<unsigned char> arc_length_loop_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(arc_length_loop),sizeof(arc_length_loop)-1,16,16,arc_length_loop_pixels.data(),64))return 305;
 const char inverse_arc_straight[]="0 G 1 w 2 8 m 5 8 9 8 12 8 c S";
 std::vector<unsigned char> inverse_arc_pixels(16*16*4);
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(inverse_arc_straight),sizeof(inverse_arc_straight)-1,16,16,inverse_arc_pixels.data(),64))return 306;
 // Exact cubic dash positions must differ from the old polyline-only
 // approximation for a curved cubic, and remain phase-order invariant.
 const char curved_dash_before[]="[2 1] 0 d 0 G 1 w 0 J 2 2 m 2 14 14 2 14 14 c S";
 const char curved_dash_after[]="0 G 1 w 0 J 2 2 m 2 14 14 2 14 14 c [2 1] 0 d S";
 std::vector<unsigned char> exact_before(32*32*4),exact_after(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(curved_dash_before),
 sizeof(curved_dash_before)-1,32,32,exact_before.data(),128))return 307;
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(curved_dash_after),
 sizeof(curved_dash_after)-1,32,32,exact_after.data(),128))return 308;
 if(exact_before!=exact_after)return 309;
 bool has_ink=false;
 for(size_t i=0;i<exact_before.size();i+=4)if(exact_before[i]<255){has_ink=true;break;}
 if(!has_ink)return 310;
 // Mixed path must preserve dash phase across both lines and cubics.
 const char mixed_path[]="[2 1] 0 d 0 G 1 w 0 J 1 8 m 5 8 l 7 12 11 12 13 8 c 15 8 l S";
 std::vector<unsigned char> mixed_pixels(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(mixed_path),sizeof(mixed_path)-1,
 32,32,mixed_pixels.data(),128))return 311;
 bool mixed_ink=false;
 for(size_t i=0;i<mixed_pixels.size();i+=4)if(mixed_pixels[i]<255){mixed_ink=true;break;}
 if(!mixed_ink)return 312;
 const char double_cubic[]="[2 1] 1 d 0 G 1 w 0 J 2 8 m 4 12 8 12 10 8 c 12 4 16 4 18 8 c S";
 std::vector<unsigned char> double_pixels(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(double_cubic),sizeof(double_cubic)-1,
 32,32,double_pixels.data(),128))return 313;
 // Off-to-on transitions inside a mixed path must regain an exposed cap.
 // A monotonic collinear cubic and its equivalent straight segment should
 // paint the same dash-run endpoints under butt caps.
 const char mixed_collinear[]="[2 3] 0 d 0 G 1 w 0 J 2 8 m 6 8 l 8 8 12 8 14 8 c S";
 const char mixed_linear[]="[2 3] 0 d 0 G 1 w 0 J 2 8 m 6 8 l 14 8 l S";
 std::vector<unsigned char> mixed_collinear_img(32*32*4),mixed_linear_img(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(mixed_collinear),sizeof(mixed_collinear)-1,
 32,32,mixed_collinear_img.data(),128))return 314;
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(mixed_linear),sizeof(mixed_linear)-1,
 32,32,mixed_linear_img.data(),128))return 315;
 if(mixed_collinear_img!=mixed_linear_img)return 316;
 const char mixed_collinear_after[]="0 G 1 w 0 J 2 8 m 6 8 l 8 8 12 8 14 8 c [2 3] 0 d S";
 std::vector<unsigned char> mixed_collinear_after_img(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(mixed_collinear_after),sizeof(mixed_collinear_after)-1,
 32,32,mixed_collinear_after_img.data(),128))return 317;
 if(mixed_collinear_img!=mixed_collinear_after_img)return 318;
 // Closed cubic contours exercise the shared seam classifier in
 // the existing bounded, polyline-based CPU stroke fallback.
 const char closed_cubic_join[]="[200 2] 0 d 0 G 2 w 1 J 4 4 m 4 14 14 14 14 4 c h S";
 const char closed_cubic_boundary[]="[20 2] 0 d 0 G 2 w 1 J 4 4 m 4 14 14 14 14 4 c h S";
 std::vector<unsigned char> closed_join_pixels(32*32*4),closed_boundary_pixels(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
   reinterpret_cast<const unsigned char*>(closed_cubic_join),
   sizeof(closed_cubic_join)-1,32,32,closed_join_pixels.data(),128))return 319;
 if(!rinpdf::renderer::render_rectangles(
   reinterpret_cast<const unsigned char*>(closed_cubic_boundary),
   sizeof(closed_cubic_boundary)-1,32,32,closed_boundary_pixels.data(),128))return 320;
 bool closed_ink=false;
 for(size_t i=0;i<closed_join_pixels.size();i+=4)
  if(closed_join_pixels[i]<255){closed_ink=true;break;}
 if(!closed_ink)return 321;
 // Closed cubic contours with an off-region at the seam take the
 // inverse arc-length path without synthesizing a seam join.
 const char closed_cubic_off_seam[]="[2 2] 2 d 0 G 1 w 0 J 4 4 m 4 14 14 14 14 4 c h S";
 const char closed_cubic_off_seam_late[]="0 G 1 w 0 J 4 4 m 4 14 14 14 14 4 c h [2 2] 2 d S";
 std::vector<unsigned char> off_seam(32*32*4),off_seam_late(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(closed_cubic_off_seam),
 sizeof(closed_cubic_off_seam)-1,32,32,off_seam.data(),128))return 322;
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(closed_cubic_off_seam_late),
 sizeof(closed_cubic_off_seam_late)-1,32,32,off_seam_late.data(),128))return 323;
 if(off_seam!=off_seam_late)return 324;
 // Closed dashed cubic contours with painted seam and Miter/Bevel joins.
 const char analytic_miter_seam[]="[100 1] 0 d 0 G 2 w 0 j 4 4 m 4 14 14 14 14 4 c h S";
 const char analytic_bevel_seam[]="[100 1] 0 d 0 G 2 w 2 j 4 4 m 4 14 14 14 14 4 c h S";
 std::vector<unsigned char> analytic_miter_pixels(32*32*4),analytic_bevel_pixels(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(analytic_miter_seam),
 sizeof(analytic_miter_seam)-1,32,32,analytic_miter_pixels.data(),128))return 325;
 if(!rinpdf::renderer::render_rectangles(
 reinterpret_cast<const unsigned char*>(analytic_bevel_seam),
 sizeof(analytic_bevel_seam)-1,32,32,analytic_bevel_pixels.data(),128))return 326;
 // Fully painted closed cubic seam: exercise the dedicated
 // no-caps path for both miter and bevel joins.
 const char closed_full_miter[]="[100 1] 0 d 0 G 2 w 0 j 10 M 4 4 m 4 14 14 14 14 4 c h S";
 const char closed_full_bevel[]="[100 1] 0 d 0 G 2 w 2 j 4 4 m 4 14 14 14 14 4 c h S";
 std::vector<unsigned char> full_miter(32*32*4),full_bevel(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
   reinterpret_cast<const unsigned char*>(closed_full_miter),
   sizeof(closed_full_miter)-1,32,32,full_miter.data(),128))return 327;
 if(!rinpdf::renderer::render_rectangles(
   reinterpret_cast<const unsigned char*>(closed_full_bevel),
   sizeof(closed_full_bevel)-1,32,32,full_bevel.data(),128))return 328;
 bool full_has_ink=false;
 for(size_t i=0;i<full_miter.size();i+=4)
  if(full_miter[i]<255){full_has_ink=true;break;}
 if(!full_has_ink)return 329;
 // Two cubic segments joined inside a fully painted closed contour.
 // Both miter and bevel must accept original endpoint tangents.
 const char internal_cubic_miter[]="[100 1] 0 d 0 G 2 w 0 j 10 M 3 3 m 3 12 9 12 12 8 c 14 4 9 2 3 3 c h S";
 const char internal_cubic_bevel[]="[100 1] 0 d 0 G 2 w 2 j 3 3 m 3 12 9 12 12 8 c 14 4 9 2 3 3 c h S";
 std::vector<unsigned char> internal_miter_img(32*32*4),internal_bevel_img(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
   reinterpret_cast<const unsigned char*>(internal_cubic_miter),
   sizeof(internal_cubic_miter)-1,32,32,internal_miter_img.data(),128))return 330;
 if(!rinpdf::renderer::render_rectangles(
   reinterpret_cast<const unsigned char*>(internal_cubic_bevel),
   sizeof(internal_cubic_bevel)-1,32,32,internal_bevel_img.data(),128))return 331;
 bool internal_ink=false;
 for(size_t i=0;i<internal_miter_img.size();i+=4)
  if(internal_miter_img[i]<255){internal_ink=true;break;}
 if(!internal_ink)return 332;
 // A painted dash crosses the authored junction of two cubic curves.
 // Both non-round join styles must retain a drawable stroke.
 const char joined_dashed_miter[]="[40 3] 0 d 0 G 2 w 0 j 3 5 m 5 12 9 12 12 7 c 15 2 19 3 21 8 c S";
 const char joined_dashed_bevel[]="[40 3] 0 d 0 G 2 w 2 j 3 5 m 5 12 9 12 12 7 c 15 2 19 3 21 8 c S";
 std::vector<unsigned char> joined_miter(32*32*4),joined_bevel(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
   reinterpret_cast<const unsigned char*>(joined_dashed_miter),
   sizeof(joined_dashed_miter)-1,32,32,joined_miter.data(),128))return 333;
 if(!rinpdf::renderer::render_rectangles(
   reinterpret_cast<const unsigned char*>(joined_dashed_bevel),
   sizeof(joined_dashed_bevel)-1,32,32,joined_bevel.data(),128))return 334;
 bool joined_ink=false;
 for(size_t i=0;i<joined_miter.size();i+=4)
  if(joined_miter[i]<255){joined_ink=true;break;}
 if(!joined_ink)return 335;
 // A zero-length dash entry at a cubic boundary must be skipped
 // when deciding if a painted run continues into the next segment.
 const char zero_dash_join[]="[5 0 5 3] 0 d 0 G 2 w 0 j 2 8 m 3 12 7 12 8 8 c 9 4 13 4 14 8 c S";
 std::vector<unsigned char> zero_dash_pixels(32*32*4);
 if(!rinpdf::renderer::render_rectangles(
   reinterpret_cast<const unsigned char*>(zero_dash_join),
   sizeof(zero_dash_join)-1,32,32,zero_dash_pixels.data(),128))return 336;
 bool zero_dash_ink=false;
 for(size_t i=0;i<zero_dash_pixels.size();i+=4)
  if(zero_dash_pixels[i]<255){zero_dash_ink=true;break;}
 if(!zero_dash_ink)return 337;
 // Inline marked-content language overrides may nest around drawing ops.
 const char marked[]=
  "/Span << /Lang (ja-JP) >> BDC "
  "/P BMC 0 g 2 2 4 4 re f EMC "
  "/Span << /Lang (zh-Hant) >> BDC 0 g 1 1 1 1 re f EMC "
  "EMC";
 if(!rinpdf::renderer::render_rectangles(
    reinterpret_cast<const unsigned char*>(marked),sizeof(marked)-1,
    8,8,pixels.data(),8*4))return 338;
 const char unbalanced[]="/Span << /Lang (ja) >> BDC 0 g 2 2 4 4 re f";
 if(rinpdf::renderer::render_rectangles(
    reinterpret_cast<const unsigned char*>(unbalanced),sizeof(unbalanced)-1,
    8,8,pixels.data(),8*4))return 339;
 const char wrong_lang[]="/Span << /Lang 7 >> BDC EMC";
 if(rinpdf::renderer::render_rectangles(
    reinterpret_cast<const unsigned char*>(wrong_lang),sizeof(wrong_lang)-1,
    8,8,pixels.data(),8*4))return 340;
 const char named_properties[]="/Span /JP BDC 0 g 2 2 4 4 re f EMC";
 const rinpdf::renderer::MarkedProperties property_names{{"JP",{"ja-JP",{},false}}};
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
   reinterpret_cast<const unsigned char*>(named_properties),
   sizeof(named_properties)-1,8,8,pixels.data(),8*4,
   {0,0,8,8},{},property_names))return 341;
 if(rinpdf::renderer::render_rectangles_box_with_fonts(
   reinterpret_cast<const unsigned char*>(named_properties),
   sizeof(named_properties)-1,8,8,pixels.data(),8*4,
   {0,0,8,8},{},{}))return 342;
 return 0;
}
