#include "../src/parser/value.h"
#include <cstring>
using namespace rinpdf::parser;
int main(){
 const char pdf[]="<< /Type /Catalog /Pages 2 0 R /Kids [3 0 R 4 0 R] /Count 2 /Enabled true /Empty null >>";
 Lexer lexer(reinterpret_cast<const unsigned char*>(pdf),sizeof(pdf)-1);
 Value root;
 if(!parse_value(lexer,root)||root.kind!=Value::Kind::Dictionary)return 1;
 if(root.dictionary.at("Type").text!="Catalog")return 2;
 const auto& pages=root.dictionary.at("Pages");
 if(pages.kind!=Value::Kind::Reference||pages.integer!=2||pages.generation!=0)return 3;
 const auto& kids=root.dictionary.at("Kids");
 if(kids.kind!=Value::Kind::Array||kids.array.size()!=2||kids.array[1].integer!=4)return 4;
 if(root.dictionary.at("Count").integer!=2||!root.dictionary.at("Enabled").boolean)return 5;
 if(root.dictionary.at("Empty").kind!=Value::Kind::Null)return 6;
 const char malformed[]="<< /A [1 2 >>";
 Lexer bad(reinterpret_cast<const unsigned char*>(malformed),sizeof(malformed)-1);
 if(parse_value(bad,root))return 7;
 const char duplicate[]="<< /A 1 /A 2 >>";
 Lexer dup(reinterpret_cast<const unsigned char*>(duplicate),sizeof(duplicate)-1);
 if(parse_value(dup,root))return 8;
 // Decimal operands retain their fractional part in PDF values.
 const std::string decimal_text="[412.5 -.25 +.75 1.]";
 Lexer decimals(reinterpret_cast<const uint8_t*>(decimal_text.data()),decimal_text.size());
 Value fractional;
 if(!parse_value(decimals,fractional)||fractional.kind!=Value::Kind::Array||
    fractional.array.size()!=4)return 80;
 const double values[]={412.5,-0.25,0.75,1.0};
 for(size_t i=0;i<4;++i)
  if(fractional.array[i].kind!=Value::Kind::Real||
     fractional.array[i].real!=values[i])return 81;
 return 0;
}
