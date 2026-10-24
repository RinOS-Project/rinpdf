#include "sfnt.h"
namespace rinpdf::renderer {
bool sfnt_cff_simple_outline(const SfntFont& font,uint16_t glyph,
 std::vector<CffPath>& contours,double tolerance){
 contours.clear();
 CffCommands commands;
 if(!sfnt_cff_outline_commands(font,glyph,commands))return false;
 return cff_flatten_commands(commands,tolerance,contours);
}
}
