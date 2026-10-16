#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
namespace rinpdf::renderer {
struct Point {double x,y;};
// Numerically integrate cubic Bezier speed in user-space coordinates.
// Composite five-point Gauss-Legendre quadrature with bounded bisection.
// This is an arc-length primitive; dashed curve placement still uses the
// existing flattened path until inverse arc-length mapping is integrated.
inline bool cubic_arc_length(Point a,Point b,Point c,Point d,double& result,double upper=1.0){
 if(!std::isfinite(upper)||upper<0||upper>1)return false;
 if(upper==0){result=0;return true;}
 constexpr double nodes[5]={0.0,-0.5384693101056831,0.5384693101056831,-0.9061798459386640,0.9061798459386640};
 constexpr double weights[5]={0.5688888888888889,0.4786286704993665,0.4786286704993665,0.2369268850561891,0.2369268850561891};
 auto integrate=[&](double lo,double hi,double& out)->bool{
  const double mid=(lo+hi)*0.5,half=(hi-lo)*0.5;
  double sum=0;
  for(int i=0;i<5;++i){
   const double t=mid+half*nodes[i],u=1.0-t;
   const double dx=3.0*(u*u*(b.x-a.x)+2.0*u*t*(c.x-b.x)+t*t*(d.x-c.x));
   const double dy=3.0*(u*u*(b.y-a.y)+2.0*u*t*(c.y-b.y)+t*t*(d.y-c.y));
   const double speed=std::hypot(dx,dy);
   if(!std::isfinite(speed))return false;
   sum+=weights[i]*speed;
  }
  out=sum*half;
  return std::isfinite(out);
 };
 double previous=0;
 if(!integrate(0,upper,previous))return false;
 for(unsigned n=2;n<=256;n*=2){
  double total=0;
  for(unsigned j=0;j<n;++j){
   double part=0;
   if(!integrate(upper*(double)j/n,upper*(double)(j+1)/n,part))return false;
   total+=part;
  }
  if(!std::isfinite(total))return false;
  if(std::abs(total-previous)<=1e-9*std::max(1.0,total)){
   result=total;return true;
  }
  previous=total;
 }
 // The quadrature failed to meet its convergence criterion.
 // Do not report an unverified approximation as successful.
 return false;
}
// Invert the numerically integrated arc length by monotone bisection.
// The caller can use the returned t to place a dash boundary on B(t).
inline bool cubic_inverse_arc_length(Point a,Point b,Point c,Point d,
                                    double distance,double& t){
 double total=0;
 if(!cubic_arc_length(a,b,c,d,total)||!std::isfinite(distance)||distance<0)return false;
 if(distance==0){t=0;return true;}
 if(distance>=total){if(distance>total+1e-9*std::max(1.0,total))return false;t=1;return true;}
 double lo=0,hi=1;
 for(unsigned i=0;i<48;++i){
  const double mid=(lo+hi)*0.5;
  double length=0;
  if(!cubic_arc_length(a,b,c,d,length,mid))return false;
  if(length<distance)lo=mid;else hi=mid;
 }
 t=(lo+hi)*0.5;
 if(!std::isfinite(t))return false;
 double check=0;
 if(!cubic_arc_length(a,b,c,d,check,t))return false;
 return std::abs(check-distance)<=1e-7*std::max(1.0,total);
}
// Evaluate B(t) directly rather than interpolating between flattened
// vertices. This is used for arc-length boundary placement.
inline Point cubic_point(Point a,Point b,Point c,Point d,double t){
 const double u=1.0-t;
 return {u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*d.x,
         u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*d.y};
}
// Derivative-based tangent at a cubic endpoint. Degenerate endpoint
// handles use the first nonzero control polygon vector, preserving
// direction at an endpoint cusp without normalizing a zero vector.
inline bool cubic_endpoint_tangent(Point a,Point b,Point c,Point d,
                                   bool at_end,Point& tangent){
 Point candidate[3];
 if(at_end){
  candidate[0]={d.x-c.x,d.y-c.y};
  candidate[1]={d.x-b.x,d.y-b.y};
  candidate[2]={d.x-a.x,d.y-a.y};
 }else{
  candidate[0]={b.x-a.x,b.y-a.y};
  candidate[1]={c.x-a.x,c.y-a.y};
  candidate[2]={d.x-a.x,d.y-a.y};
 }
 for(Point v:candidate){
  const double len=std::hypot(v.x,v.y);
  if(!std::isfinite(len))return false;
  if(len>0){
   tangent={v.x/len,v.y/len};
   return std::isfinite(tangent.x)&&std::isfinite(tangent.y);
  }
 }
 return false;
}

}
