#include "sfnt.h"
#include <cmath>
#include <cstdint>
#include <vector>
namespace rinpdf::renderer {
bool sfnt_cff_outline_commands(const SfntFont& font,uint16_t glyph,
 CffCommands& commands){
 commands.clear();
 const uint8_t* program=nullptr;
 size_t length=0;
 if(!sfnt_cff_glyph_program(font,glyph,program,length)||length>65536)return false;
 std::vector<double> stack;
 CffCommands result;
 double x=0,y=0;
 bool ended=false;
 size_t segments=0;
 size_t stems=0;
 bool path_started=false;
 bool width_seen=false;
 auto coordinate=[&](double dx,double dy)->bool{
  x+=dx;y+=dy;
  return std::isfinite(x)&&std::isfinite(y)&&
   std::abs(x)<=1000000&&std::abs(y)<=1000000;
 };
 auto begin=[&](double dx,double dy)->bool{
  if(result.size()>=8192||!coordinate(dx,dy))return false;
  path_started=true;
  result.push_back({CffSegment::Kind::Move,{},{},{x,y}});
  return true;
 };
 auto line=[&](double dx,double dy)->bool{
  if(!path_started||segments>=8192||result.size()>=8192||
     !coordinate(dx,dy))return false;
  ++segments;
  result.push_back({CffSegment::Kind::Line,{},{},{x,y}});
  return true;
 };
 auto curve=[&](double a,double b,double c,double d,double e,double f)->bool{
  if(!path_started||segments>=8192||result.size()>=8192)return false;
  if(!coordinate(a,b))return false;
  const CffPoint p1{x,y};
  if(!coordinate(c,d))return false;
  const CffPoint p2{x,y};
  if(!coordinate(e,f))return false;
  ++segments;
  result.push_back({CffSegment::Kind::Cubic,p1,p2,{x,y}});
  return true;
 };
 struct Frame{const uint8_t* bytes;size_t length,pos;};
 std::vector<Frame> frames;
 const uint8_t* current=program;
 size_t current_length=length;
 size_t executed=0;
 for(size_t pos=0;pos<current_length;){
  if(++executed>100000)return false;
  const unsigned op=current[pos++];
  if(op==28){
   if(current_length-pos<2)return false;
   stack.push_back(double(int16_t(uint16_t(current[pos])<<8|current[pos+1])));
   pos+=2;
  }else if(op>=32&&op<=246)stack.push_back(int(op)-139);
  else if(op>=247&&op<=250){
   if(pos>=current_length)return false;
   stack.push_back((int(op)-247)*256+current[pos++]+108);
  }else if(op>=251&&op<=254){
   if(pos>=current_length)return false;
   stack.push_back(-(int(op)-251)*256-current[pos++]-108);
  }else if(op==255){
   if(current_length-pos<4)return false;
   const uint32_t bits=uint32_t(current[pos])<<24|
      uint32_t(current[pos+1])<<16|uint32_t(current[pos+2])<<8|current[pos+3];
   pos+=4;
   stack.push_back(double(int32_t(bits))/65536.0);
  }else{
   // Type 2 allows one optional width before the first stem/moveto.
   // Width is positioning metadata, not a path coordinate.
   if(!width_seen&&!path_started){
    const bool stem=(op==1||op==3||op==18||op==23||op==19||op==20);
    const bool move=(op==4||op==21||op==22);
    const bool has_width=(stem&&(stack.size()%2)==1)||
      (op==21&&stack.size()==3)||
      ((op==4||op==22)&&stack.size()==2)||
      (op==14&&stack.size()==1);
    if(has_width)stack.erase(stack.begin());
    if(stem||move||op==14)width_seen=true;
   }
   if(op==12){
    if(pos>=current_length)return false;
    const unsigned escaped=current[pos++];
    const size_t n=stack.size();
    if(escaped==34){ // hflex
     if(n!=7||
        !curve(stack[0],0,stack[1],stack[2],stack[3],0)||
        !curve(stack[4],0,stack[5],-stack[2],stack[6],0))return false;
    }else if(escaped==35){ // flex, with ignored flex depth
     if(n!=13||
        !curve(stack[0],stack[1],stack[2],stack[3],stack[4],stack[5])||
        !curve(stack[6],stack[7],stack[8],stack[9],stack[10],stack[11]))
      return false;
    }else if(escaped==36){ // hflex1
     if(n!=9||
        !curve(stack[0],stack[1],stack[2],stack[3],stack[4],0)||
        !curve(stack[5],0,stack[6],stack[7],
               stack[8],-(stack[1]+stack[3]+stack[7])))return false;
    }else if(escaped==37){ // flex1
     if(n!=11)return false;
     const double dx=stack[0]+stack[2]+stack[4]+stack[6]+stack[8];
     const double dy=stack[1]+stack[3]+stack[5]+stack[7]+stack[9];
     const double last_x=std::abs(dx)>std::abs(dy)?stack[10]:-dx;
     const double last_y=std::abs(dx)>std::abs(dy)?-dy:stack[10];
     if(!curve(stack[0],stack[1],stack[2],stack[3],stack[4],stack[5])||
        !curve(stack[6],stack[7],stack[8],stack[9],last_x,last_y))
      return false;
    }else return false;
    stack.clear();
    continue;
   }
   const size_t n=stack.size();
   if(op==10||op==29){
    if(stack.empty()||frames.size()>=10)return false;
    const double number=stack.back();
    if(!std::isfinite(number)||number<-65536||number>65536||
       std::trunc(number)!=number)return false;
    stack.pop_back();
    const uint8_t* sub=nullptr;
    size_t sub_length=0;
    bool cid_keyed=false;
    if(op==10&&!sfnt_cff_is_cid_keyed(font,cid_keyed))return false;
    const bool resolved=op==10?
       (cid_keyed?
         sfnt_cff_cid_local_subroutine(font,glyph,int32_t(number),sub,sub_length):
         sfnt_cff_local_subroutine(font,int32_t(number),sub,sub_length)):
       sfnt_cff_global_subroutine(font,int32_t(number),sub,sub_length);
    if(!resolved||sub_length==0)return false;
    frames.push_back({current,current_length,pos});
    current=sub;current_length=sub_length;pos=0;
    continue;
   }
   if(op==11){
    if(frames.empty()||n>48)return false;
    const Frame previous=frames.back();frames.pop_back();
    if(pos!=current_length)return false;
    current=previous.bytes;current_length=previous.length;
    pos=previous.pos;
    continue;
   }
   if(op==14){
    if(!frames.empty()||n!=0||pos!=current_length)return false;
    ended=true;break;
   }
   if(op==1||op==3||op==18||op==23){
    // Stem hints do not affect unhinted outline geometry.
    if(path_started||n%2||stems+n/2>96)return false;
    stems+=n/2;
   }else if(op==19||op==20){
    if(path_started||n%2||stems+n/2>96)return false;
    stems+=n/2;
    const size_t mask_bytes=(stems+7)/8;
    if(mask_bytes>current_length-pos)return false;
    pos+=mask_bytes;
   }else if(op==21){
    if(n!=2||!begin(stack[0],stack[1]))return false;
   }else if(op==22||op==4){
    if(n!=1||!begin(op==22?stack[0]:0,op==4?stack[0]:0))return false;
   }else if(op==5){
    if(n<2||(n%2))return false;
    for(size_t i=0;i<n;i+=2)if(!line(stack[i],stack[i+1]))return false;
   }else if(op==6||op==7){
    if(!n)return false;
    bool horizontal=op==6;
    for(double v:stack){
     if(!line(horizontal?v:0,horizontal?0:v))return false;
     horizontal=!horizontal;
    }
   }else if(op==26||op==27){
    // vvcurveto/hhcurveto: an optional initial cross-axis
    // displacement followed by one or more four-operand curves.
    if(n<4||(n%4!=0&&n%4!=1))return false;
    size_t i=0;
    double cross=0;
    if(n%4){cross=stack[i++];}
    for(;i<n;i+=4){
     if(op==26){
      if(!curve(cross,stack[i],stack[i+1],stack[i+2],0,stack[i+3]))
       return false;
     }else{
      if(!curve(stack[i],cross,stack[i+1],stack[i+2],stack[i+3],0))
       return false;
     }
     cross=0;
    }
   }else if(op==24){
    // rcurveline: one or more curves followed by a line.
    if(n<8||(n-2)%6)return false;
    for(size_t i=0;i+8<=n;i+=6)
     if(!curve(stack[i],stack[i+1],stack[i+2],stack[i+3],
               stack[i+4],stack[i+5]))return false;
    if(!line(stack[n-2],stack[n-1]))return false;
   }else if(op==25){
    // rlinecurve: one or more lines followed by a curve.
    if(n<8||(n-6)%2)return false;
    for(size_t i=0;i<n-6;i+=2)
     if(!line(stack[i],stack[i+1]))return false;
    if(!curve(stack[n-6],stack[n-5],stack[n-4],stack[n-3],
              stack[n-2],stack[n-1]))return false;
   }else if(op==30||op==31){
    // vhcurveto / hvcurveto alternate vertical and horizontal starts.
    // An optional final operand sets the terminal perpendicular delta.
    if(n<4||n%4>1)return false;
    bool vertical=op==30;
    size_t i=0;
    while(i<n){
     const bool last=(n-i<=5);
     if(n-i<4)return false;
     const double extra=last&&n-i==5?stack[i+4]:0;
     if(vertical){
      if(!curve(0,stack[i],stack[i+1],stack[i+2],
                stack[i+3],extra))return false;
     }else{
      if(!curve(stack[i],0,stack[i+1],stack[i+2],
                extra,stack[i+3]))return false;
     }
     i+=last&&n-i==5?5:4;
     vertical=!vertical;
    }
   }else if(op==8){
    if(n<6||(n%6))return false;
    for(size_t i=0;i<n;i+=6)
     if(!curve(stack[i],stack[i+1],stack[i+2],stack[i+3],
               stack[i+4],stack[i+5]))return false;
   }else return false;
   stack.clear();
  }
  if(stack.size()>48)return false;
 }
 if(!ended)return false;
 commands=std::move(result);
 return true;
}
}
