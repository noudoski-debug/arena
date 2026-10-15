// Metadata-only mock: proves type/offset handling, NOT the meaning of obfuscated qXK on device.
#include "../jni/src/Runtime.cpp"
#include <array>
#include <cassert>
#include <limits>
extern "C" int DobbyHook(void*,void*,void**) {return -1;}
namespace {
int classId=0,fieldId=0,typeId=0,kind=0x0C;
float health=100;
bool fieldExists=true;
int reads=0,lookups=0;
}
extern "C" void* il2cpp_object_get_class(void*) {return &classId;}
extern "C" void* il2cpp_class_get_field_from_name(void* klass,const char* name) {
    assert(klass==&classId && std::strcmp(name,"qXK")==0);++lookups;
    return fieldExists?&fieldId:nullptr;
}
extern "C" const void* il2cpp_field_get_type(void* field) {assert(field==&fieldId);return &typeId;}
extern "C" int il2cpp_type_get_type(const void* type) {assert(type==&typeId);return kind;}
extern "C" void il2cpp_field_get_value(void*,void* field,void* result) {
    assert(field==&fieldId);std::memcpy(result,&health,sizeof(health));++reads;
}
int main() {
    alignas(8) std::array<unsigned char,0xB0> event{};
    int wrapper=0;
    void* pointer=&wrapper;
    std::memcpy(event.data()+offsets::EntityHealth,&pointer,sizeof(pointer));
    float value=0;
    assert(!runtime::readHealth(nullptr,value));
    assert(runtime::readHealth(event.data(),value) && value==100 && reads==1 && lookups==1);
    health=0;assert(runtime::readHealth(event.data(),value) && value==0 && lookups==1);
    health=-1;assert(!runtime::readHealth(event.data(),value));
    health=std::numeric_limits<float>::quiet_NaN();assert(!runtime::readHealth(event.data(),value));
    health=std::numeric_limits<float>::infinity();assert(!runtime::readHealth(event.data(),value));
    const int before=reads;
    kind=0x13;assert(!runtime::readHealth(event.data(),value) && reads==before); // uninflated generic type
    kind=0x0E;assert(!runtime::readHealth(event.data(),value) && reads==before); // string, not float
    pointer=nullptr;std::memcpy(event.data()+offsets::EntityHealth,&pointer,sizeof(pointer));
    assert(!runtime::readHealth(event.data(),value));
}
