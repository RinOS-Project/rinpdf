#include "../src/renderer/sfnt.h"
#include "../src/renderer/sfnt_gsub.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cerrno>
int main(int argc,char** argv){
 if(argc<3){
  std::fprintf(stderr,"usage: rinpdf-gsub-font FONT.ttf GLYPH_ID [EXPECTED_VERTICAL_ID]\n");
  return 2;
 }
 errno=0;
 char* end=nullptr;
 const unsigned long raw=std::strtoul(argv[2],&end,0);
 if(errno||!end||*end||raw>65535){
  std::fprintf(stderr,"invalid glyph id\n");return 2;
 }
 std::ifstream file(argv[1],std::ios::binary);
 if(!file){std::fprintf(stderr,"cannot open font\n");return 2;}
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
 if(bytes.empty()||bytes.size()>64*1024*1024){
  std::fprintf(stderr,"font is empty or exceeds 64 MiB\n");return 2;
 }
 rinpdf::renderer::SfntFont sfnt;
 if(!rinpdf::renderer::parse_sfnt(bytes.data(),bytes.size(),sfnt)){
  std::fprintf(stderr,"invalid or unsupported SFNT font\n");return 1;
 }
 uint16_t vertical=0;
 if(!rinpdf::renderer::sfnt_vertical_substitution(
      sfnt,static_cast<uint16_t>(raw),vertical)){
  std::fprintf(stderr,"malformed or unsupported GSUB vertical substitution\n");
  return 1;
 }
 std::printf("glyph=%lu vertical=%u changed=%s total_glyphs=%u\n",
             raw,vertical,vertical==raw?"no":"yes",sfnt.glyph_count);
 if(argc>=4){
  errno=0;end=nullptr;
  const unsigned long expected=std::strtoul(argv[3],&end,0);
  if(errno||!end||*end||expected>65535)return 2;
  if(vertical!=expected){
   std::fprintf(stderr,"expected vertical glyph %lu, got %u\n",expected,vertical);
   return 1;
  }
 }
 return 0;
}
