#include "object.h"
#include <cstring>
#include <limits>
namespace rinpdf::parser {
static bool ws(unsigned char c){return c==0||c==9||c==10||c==12||c==13||c==32;}
static void skip(const uint8_t *d,size_t n,size_t &p){while(p<n&&ws(d[p]))++p;}
static bool number(const uint8_t *d,size_t n,size_t &p,size_t &value){
 skip(d,n,p);if(p==n||d[p]<'0'||d[p]>'9')return false;
 value=0;
 while(p<n&&d[p]>='0'&&d[p]<='9'){
  unsigned digit=d[p++]-'0';
  if(value>(std::numeric_limits<size_t>::max()-digit)/10)return false;
  value=value*10+digit;
 }
 return p==n||ws(d[p]);
}
bool resolve_object(const uint8_t *d,size_t n,const XrefEntry& e,size_t& begin,size_t& end){
 begin=end=0;if(!d||!e.in_use||e.offset>=n)return false;
 size_t p=e.offset,id=0,generation=0;
 if(!number(d,n,p,id)||!number(d,n,p,generation)||id!=e.object||generation!=e.generation)return false;
 skip(d,n,p);
 if(n-p<3||std::memcmp(d+p,"obj",3)!=0||(p+3<n&&!ws(d[p+3])))return false;
 p+=3;begin=p;
 // Limited resolver: refuse stream objects; binary streams can contain endobj.
 for(size_t i=p;i+6<=n;++i){
  if(i+6<=n&&std::memcmp(d+i,"stream",6)==0&&(i==p||ws(d[i-1]))&&(i+6==n||ws(d[i+6])))return false;
  if(std::memcmp(d+i,"endobj",6)==0&&(i==p||ws(d[i-1]))&&(i+6==n||ws(d[i+6]))){end=i;return true;}
 }
 return false;
}
}
