#include "xref.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace rinpdf::parser {
static bool white(uint8_t c){return c==0||c==9||c==10||c==12||c==13||c==32;}
static void ws(const uint8_t *d,size_t n,size_t &p){while(p<n&&white(d[p]))++p;}
static bool decimal(const uint8_t *d,size_t n,size_t &p,size_t &v){
 ws(d,n,p);if(p==n||d[p]<'0'||d[p]>'9')return false;
 v=0;
 do{unsigned x=d[p++]-'0';if(v>(std::numeric_limits<size_t>::max()-x)/10)return false;v=v*10+x;}
 while(p<n&&d[p]>='0'&&d[p]<='9');
 if(p<n&&!white(d[p])&&d[p]!='<'&&d[p]!='>')return false;
 return true;
}
static bool keyword(const uint8_t *d,size_t n,size_t &p,const char *s){
 ws(d,n,p);size_t len=std::strlen(s);
 if(len>n-p||std::memcmp(d+p,s,len))return false;
 if(p+len<n&&!white(d[p+len])&&d[p+len]!='<')return false;
 p+=len;return true;
}
bool find_startxref(const uint8_t* d,size_t n,size_t& offset){
 if(!d||n<20)return false;
 constexpr char marker[]="startxref";
 constexpr size_t len=sizeof(marker)-1;
 for(size_t p=n-len+1;p>0;){
  --p;
  if(std::memcmp(d+p,marker,len)!=0)continue;
  if(p>0&&!white(d[p-1]))continue;
  size_t value=0,pos=p+len;
  if(!decimal(d,n,pos,value)||value>=n)return false;
  offset=value;
  return true;
 }
 return false;
}
bool read_classic_xref(const uint8_t *d,size_t n,std::vector<XrefEntry>& entries,size_t *trailer_offset){
 entries.clear();if(trailer_offset)*trailer_offset=0;if(!d||n<20)return false;
 size_t offset=0;
 if(!find_startxref(d,n,offset))return false;
 size_t p=offset;
 if(!keyword(d,n,p,"xref"))return false;
 size_t total=0;
 while(true){
  ws(d,n,p);
  if(p+7<=n&&std::memcmp(d+p,"trailer",7)==0){
   p+=7;ws(d,n,p);
   if(p+2>n||d[p]!='<'||d[p+1]!='<'||total==0)return false;
   if(trailer_offset)*trailer_offset=p;
   return true;
  }
  size_t first=0,count=0;
  if(!decimal(d,n,p,first)||!decimal(d,n,p,count)||count==0||count>1000000||first>1000000-count||total>1000000-count)return false;
  total+=count;
  for(size_t k=0;k<count;++k){
   size_t off=0,gen=0;
   if(!decimal(d,n,p,off)||!decimal(d,n,p,gen)||gen>65535)return false;
   ws(d,n,p);if(p>=n)return false;
   char flag=(char)d[p++];
   if(flag!='n'&&flag!='f')return false;
   if(p<n&&!white(d[p]))return false;
   if(flag=='n'&&off>=n)return false;
   entries.push_back({first+k,off,(unsigned)gen,flag=='n'});
  }
 }
}
}
