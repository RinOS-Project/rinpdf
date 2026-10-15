#include "../src/parser/lexer.h"
#include <cstring>
using namespace rinpdf::parser;
int main(){
 const char input[]="%PDF-1.4\n<< /Type /Page /Name#20Here (a\\(b\\)c) <4142F> /Kids [2 0 R] >>";
 Lexer lexer(reinterpret_cast<const unsigned char*>(input),sizeof(input)-1);
 auto expect=[&](TokenKind kind,const char *value){auto t=lexer.next();return t.kind==kind&&t.value==value;};
 if(!expect(TokenKind::DictOpen,"<<"))return 1;
 if(!expect(TokenKind::Name,"Type")||!expect(TokenKind::Name,"Page"))return 2;
 if(!expect(TokenKind::Name,"Name Here"))return 3;
 if(!expect(TokenKind::String,"a(b)c"))return 4;
 auto hex=lexer.next();
 if(hex.kind!=TokenKind::HexString||hex.value.size()!=3||(unsigned char)hex.value[2]!=0xF0)return 5;
 if(!expect(TokenKind::Name,"Kids")||!expect(TokenKind::ArrayOpen,"["))return 6;
 if(!expect(TokenKind::Integer,"2")||!expect(TokenKind::Integer,"0")||!expect(TokenKind::Keyword,"R"))return 7;
 if(!expect(TokenKind::ArrayClose,"]")||!expect(TokenKind::DictClose,">>")||!expect(TokenKind::End,""))return 8;
 const char invalid[]="(unterminated";
 Lexer bad(reinterpret_cast<const unsigned char*>(invalid),sizeof(invalid)-1);
 if(bad.next().kind!=TokenKind::Invalid)return 9;
 return 0;
}
