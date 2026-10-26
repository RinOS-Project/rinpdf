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
 // Reuse the worklist capacity across consecutive curves in one glyph.
 std::vector<Cubic> pending;
 pending.reserve(32);
 for(const auto& command:commands){
  if(!std::isfinite(command.end.x)||!std::isfinite(command.end.y))
   return false;
  if(command.kind==CffSegment::Kind::Move){
   // Start a new contour only after enforcing both contour and total
   // vertex budgets. A move contributes one vertex to the rasterizer.
   if(paths.size()>=256||total>=16384)return false;
   paths.push_back({command.end});
   ++total;
  }else if(command.kind==CffSegment::Kind::Line){
   if(paths.empty()||paths.back().size()>=4096||
      total>=16384)return false;
   paths.back().push_back(command.end);
   ++total;
  }else if(command.kind==CffSegment::Kind::Cubic){
   if(paths.empty()||
      !std::isfinite(command.first.x)||!std::isfinite(command.first.y)||
      !std::isfinite(command.second.x)||!std::isfinite(command.second.y))
    return false;
   pending.clear();
   pending.push_back({current,command.first,
                      command.second,command.end,0});
   while(!pending.empty()){
    const auto q=pending.back();pending.pop_back();
    const double dx=q.d.x-q.a.x,dy=q.d.y-q.a.y;
    const double chord=std::hypot(dx,dy);
    if(!std::isfinite(chord))return false;
    // Distance to the finite endpoint segment, not its infinite line.
    // Collinear control points beyond either endpoint can describe a
    // substantial backtracking cubic and must not be flattened to a line.
    auto distance_to_chord=[&](CffPoint p)->double{
     if(chord==0)return std::hypot(p.x-q.a.x,p.y-q.a.y);
     const double t=std::clamp(((p.x-q.a.x)*dx+(p.y-q.a.y)*dy)/
                                (chord*chord),0.0,1.0);
     return std::hypot(p.x-(q.a.x+t*dx),p.y-(q.a.y+t*dy));
    };
    const double d1=distance_to_chord(q.b);
    const double d2=distance_to_chord(q.c);
    if(!std::isfinite(d1)||!std::isfinite(d2))return false;
    const bool flat=d1<=tolerance&&d2<=tolerance;
    if(flat){
     if(paths.back().size()>=4096||total>=16384)return false;
     paths.back().push_back(q.d);
     ++total;
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
