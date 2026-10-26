#include "bitmap_glyph.h"
#include <cmath>
#include <algorithm>
namespace rinpdf::renderer {
static const uint8_t* glyph(uint8_t c){
 static constexpr uint8_t H[7]={17,17,17,31,17,17,17};
 static constexpr uint8_t I[7]={31,4,4,4,4,4,31};
 static constexpr uint8_t bang[7]={4,4,4,4,4,0,4};
 static constexpr uint8_t A[7]={14,17,17,31,17,17,17};
 static constexpr uint8_t B[7]={30,17,17,30,17,17,30};
 static constexpr uint8_t C[7]={14,17,16,16,16,17,14};
 static constexpr uint8_t D[7]={30,17,17,17,17,17,30};
 static constexpr uint8_t E[7]={31,16,16,30,16,16,31};
 static constexpr uint8_t F[7]={31,16,16,30,16,16,16};
 static constexpr uint8_t G[7]={14,17,16,23,17,17,14};
 static constexpr uint8_t J[7]={7,2,2,2,18,18,12};
 static constexpr uint8_t K[7]={17,18,20,24,20,18,17};
 static constexpr uint8_t L[7]={16,16,16,16,16,16,31};
 static constexpr uint8_t M[7]={17,27,21,21,17,17,17};
 static constexpr uint8_t N[7]={17,25,21,19,17,17,17};
 static constexpr uint8_t O[7]={14,17,17,17,17,17,14};
 static constexpr uint8_t P[7]={30,17,17,30,16,16,16};
 static constexpr uint8_t Q[7]={14,17,17,17,21,18,13};
 static constexpr uint8_t R[7]={30,17,17,30,20,18,17};
 static constexpr uint8_t S[7]={15,16,16,14,1,1,30};
 static constexpr uint8_t T[7]={31,4,4,4,4,4,4};
 static constexpr uint8_t U[7]={17,17,17,17,17,17,14};
 static constexpr uint8_t V[7]={17,17,17,17,17,10,4};
 static constexpr uint8_t W[7]={17,17,17,21,21,21,10};
 static constexpr uint8_t X[7]={17,17,10,4,10,17,17};
 static constexpr uint8_t Y[7]={17,17,10,4,4,4,4};
 static constexpr uint8_t Z[7]={31,1,2,4,8,16,31};
 static constexpr uint8_t space[7]={0,0,0,0,0,0,0};
 switch(c){case 'H':return H;case 'I':return I;case '!':return bang;
 case 'A':return A;case ' ':return space;
 case 'B':return B;case 'C':return C;case 'D':return D;
 case 'E':return E;case 'F':return F;case 'G':return G;
 case 'J':return J;case 'K':return K;case 'L':return L;
 case 'M':return M;case 'N':return N;case 'O':return O;
 case 'P':return P;case 'Q':return Q;case 'R':return R;
 case 'S':return S;case 'T':return T;case 'U':return U;
 case 'V':return V;case 'W':return W;case 'X':return X;
 case 'Y':return Y;case 'Z':return Z;
 default:return nullptr;}
}
bool diagnostic_glyph_supported(uint8_t c){return glyph(c)!=nullptr;}
bool draw_diagnostic_glyph(uint8_t ascii,double x,double baseline,
 double em,unsigned width,unsigned height,uint8_t* rgba,size_t stride,
 double x0,double y1,double sx,double sy,uint8_t red,uint8_t green,uint8_t blue){
 const auto* rows=glyph(ascii);
 if(!rows||!rgba||!width||!height||stride<size_t(width)*4||
    !std::isfinite(x)||!std::isfinite(baseline)||!std::isfinite(em)||
    em<=0||em>1000||!std::isfinite(sx)||!std::isfinite(sy)||
    sx<=0||sy<=0)return false;
 // Render within 0.6em advance, with ascenders 0.7em above baseline.
 const double cell_w=em*0.1,cell_h=em*0.1;
 for(int row=0;row<7;++row)for(int col=0;col<5;++col){
  if(!(rows[row]&(1u<<(4-col))))continue;
  const double xa=(x+col*cell_w-x0)*sx;
  const double xb=(x+(col+1)*cell_w-x0)*sx;
  const double ya=(y1-(baseline+(7-row)*cell_h))*sy;
  const double yb=(y1-(baseline+(6-row)*cell_h))*sy;
  if(!std::isfinite(xa)||!std::isfinite(xb)||!std::isfinite(ya)||!std::isfinite(yb)||
     xa<-1e8||xa>1e8||xb<-1e8||xb>1e8||
     ya<-1e8||ya>1e8||yb<-1e8||yb>1e8)return false;
  int left=std::max(0,int(std::floor(xa)));
  int right=std::min(int(width),int(std::ceil(xb)));
  int top=std::max(0,int(std::floor(ya)));
  int bottom=std::min(int(height),int(std::ceil(yb)));
  for(int py=top;py<bottom;++py)for(int px=left;px<right;++px){
   auto* pixel=rgba+size_t(py)*stride+size_t(px)*4;
   pixel[0]=red;pixel[1]=green;pixel[2]=blue;pixel[3]=255;
  }
 }
 return true;
}
}
