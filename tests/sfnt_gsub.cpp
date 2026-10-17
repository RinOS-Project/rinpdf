#include "../src/renderer/sfnt_gsub.h"
#include <vector>
using namespace rinpdf::renderer;
static void w16(std::vector<uint8_t>& b,unsigned p,unsigned v){
 b[p]=static_cast<uint8_t>(v>>8);b[p+1]=static_cast<uint8_t>(v);
}
static void w32(std::vector<uint8_t>& b,unsigned p,uint32_t v){
 b[p]=uint8_t(v>>24);b[p+1]=uint8_t(v>>16);
 b[p+2]=uint8_t(v>>8);b[p+3]=uint8_t(v);
}
int main(){
 std::vector<uint8_t> d(70);
 w16(d,0,1);w16(d,4,10);w16(d,6,30);w16(d,8,44);
 // ScriptList: DFLT -> default LangSys activates feature 0.
 w16(d,10,1);w32(d,12,0x44464c54);w16(d,16,8);
 w16(d,18,4);w16(d,20,0);
 w16(d,22,0);w16(d,24,0xffff);w16(d,26,1);w16(d,28,0);
 // FeatureList: vrt2 -> lookup 0.
 w16(d,30,1);w32(d,32,0x76727432);w16(d,36,8);
 w16(d,38,0);w16(d,40,1);w16(d,42,0);
 // SingleSubst format 2, glyph 1 -> glyph 2.
 w16(d,44,1);w16(d,46,4);
 w16(d,48,1);w16(d,50,0);w16(d,52,1);w16(d,54,8);
 w16(d,56,2);w16(d,58,8);w16(d,60,1);w16(d,62,2);
 w16(d,64,1);w16(d,66,1);w16(d,68,1);
 SfntFont f{d.data(),d.size(),4,{{0x47535542,0,70}}};
 uint16_t result=0;
 if(!sfnt_vertical_substitution(f,1,result)||result!=2)return 1;
 if(!sfnt_vertical_substitution(f,3,result)||result!=3)return 2;
 // An unselected feature must not substitute glyphs.
 w16(d,26,0);
 if(!sfnt_vertical_substitution(f,1,result)||result!=1)return 3;
 w16(d,26,1);
 w16(d,62,5);
 if(sfnt_vertical_substitution(f,1,result))return 4;
 w16(d,62,2);
 w16(d,58,0xff);
 if(sfnt_vertical_substitution(f,1,result))return 5;
 // ExtensionSubst (lookup 7) wraps SingleSubst (lookup 1).
 std::vector<uint8_t> extended=d;
 extended.resize(78,0);
 w16(extended,48,7); // LookupType 7
 w16(extended,56,1); // ExtensionSubstFormat
 w16(extended,58,1); // ExtensionLookupType: SingleSubst
 w32(extended,60,8); // Offset from ExtensionSubst
 w16(extended,64,2);w16(extended,66,8);
 w16(extended,68,1);w16(extended,70,2);
 w16(extended,72,1);w16(extended,74,1);w16(extended,76,1);
 SfntFont extended_font{extended.data(),extended.size(),4,
  {{0x47535542,0,static_cast<uint32_t>(extended.size())}}};
 if(!sfnt_vertical_substitution(extended_font,1,result)||result!=2)return 7;
 w32(extended,60,0xffffffff);
 if(sfnt_vertical_substitution(extended_font,1,result))return 8;
 w32(extended,60,8);
 w16(extended,58,7); // Nested extensions are not followed.
 if(!sfnt_vertical_substitution(extended_font,1,result)||result!=1)return 9;
 // Add a Japanese LangSys that enables the feature while default does not.
 std::vector<uint8_t> japanese(86,0);
 w16(japanese,0,1);w16(japanese,4,10);
 w16(japanese,6,40);w16(japanese,8,54);
 w16(japanese,10,1);w32(japanese,12,0x68616e69);w16(japanese,16,8);
 w16(japanese,18,0);w16(japanese,20,1);
 w32(japanese,22,0x4a414e20);w16(japanese,26,10);
 w16(japanese,28,0);w16(japanese,30,0xffff);
 w16(japanese,32,1);w16(japanese,34,0);
 w16(japanese,40,1);w32(japanese,42,0x76727432);
 w16(japanese,46,8);w16(japanese,48,0);w16(japanese,50,1);w16(japanese,52,0);
 d[58]=8; // Restore the valid coverage offset after malformed-input test.
 for(unsigned i=0;i<26;++i)japanese[54+i]=d[44+i];
 SfntFont jp{japanese.data(),japanese.size(),4,{{0x47535542,0,86}}};
 if(!sfnt_vertical_substitution(jp,1,result,0x68616e69,0x4a414e20)||
    result!=2)return 10;
 if(!sfnt_vertical_substitution(jp,1,result,0x68616e69)||
    result!=1)return 11;
 SfntFont no_gsub{d.data(),d.size(),4,{}};
 if(!sfnt_vertical_substitution(no_gsub,1,result)||result!=1)return 6;
 return 0;
}
