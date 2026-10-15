#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace rinpdf::parser {
enum class TokenKind { End, Name, Integer, Keyword, DictOpen, DictClose, ArrayOpen, ArrayClose, String, HexString, Invalid };
struct Token { TokenKind kind=TokenKind::Invalid; std::string value; };
class Lexer {
 public:
 Lexer(const uint8_t *bytes,size_t length):data(bytes),size(length){}
 Token next();
 size_t position() const { return offset; }
 private:
 const uint8_t *data;
 size_t size;
 size_t offset=0;
 void skip_space();
};
}
