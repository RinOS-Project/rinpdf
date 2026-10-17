#include "../src/parser/tounicode.h"
#include <string>
using namespace rinpdf::parser;
int main(){
 const std::string cmap=
 "/CIDInit /ProcSet findresource begin\n"
 "12 dict begin\n"
 "begincmap\n"
 "1 begincodespacerange <00> <FF> endcodespacerange\n"
 "2 beginbfchar <41> <0041> <42> <20AC> endbfchar\n"
 "1 beginbfrange <50> <52> <0061> endbfrange\n"
 "endcmap\n";
 std::map<CMapKey,uint32_t> mapping;
 if(!parse_tounicode_cmap(reinterpret_cast<const uint8_t*>(cmap.data()),cmap.size(),mapping))return 1;
 if(mapping.size()!=5||mapping.at({1,0x41})!=0x41||
    mapping.at({1,0x42})!=0x20ac||mapping.at({1,0x52})!=0x63)return 2;
 const std::string two="1 beginbfchar <8140> <4E00> endbfchar";
 if(!parse_tounicode_cmap(reinterpret_cast<const uint8_t*>(two.data()),two.size(),mapping))return 3;
 if(mapping.size()!=1||mapping.at({2,0x8140})!=0x4e00)return 4;
 const std::string duplicated="2 beginbfchar <41> <0041> <41> <0042> endbfchar";
 if(parse_tounicode_cmap(reinterpret_cast<const uint8_t*>(duplicated.data()),
     duplicated.size(),mapping))return 5;
 if(mapping.size()!=1)return 6;
 const std::string array="1 beginbfrange <20> <22> [<0041><0042><0043>] endbfrange";
 if(parse_tounicode_cmap(reinterpret_cast<const uint8_t*>(array.data()),
     array.size(),mapping))return 7; // not implemented
 const std::string surrogate="1 beginbfchar <01> <D83DDE00> endbfchar";
 if(!parse_tounicode_cmap(reinterpret_cast<const uint8_t*>(surrogate.data()),
     surrogate.size(),mapping)||mapping.at({1,1})!=0x1f600)return 8;
 const std::string multi=
  "2 beginbfchar <41> <00410042> <42> <D83DDE00> endbfchar "
  "1 beginbfrange <50> <52> [<0061><00620063><0064>] endbfrange";
 std::map<CMapKey,std::u32string> sequences;
 if(!parse_tounicode_sequences(reinterpret_cast<const uint8_t*>(multi.data()),
    multi.size(),sequences))return 9;
 if(sequences.size()!=5||sequences.at({1,0x41})!=U"AB"||
    sequences.at({1,0x42})!=U"\U0001F600"||
    sequences.at({1,0x51})!=U"bc")return 10;
 const std::string malformed=
  "1 beginbfrange <50> <52> [<0061><0062>] endbfrange";
 if(parse_tounicode_sequences(reinterpret_cast<const uint8_t*>(malformed.data()),
    malformed.size(),sequences))return 11;
 if(sequences.size()!=5)return 12;
 return 0;
}
