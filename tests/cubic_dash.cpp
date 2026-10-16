#include "../src/renderer/cubic_dash.h"
#include <cmath>
#include <vector>
using namespace rinpdf::renderer;
int main(){
 const Point a{0,0},b{2,0},c{8,0},d{10,0};
 const std::vector<double> pattern{2,1};
 CubicDashCursor cur;
 if(!cubic_dash_start(pattern,1,cur))return 1;
 std::vector<CubicDashSpan> spans;
 if(!cubic_dash_spans(a,b,c,d,pattern,cur,spans)||spans.size()<5)return 2;
 if(!spans.front().painted||std::abs(cubic_point(a,b,c,d,spans.front().t1).x-1)>1e-6)return 3;
 for(size_t i=1;i<spans.size();++i)
  if(std::abs(spans[i-1].t1-spans[i].t0)>1e-8||spans[i].painted==spans[i-1].painted)return 4;
 if(!cubic_dash_spans(d,{12,0},{18,0},{20,0},pattern,cur,spans))return 5;
 if(spans.empty()||spans.front().painted)return 6;
 if(cubic_dash_start({0,0},0,cur))return 7;
 if(cubic_dash_start({-1,2},0,cur))return 8;
 if(!cubic_dash_start({0,2,1},0,cur))return 9;
 if(!cubic_dash_spans(a,b,c,d,{0,2,1},cur,spans))return 10;
 const Point p{1,1};
 if(!cubic_dash_spans(p,p,p,p,pattern,cur,spans)||!spans.empty())return 11;

 // The closing seam is a join only when paint reaches it from both sides.
 CubicDashSeam seam;
 CubicDashCursor initial;
 CubicDashCursor terminal{0,1};
 if(!cubic_dash_start({2,2},0,initial))return 12;
 if(!cubic_dash_closed_seam({2,2},initial,terminal,{true,true},{true,true},seam))return 13;
 if(!seam.joined||!seam.first_painted||!seam.last_painted)return 14;
 if(!cubic_dash_closed_seam({2,2},initial,terminal,{true,false},{true,true},seam))return 15;
 if(seam.joined||seam.last_painted)return 16;
 if(!cubic_dash_closed_seam({2,2},initial,terminal,{}, {},seam))return 17;
 if(seam.joined)return 18;
 if(!cubic_dash_start({2,2},2,initial))return 19;
 if(!cubic_dash_closed_seam({2,2},initial,terminal,{true},{true},seam))return 20;
 if(seam.joined||seam.first_painted)return 21;
 if(cubic_dash_closed_seam({2,2},initial,terminal,{true},{},seam))return 22;
 if(!cubic_dash_start({2,2},0,initial))return 23;
 terminal={0,0};
 if(!cubic_dash_closed_seam({2,2},initial,terminal,{true},{true},seam))return 24;
 if(seam.joined)return 25;
 // Adaptive subdivision retains the exact dash boundaries and adds
 // vertices only when the device-space curve is non-flat.
 std::vector<Point> flat,curved;
 if(!cubic_dash_flatten_span(a,b,c,d,0.2,0.7,1,1,flat))return 26;
 if(flat.size()!=2)return 27;
 if(!cubic_dash_flatten_span({0,0},{0,10},{10,10},{10,0},
                             0,1,4,4,curved))return 28;
 if(curved.size()<=2)return 29;
 if(std::hypot(curved.front().x,curved.front().y)>1e-9||
    std::hypot(curved.back().x-10,curved.back().y)>1e-9)return 30;
 if(cubic_dash_flatten_span(a,b,c,d,0.7,0.2,1,1,flat))return 31;
 return 0;
}
