#include "lexer.h"
#include <limits>
namespace rinpdf::parser {
static bool white(unsigned char c){return c==0||c==9||c==10||c==12||c==13||c==32;}
static bool delimiter(unsigned char c){return white(c)||c=='/'||c=='<'||c=='>'||c=='['||c==']'||c=='('||c==')'||c=='{'||c=='}'||c=='%';}
static int hex(unsigned char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}
void Lexer::skip_space(){
 while(offset<size){unsigned char c=data[offset];if(white(c)){++offset;continue;}if(c=='%'){while(offset<size&&data[offset]!='\n'&&data[offset]!='\r')++offset;continue;}break;}
}
Token Lexer::next(){
 if(!data)return {TokenKind::Invalid,{}};
 skip_space();if(offset==size)return {TokenKind::End,{}};
 unsigned char c=data[offset++];
 if(c=='[')return {TokenKind::ArrayOpen,"["};
 if(c==']')return {TokenKind::ArrayClose,"]"};
 if(c=='<'&&offset<size&&data[offset]=='<'){++offset;return {TokenKind::DictOpen,"<<"};}
 if(c=='>'&&offset<size&&data[offset]=='>'){++offset;return {TokenKind::DictClose,">>"};}
 if(c=='<'){
  std::string out;int hi=-1;
  while(offset<size){unsigned char ch=data[offset++];if(ch=='>'){if(hi>=0)out.push_back((char)(hi<<4));return {TokenKind::HexString,out};}if(white(ch))continue;int v=hex(ch);if(v<0)return {TokenKind::Invalid,{}};if(hi<0)hi=v;else{out.push_back((char)((hi<<4)|v));hi=-1;}}
  return {TokenKind::Invalid,{}};
 }
 if(c=='('){
  std::string out;unsigned depth=1;
  while(offset<size){
   unsigned char ch=data[offset++];
   if(ch=='\\'){
    if(offset==size)return {TokenKind::Invalid,{}};
    unsigned char escaped=data[offset++];
    switch(escaped){
     case 'n':out+='\n';break;
     case 'r':out+='\r';break;
     case 't':out+='\t';break;
     case 'b':out+='\b';break;
     case 'f':out+='\f';break;
     case '\r':
      if(offset<size&&data[offset]=='\n')++offset;
      break;
     case '\n':break;
     default:
      if(escaped>='0'&&escaped<='7'){
       unsigned value=escaped-'0';
       for(unsigned i=1;i<3&&offset<size;++i){
        const unsigned char digit=data[offset];
        if(digit<'0'||digit>'7')break;
        value=value*8+(digit-'0');++offset;
       }
       out+=static_cast<char>(value&255);
      }else out+=static_cast<char>(escaped);
    }
    continue;
   }
   if(ch=='('){if(++depth>64)return {TokenKind::Invalid,{}};out+='(';continue;}
   if(ch==')'){if(--depth==0)return {TokenKind::String,out};out+=')';continue;}
   if(ch=='\r'){
    if(offset<size&&data[offset]=='\n')++offset;
    out+='\n';
   }else out+=static_cast<char>(ch);
   if(out.size()>16*1024*1024)return {TokenKind::Invalid,{}};
  }
  return {TokenKind::Invalid,{}};
 }
 if(c=='/'){
  std::string name;
  while(offset<size&&!delimiter(data[offset])){if(data[offset]=='#'){if(offset+2>=size)return {TokenKind::Invalid,{}};int a=hex(data[offset+1]),b=hex(data[offset+2]);if(a<0||b<0)return {TokenKind::Invalid,{}};name+=(char)((a<<4)|b);offset+=3;}else name+=(char)data[offset++];}
  return {TokenKind::Name,name};
 }
 if(delimiter(c))return {TokenKind::Invalid,{}};
 std::string word(1,(char)c);
 while(offset<size&&!delimiter(data[offset]))word+=(char)data[offset++];
 bool integer=!word.empty();size_t start=(word[0]=='+'||word[0]=='-')?1:0;
 if(start==word.size())integer=false;
 for(size_t i=start;i<word.size();++i)if(word[i]<'0'||word[i]>'9')integer=false;
 if(integer)return {TokenKind::Integer,word};
 // PDF real tokens have an optional sign and a single decimal point.
 bool real=true,point=false,digit=false;
 for(size_t i=start;i<word.size();++i){
  if(word[i]=='.'&&!point){point=true;continue;}
  if(word[i]>='0'&&word[i]<='9'){digit=true;continue;}
  real=false;break;
 }
 return {real&&point&&digit?TokenKind::Real:TokenKind::Keyword,word};
}
}
