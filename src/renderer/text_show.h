#pragma once
#include "../parser/lexer.h"
#include <cstdint>
#include <string>
#include <vector>
namespace rinpdf::renderer {
struct TextRun {
 bool adjustment=false;
 std::string bytes;
 double displacement=0;
};
// Parse a PDF TJ array after its '[' token. Does not render glyphs.
bool parse_text_array(parser::Lexer& lexer,std::vector<TextRun>& runs);
}
