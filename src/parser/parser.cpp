#include "parser.h"
#include <cctype>
#include <cstring>
#include <limits>
namespace rinpdf::parser {
static bool space(unsigned char c){return c==0||c==9||c==10||c==12||c==13||c==32;}
static bool digit(unsigned char c){return c>='0'&&c<='9';}
static bool boundary(unsigned char c){return space(c)||c=='/'||c=='<'||c=='>'||c=='['||c==']'||c=='('||c==')';}
static void skip(const uint8_t *p,size_t n,size_t &i){
 while(i<n){if(space(p[i])){++i;continue;}if(p[i]=='%'){while(i<n&&p[i]!='\n'&&p[i]!='\r')++i;continue;}break;}
}
static bool number(const uint8_t *p,size_t n,size_t &i){
 size_t start=i;while(i<n&&digit(p[i]))++i;
 return i>start && (i==n||boundary(p[i]));
}
static bool word(const uint8_t *p,size_t n,size_t &i,const char *s){
 size_t len=std::strlen(s);
 if(len>n-i||std::memcmp(p+i,s,len)!=0)return false;
 if(i+len<n&&!boundary(p[i+len]))return false;
 i+=len;return true;
}
static bool page_dictionary(const uint8_t *p,size_t n,size_t begin,size_t end){
 // Check the top-level dictionary only; do not match /Type in nested resources.
 size_t depth=0;
 for(size_t i=begin;i+1<end;){
  if(p[i]=='%'){while(i<end&&p[i]!='\n'&&p[i]!='\r')++i;continue;}
  if(p[i]=='('){++i;int nesting=1;while(i<end&&nesting){if(p[i]=='\\'){i+=(i+1<end?2:1);continue;}if(p[i]=='(')++nesting;else if(p[i]==')')--nesting;++i;}continue;}
  if(p[i]=='<'&&p[i+1]=='<'){++depth;i+=2;continue;}
  if(p[i]=='>'&&p[i+1]=='>'){if(depth) --depth;i+=2;continue;}
  if(depth==1&&p[i]=='/'&&i+5<=end&&std::memcmp(p+i,"/Type",5)==0&&(i+5==end||boundary(p[i+5]))){
   size_t j=i+5;skip(p,end,j);
   if(j+5<=end&&std::memcmp(p+j,"/Page",5)==0&&(j+5==end||boundary(p[j+5])))return true;
  }
  ++i;
 }
 return false;
}
bool count_pages(const uint8_t *p,size_t n,size_t &pages){
 pages=0;if(!p||n<8)return false;
 // This first milestone does not support object streams or encrypted PDFs.
 size_t i=8;bool found=false;
 while(i<n){
  skip(p,n,i);if(i>=n)break;
  size_t start=i,j=i;
  if(!number(p,n,j)){++i;continue;}
  skip(p,n,j);if(!number(p,n,j)){i=start+1;continue;}
  skip(p,n,j);if(!word(p,n,j,"obj")){i=start+1;continue;}
  found=true;
  size_t end=j;
  while(end+6<=n && std::memcmp(p+end,"endobj",6)!=0)++end;
  if(end+6>n)return false;
  // Reject stream-containing objects rather than accidentally interpreting stream bytes as objects.
  bool stream=false;
  for(size_t k=j;k+6<=end;++k)if(std::memcmp(p+k,"stream",6)==0&&(k==j||boundary(p[k-1]))&&(k+6==end||boundary(p[k+6]))){stream=true;break;}
  if(!stream&&page_dictionary(p,n,j,end)){
   if(pages==std::numeric_limits<size_t>::max())return false;
   ++pages;
  }
  i=end+6;
 }
 return found;
}
}
