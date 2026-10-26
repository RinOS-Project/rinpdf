#include "sfnt_gsub.h"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::renderer {
namespace {
bool read16(const uint8_t* d,size_t n,size_t p,uint16_t& v){
 if(!d||p>n||n-p<2)return false;
 v=uint16_t((uint16_t(d[p])<<8)|d[p+1]);return true;
}
bool read32(const uint8_t* d,size_t n,size_t p,uint32_t& v){
 if(!d||p>n||n-p<4)return false;
 v=(uint32_t(d[p])<<24)|(uint32_t(d[p+1])<<16)|
   (uint32_t(d[p+2])<<8)|d[p+3];return true;
}
bool coverage_index(const uint8_t* d,size_t n,size_t p,
 uint16_t glyph,int& index){
 uint16_t format=0,count=0;
 if(!read16(d,n,p,format)||!read16(d,n,p+2,count))return false;
 index=-1;
 if(format==1){
  if(count>4096||p>n||n-p<4+size_t(count)*2)return false;
  uint16_t previous=0;
  for(unsigned i=0;i<count;++i){
   uint16_t item=0;
   if(!read16(d,n,p+4+i*2,item)||(i&&item<=previous))return false;
   if(item==glyph)index=int(i);
   previous=item;
  }
 }else if(format==2){
  if(count>4096||p>n||n-p<4+size_t(count)*6)return false;
  uint16_t prev_end=0;
  uint32_t expected_start=0;
  for(unsigned i=0;i<count;++i){
   uint16_t first=0,last=0,start=0;
   if(!read16(d,n,p+4+i*6,first)||
      !read16(d,n,p+6+i*6,last)||
      !read16(d,n,p+8+i*6,start)||first>last||
      (i&&first<=prev_end)||start!=expected_start)return false;
   if(glyph>=first&&glyph<=last)index=int(start)+glyph-first;
   expected_start+=uint32_t(last)-first+1u;
   if(expected_start>65536u)return false;
   prev_end=last;
  }
 }else return false;
 return true;
}
bool single_lookup(const uint8_t* d,size_t n,size_t p,
 uint16_t glyph,uint16_t glyph_count,uint16_t& replacement,bool& found){
 uint16_t format=0,coverage_offset=0;
 if(!read16(d,n,p,format)||!read16(d,n,p+2,coverage_offset)||
    (format!=1&&format!=2)||coverage_offset<6||
    coverage_offset>=n-p)return false;
 if(format==2){
  uint16_t count=0;
  if(!read16(d,n,p+4,count)||count>4096||
     n-p<6+size_t(count)*2||
     coverage_offset<6+size_t(count)*2)return false;
 }
 int index=-1;
 if(!coverage_index(d,n,p+coverage_offset,glyph,index))return false;
 if(index<0)return true;
 uint16_t value=0;
 if(format==1){
  if(!read16(d,n,p+4,value))return false;
  replacement=uint16_t((uint32_t(glyph)+value)&65535);
 }else if(format==2){
  uint16_t count=0;
  if(!read16(d,n,p+4,count)||count>4096||
     index>=count||n-p<6+size_t(count)*2||
     !read16(d,n,p+6+size_t(index)*2,value))return false;
  replacement=value;
 }else return false;
 if(replacement>=glyph_count)return false;
 found=true;return true;
}
}
// Select only feature indices enabled by DFLT/default LangSys or a
// supported CJK script's default LangSys. Do not scan unrelated features.
bool active_features(const uint8_t* d,size_t n,uint16_t scripts,
 uint16_t feature_count,std::vector<bool>& enabled,
 uint32_t script_tag,uint32_t language_tag){
 uint16_t count=0;
 if(!read16(d,n,scripts,count)||count>4096||
    scripts>n||n-scripts<2+size_t(count)*6)return false;
 enabled.assign(feature_count,false);
 const uint32_t tags[]={script_tag,0x44464c54,0x68616e69,0x6b616e61,0x68616e67};
 for(uint32_t preferred:tags){
  if(!preferred)continue;
  for(unsigned i=0;i<count;++i){
   const size_t rec=scripts+2+size_t(i)*6;
   uint32_t tag=0;uint16_t offset=0;
   if(!read32(d,n,rec,tag)||!read16(d,n,rec+4,offset))return false;
   if(tag!=preferred)continue;
   const size_t script=size_t(scripts)+offset;
   uint16_t default_lang=0;
   if(offset<2||!read16(d,n,script,default_lang))return false;
   uint16_t lang_count=0;
   if(!read16(d,n,script+2,lang_count)||lang_count>4096||
      script>n||n-script<4+size_t(lang_count)*6)return false;
   uint16_t selected=default_lang;
   if(language_tag){
    for(unsigned j=0;j<lang_count;++j){
     uint32_t current=0;uint16_t relative=0;
     if(!read32(d,n,script+4+size_t(j)*6,current)||
        !read16(d,n,script+8+size_t(j)*6,relative))return false;
     if(current==language_tag){selected=relative;break;}
    }
   }
   // A script without a default LangSys may still supply a matching
   // language-specific LangSys; otherwise continue to the next script.
   if(selected==0)continue;
   // LangSys offsets are relative to the selected Script table.
   // Ensure they point beyond its header and language records.
   if(size_t(selected)<4+size_t(lang_count)*6)return false;
   const size_t lang=script+selected;
   uint16_t required=0,features=0;
   if(selected<4||!read16(d,n,lang+2,required)||
      !read16(d,n,lang+4,features)||features>4096||
      lang>n||n-lang<6+size_t(features)*2)return false;
   if(required!=0xffff){
    if(required>=feature_count)return false;
    enabled[required]=true;
   }
   for(unsigned j=0;j<features;++j){
    uint16_t index=0;
    if(!read16(d,n,lang+6+size_t(j)*2,index)||
       index>=feature_count)return false;
    enabled[index]=true;
   }
   return true;
  }
 }
 return true;
}
bool sfnt_vertical_substitution(const SfntFont& font,uint16_t glyph,
 uint16_t& replacement,uint32_t script_tag,uint32_t language_tag){
 if(!font.bytes||glyph>=font.glyph_count)return false;
 replacement=glyph;
 const SfntTable* table=nullptr;
 for(const auto& t:font.tables)if(t.tag==0x47535542){table=&t;break;}
 if(!table)return true;
 const uint8_t* data=font.bytes+table->offset;
 const size_t n=table->length;
 uint16_t major=0,minor=0,scripts=0,features=0,lookups=0;
 if(n<10||!read16(data,n,0,major)||!read16(data,n,2,minor)||
    !read16(data,n,4,scripts)||!read16(data,n,6,features)||!read16(data,n,8,lookups)||
    major!=1||(minor!=0&&minor!=1))return false;
 uint16_t feature_count=0,lookup_count=0;
 if(!read16(data,n,features,feature_count)||
    !read16(data,n,lookups,lookup_count)||
    feature_count>4096||lookup_count>4096||
    features>n||n-features<2+size_t(feature_count)*6||
    lookups>n||n-lookups<2+size_t(lookup_count)*2)return false;
 std::vector<bool> enabled;
 if(!active_features(data,n,scripts,feature_count,enabled,
       script_tag,language_tag))return false;
 // Prefer vrt2 to vert, which are mutually alternative vertical features.
 for(unsigned pass=0;pass<2;++pass){
  bool substituted=false;
  const uint32_t wanted=pass==0?0x76727432:0x76657274;
  for(unsigned i=0;i<feature_count;++i){
   const size_t rec=features+2+size_t(i)*6;
   uint32_t tag=0;uint16_t relative=0;
   if(!read32(data,n,rec,tag)||!read16(data,n,rec+4,relative))return false;
   if(tag!=wanted||!enabled[i])continue;
   const size_t feature=features+relative;
   uint16_t count=0;
   if(relative<2||!read16(data,n,feature+2,count)||
      count>4096||feature>n||n-feature<4+size_t(count)*2)return false;
   uint16_t current=glyph;
   bool feature_changed=false;
   for(unsigned j=0;j<count;++j){
    uint16_t lookup_index=0,lookup_relative=0,type=0,sub_count=0;
    if(!read16(data,n,feature+4+size_t(j)*2,lookup_index)||
       lookup_index>=lookup_count||
       !read16(data,n,lookups+2+size_t(lookup_index)*2,lookup_relative))
      return false;
    const size_t lookup=lookups+lookup_relative;
    if(lookup_relative<2||!read16(data,n,lookup,type)||
       !read16(data,n,lookup+4,sub_count)||sub_count>4096||
       lookup>n||n-lookup<6+size_t(sub_count)*2)return false;
    if(type!=1&&type!=7)continue; // Only SingleSubst and its extension.
    for(unsigned k=0;k<sub_count;++k){
     uint16_t sub_relative=0;
     if(!read16(data,n,lookup+6+size_t(k)*2,sub_relative)||
        sub_relative<6||sub_relative>=n-lookup)return false;
     size_t sub=lookup+sub_relative;
     if(type==7){
      // ExtensionSubst format 1 holds a 32-bit offset relative to itself.
      uint16_t format=0,extension_type=0;
      uint32_t extension_offset=0;
      if(!read16(data,n,sub,format)||
         !read16(data,n,sub+2,extension_type)||
         !read32(data,n,sub+4,extension_offset)||
         format!=1||extension_offset<8||
         extension_offset>=n-sub)return false;
      if(extension_type!=1)continue;
      sub+=extension_offset;
     }
     bool found=false;
     if(!single_lookup(data,n,sub,
           current,font.glyph_count,replacement,found))return false;
     if(found){current=replacement;feature_changed=true;break;}
    }
   }
   if(feature_changed){replacement=current;substituted=true;}
  }
  if(substituted)return true;
 }
 return true;
}
}
