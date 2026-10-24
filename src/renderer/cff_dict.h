#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::renderer {
// CFF 1 DICT parser. Operators are reported with their numeric operands;
// malformed encodings and out-of-bounds reads always fail closed.
template<class Visitor>
bool cff_walk_dict(const uint8_t* d,size_t n,size_t first,size_t last,
                   Visitor visit){
 if(!d||first>last||last>n)return false;
 std::vector<int32_t> args;
 for(size_t p=first;p<last;){
  unsigned op=d[p++];
  if(op<=21){
   if(op==12){
    if(p>=last)return false;
    op=0x100u+d[p++];
   }
   if(!visit(op,args))return false;
   args.clear();
   continue;
  }
  int32_t value=0;
  if(op>=32&&op<=246)value=int32_t(op)-139;
  else if(op>=247&&op<=250){
   if(p>=last)return false;
   value=(int32_t(op)-247)*256+d[p++]+108;
  }else if(op>=251&&op<=254){
   if(p>=last)return false;
   value=-(int32_t(op)-251)*256-d[p++]-108;
  }else if(op==28){
   if(last-p<2)return false;
   value=int16_t((uint16_t(d[p])<<8)|d[p+1]);p+=2;
  }else if(op==29){
   if(last-p<4)return false;
   uint32_t raw=uint32_t(d[p])<<24|uint32_t(d[p+1])<<16|
                 uint32_t(d[p+2])<<8|d[p+3];
   value=static_cast<int32_t>(raw);p+=4;
  }else if(op==30){
   // A real number can be skipped but is not representable as an offset.
   bool terminated=false;
   while(p<last&&!terminated){
    const unsigned pair=d[p++];
    terminated=((pair>>4)==15||(pair&15)==15);
   }
   if(!terminated)return false;
   // Sentinel cannot silently serve as a positive INDEX offset.
   value=INT32_MIN;
  }else return false;
  if(args.size()>=48)return false;
  args.push_back(value);
 }
 return args.empty();
}
}
