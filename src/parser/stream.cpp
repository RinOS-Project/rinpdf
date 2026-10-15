#include "stream.h"
#include <cstring>
#include <utility>
namespace rinpdf::parser {
static bool space(unsigned char c){return c==0||c==9||c==10||c==12||c==13||c==32;}
static bool keyword(const uint8_t *data,size_t size,size_t pos,const char *word,size_t len){
 return pos<=size&&len<=size-pos&&std::memcmp(data+pos,word,len)==0&&(pos+len==size||space(data[pos+len]));
}
bool locate_stream(const uint8_t *data,size_t size,StreamRange& out){
 out=StreamRange{};if(!data||size==0)return false;
 Lexer lexer(data,size);Value dict;
 if(!parse_value(lexer,dict)||dict.kind!=Value::Kind::Dictionary)return false;
 auto it=dict.dictionary.find("Length");
 if(it==dict.dictionary.end()||it->second.kind!=Value::Kind::Integer||it->second.integer<0)return false;
 auto amount=(unsigned long long)it->second.integer;
 size_t pos=lexer.position();
 while(pos<size&&space(data[pos]))++pos;
 if(!keyword(data,size,pos,"stream",6))return false;
 pos+=6;
 if(pos==size)return false;
 if(data[pos]=='\r'){++pos;if(pos<size&&data[pos]=='\n')++pos;}
 else if(data[pos]=='\n')++pos;
 else return false;
 if(amount>size-pos)return false;
 size_t end=pos+(size_t)amount;
 size_t marker=end;
 if(marker<size&&data[marker]=='\r'){++marker;if(marker<size&&data[marker]=='\n')++marker;}
 else if(marker<size&&data[marker]=='\n')++marker;
 if(!keyword(data,size,marker,"endstream",9))return false;
 marker+=9;
 while(marker<size&&space(data[marker]))++marker;
 if(marker!=size)return false;
 out.dictionary=std::move(dict);out.offset=pos;out.length=(size_t)amount;
 return true;
}
}
