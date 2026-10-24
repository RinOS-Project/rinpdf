#include "sfnt.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace rinpdf::renderer {
bool cff_flatten_commands(const CffCommands& commands,double tolerance,
 std::vector<CffPath>& contours){
 contours.clear();
 if(!std::isfinite(tolerance)||tolerance<=0||tolerance>8||
    commands.size()>8192)return false;
 std::vector<CffPath> paths;
 CffPoint current{};
 size_t total=0;
 auto mid=[](CffPoint a,CffPoint b)->CffPoint{
  return {(a.x+b.x)*0.5,(a.y+b.y)*0.5};
 };
 struct Cubic {CffPoint a,b,c,d;unsigned depth;};
 for(const auto& command:commands){
  if(!std::isfinite(command.end.x)||!std::isfinite(command.end.y))
   return false;
  if(command.kind==CffSegment::Kind::Move){
   if(paths.size()>=256)return false;
   paths.push_back({command.end});
  }else if(command.kind==CffSegment::Kind::Line){
   if(paths.empty()||++total>16384)return false;
   paths.back().push_back(command.end);
  }else if(command.kind==CffSegment::Kind::Cubic){
   if(paths.empty()||
      !std::isfinite(command.first.x)||!std::isfinite(command.first.y)||
      !std::isfinite(command.second.x)||!std::isfinite(command.second.y))
    return false;
   std::vector<Cubic> pending{{current,command.first,
                               command.second,command.end,0}};
   while(!pending.empty()){
    const auto q=pending.back();pending.pop_back();
    const double dx=q.d.x-q.a.x,dy=q.d.y-q.a.y;
    const double chord=std::hypot(dx,dy);
    const double d1=std::abs((q.b.x-q.a.x)*dy-(q.b.y-q.a.y)*dx);
    const double d2=std::abs((q.c.x-q.a.x)*dy-(q.c.y-q.a.y)*dx);
    if(!std::isfinite(chord)||!std::isfinite(d1)||!std::isfinite(d2))
     return false;
    const bool flat=chord>0?std::max(d1,d2)/chord<=tolerance:
      (std::hypot(q.b.x-q.a.x,q.b.y-q.a.y)<=tolerance&&
       std::hypot(q.c.x-q.a.x,q.c.y-q.a.y)<=tolerance);
    if(flat){
     if(++total>16384)return false;
     paths.back().push_back(q.d);
    }else{
     if(q.depth>=12||pending.size()>4096)return false;
     const auto ab=mid(q.a,q.b),bc=mid(q.b,q.c),cd=mid(q.c,q.d);
     const auto abbc=mid(ab,bc),bccd=mid(bc,cd),center=mid(abbc,bccd);
     pending.push_back({center,bccd,cd,q.d,q.depth+1});
     pending.push_back({q.a,ab,abbc,center,q.depth+1});
    }
   }
  }else return false;
  current=command.end;
 }
 contours=std::move(paths);
 return true;
}
}
