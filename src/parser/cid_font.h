#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
// PDF CIDToGIDMap: big-endian uint16 per CID. Identity is implicit.
struct CIDGlyphMap { bool identity=true; std::vector<uint8_t> bytes; };
inline bool cid_to_gid(const CIDGlyphMap& map,uint16_t cid,uint16_t& gid){
 if(map.identity){gid=cid;return true;}
 const size_t pos=size_t(cid)*2;
 if(pos>map.bytes.size()||map.bytes.size()-pos<2)return false;
 gid=uint16_t((uint16_t(map.bytes[pos])<<8)|map.bytes[pos+1]);
 return true;
}
inline bool cid_map_valid(const CIDGlyphMap& map){
 return map.identity?map.bytes.empty():
        !map.bytes.empty()&&map.bytes.size()%2==0&&
        map.bytes.size()<=131072;
}
}
