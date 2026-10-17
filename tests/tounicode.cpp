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
 const std::string long_codes=
  "2 begincodespacerange <010000> <01FFFF> <10000000> <10FFFFFF> endcodespacerange "
  "2 beginbfchar <010203> <65E5> <10203040> <672C> endbfchar";
 if(!parse_tounicode_sequences(reinterpret_cast<const uint8_t*>(long_codes.data()),
     long_codes.size(),sequences))return 13;
 if(sequences.size()!=2||sequences.at({3,0x010203})!=U"\u65e5"||
    sequences.at({4,0x10203040})!=U"\u672c")return 14;
 const std::string bad_space=
  "1 begincodespacerange <0200> <0100> endcodespacerange "
  "1 beginbfchar <41> <0041> endbfchar";
 if(parse_tounicode_sequences(reinterpret_cast<const uint8_t*>(bad_space.data()),
     bad_space.size(),sequences))return 15;
 std::vector<std::pair<CMapKey,CMapKey>> ranges;
 if(!parse_tounicode_sequences(reinterpret_cast<const uint8_t*>(long_codes.data()),
     long_codes.size(),sequences,&ranges))return 16;
 if(ranges.size()!=2||ranges[0].first.width!=3||
    ranges[1].first.width!=4)return 17;
 const std::string outside=
  "1 begincodespacerange <8100> <81FF> endcodespacerange "
  "1 beginbfchar <8201> <0041> endbfchar";
 if(parse_tounicode_sequences(reinterpret_cast<const uint8_t*>(outside.data()),
     outside.size(),sequences,&ranges))return 18;
 const std::string inherited=
  "/Adobe-Identity-UCS usecmap 1 beginbfchar <41> <0041> endbfchar";
 if(parse_tounicode_sequences(reinterpret_cast<const uint8_t*>(inherited.data()),
    inherited.size(),sequences))return 19;
 const std::string ambiguous=
  "2 begincodespacerange <81> <81> <8100> <81FF> endcodespacerange "
  "1 beginbfchar <81> <0041> endbfchar";
 if(parse_tounicode_sequences(reinterpret_cast<const uint8_t*>(ambiguous.data()),
    ambiguous.size(),sequences))return 20;
 const std::map<CMapKey,std::u32string> parent{{{1,0x41},U"A"},{{1,0x42},U"B"}};
 const std::vector<std::pair<CMapKey,CMapKey>> parent_space{
   {{1,0x00},{1,0xff}}};
 const std::string derived=
   "/MyParent usecmap 1 beginbfchar <41> <005A> endbfchar";
 if(!parse_tounicode_sequences(
   reinterpret_cast<const uint8_t*>(derived.data()),derived.size(),
   sequences,&ranges,"MyParent",&parent,&parent_space))return 21;
 if(sequences.size()!=2||sequences.at({1,0x41})!=U"Z"||
    sequences.at({1,0x42})!=U"B"||ranges.size()!=1)return 22;
 if(parse_tounicode_sequences(
   reinterpret_cast<const uint8_t*>(derived.data()),derived.size(),
   sequences,&ranges,"WrongName",&parent,&parent_space))return 23;
 return 0;
}
