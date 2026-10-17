#include "../src/renderer/text_show.h"
#include <cstdint>
#include <string>
#include <vector>
using namespace rinpdf;
int main(){
 const std::string good="[(Hi) -120 <4142> 25.5 (z)]";
 parser::Lexer lex(reinterpret_cast<const uint8_t*>(good.data()),good.size());
 if(lex.next().kind!=parser::TokenKind::ArrayOpen)return 1;
 std::vector<renderer::TextRun> runs;
 if(!renderer::parse_text_array(lex,runs))return 2;
 if(runs.size()!=5||runs[0].bytes!="Hi"||!runs[1].adjustment||
    runs[1].displacement!=-120||runs[2].bytes!="AB"||
    runs[3].displacement!=25.5||runs[4].bytes!="z")return 3;
 const std::string invalid="[(abc) /Name]";
 parser::Lexer bad(reinterpret_cast<const uint8_t*>(invalid.data()),invalid.size());
 bad.next();
 if(renderer::parse_text_array(bad,runs))return 4;
 if(runs.size()!=5)return 5; // preserve output on rejection
 const std::string unfinished="[(abc) -1";
 parser::Lexer unterminated(reinterpret_cast<const uint8_t*>(unfinished.data()),unfinished.size());
 unterminated.next();
 if(renderer::parse_text_array(unterminated,runs))return 6;
 return 0;
}
