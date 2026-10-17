#include "../src/parser/object_stream.h"
#include <string>
#include <vector>
using namespace rinpdf::parser;
int main(){
 // Two objects: a dictionary and a numeric array.
 const std::string body="<< /Type /Catalog >> [1 2 3]";
 const std::string header="3 0 4 20 ";
 const std::string payload=header+body;
 std::vector<ObjectStreamItem> objects;
 if(!decode_object_stream(reinterpret_cast<const uint8_t*>(payload.data()),
       payload.size(),2,header.size(),objects))return 1;
 if(objects.size()!=2||objects[0].object!=3||
    objects[0].value.kind!=Value::Kind::Dictionary||
    objects[1].object!=4||objects[1].value.kind!=Value::Kind::Array)return 2;
 const size_t retained=objects.size();
 if(decode_object_stream(reinterpret_cast<const uint8_t*>(payload.data()),
       payload.size(),2,header.size()-1,objects))return 3;
 if(decode_object_stream(reinterpret_cast<const uint8_t*>(payload.data()),
       payload.size(),2,payload.size(),objects))return 4;
 const std::string duplicate="3 0 3 20 "+body;
 if(decode_object_stream(reinterpret_cast<const uint8_t*>(duplicate.data()),
       duplicate.size(),2,header.size(),objects))return 5;
 const std::string overlap="3 0 4 0 "+body;
 if(decode_object_stream(reinterpret_cast<const uint8_t*>(overlap.data()),
       overlap.size(),2,header.size(),objects))return 6;
 if(objects.size()!=retained)return 7;
 return 0;
}
