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
 return 0;
}
