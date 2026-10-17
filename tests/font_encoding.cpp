#include "../src/parser/font_encoding.h"
#include <string>
using namespace rinpdf::parser;
int main(){
 const std::string bytes="[65 /A /B 90 /Euro]";
 Lexer lex(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size());
 Value array;
 if(!parse_value(lex,array))return 1;
 std::map<uint8_t,uint32_t> mapping;
 if(!decode_font_differences(array,mapping))return 2;
 if(mapping.size()!=3||mapping.at(65)!=65||mapping.at(66)!=66||
    mapping.at(90)!=0x20ac)return 3;
 const std::string invalid="[255 /A /B]";
 Lexer invalid_lexer(reinterpret_cast<const uint8_t*>(invalid.data()),invalid.size());
 Value bad;
 if(!parse_value(invalid_lexer,bad))return 4;
 if(decode_font_differences(bad,mapping))return 5;
 if(mapping.size()!=3)return 6;
 const std::string unknown="[65 /UnrecognizedGlyph]";
 Lexer unknown_lexer(reinterpret_cast<const uint8_t*>(unknown.data()),unknown.size());
 if(!parse_value(unknown_lexer,bad))return 7;
 if(decode_font_differences(bad,mapping))return 8;
 uint32_t unicode=0;
 if(!font_byte_unicode("WinAnsiEncoding",0x80,{},unicode)||
    unicode!=0x20ac)return 9;
 if(font_byte_unicode("WinAnsiEncoding",0x81,{},unicode))return 10;
 if(!font_byte_unicode("MacRomanEncoding",0x80,{},unicode)||
    unicode!=0x00c4)return 11;
 if(!font_byte_unicode("MacRomanEncoding",0xdb,{},unicode)||
    unicode!=0x20ac)return 12;
 if(!font_byte_unicode("StandardEncoding",39,{},unicode)||
    unicode!=0x2019)return 13;
 if(font_byte_unicode("StandardEncoding",0x80,{},unicode))return 14;
 if(!font_byte_unicode("WinAnsiEncoding",65,{{65,0x00c1}},unicode)||
    unicode!=0x00c1)return 15;
 return 0;
}
