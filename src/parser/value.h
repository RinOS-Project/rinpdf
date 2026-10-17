#pragma once
#include "lexer.h"
#include <map>
#include <vector>
namespace rinpdf::parser {
struct Value {
 enum class Kind { Null, Boolean, Integer, Real, Name, String, Array, Dictionary, Reference, Invalid };
 Kind kind=Kind::Invalid;
 long long integer=0;
 double real=0;
 bool boolean=false;
 unsigned generation=0;
 std::string text;
 std::vector<Value> array;
 std::map<std::string,Value> dictionary;
};
bool parse_value(Lexer& lexer,Value& result,unsigned max_depth=32);
}
