#include "../src/renderer/cubic_arc.h"
#include <cmath>
#include <limits>
using namespace rinpdf::renderer;
int main(){
  const Point a{0,0}, b{2,0}, c{8,0}, d{10,0};
  double total=0,t=0,half=0;
  if(!cubic_arc_length(a,b,c,d,total)||std::abs(total-10)>1e-7)return 1;
  if(!cubic_inverse_arc_length(a,b,c,d,5,t)||std::abs(t-0.5)>1e-6)return 2;
  const Point middle=cubic_point(a,b,c,d,t);
  if(std::abs(middle.x-5)>1e-6||std::abs(middle.y)>1e-6)return 3;
  if(!cubic_arc_length(a,b,c,d,half,t)||std::abs(half-5)>1e-6)return 4;
  if(cubic_inverse_arc_length(a,b,c,d,11,t))return 5;
  if(cubic_arc_length(a,b,c,d,total,std::numeric_limits<double>::quiet_NaN()))return 6;
  if(cubic_inverse_arc_length(a,b,c,d,-1,t))return 7;
  const Point p{5,5};
  if(!cubic_arc_length(p,p,p,p,total)||total!=0)return 8;
  if(!cubic_inverse_arc_length(p,p,p,p,0,t)||t!=0)return 9;
  return 0;
}
