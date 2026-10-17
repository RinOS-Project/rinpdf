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
// Compute horizontal displacement for Standard-14 Courier family only.
// Widths are 600/1000 em per single-byte character; other fonts/encodings
// must not silently use Courier metrics. TJ numbers subtract thousandths.
bool courier_text_advance(const std::string& base_font,
 const std::vector<TextRun>& runs,double font_size,
 double char_spacing,double word_spacing,double horizontal_scale,
 double& advance);
// Parse a PDF TJ array after its '[' token. Does not render glyphs.
bool parse_text_array(parser::Lexer& lexer,std::vector<TextRun>& runs);
}
