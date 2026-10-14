#pragma once
#include <algorithm>
#include <cmath>
namespace modules {
struct Vec2 { float x=0, y=0; };
struct Vec3 { float x=0, y=0, z=0; };
struct Quaternion { float x=0, y=0, z=0, w=1; };
inline Vec3 operator+(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a,float t) { return {a.x*t,a.y*t,a.z*t}; }
inline float length(Vec3 v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
inline float wrap(float angle) { return std::remainder(angle,360.f); }
inline Quaternion rotation(float yaw,float pitch) {
    constexpr float rad=0.00872664626f;
    float cy=std::cos(yaw*rad),sy=std::sin(yaw*rad),cp=std::cos(pitch*rad),sp=std::sin(pitch*rad);
    return {sp*cy,cp*sy,-sp*sy,cp*cy};
}
inline Vec2 angles(Vec3 direction) {
    constexpr float deg=57.2957795f;
    return {std::atan2(direction.x,direction.z)*deg,
            -std::atan2(direction.y,std::hypot(direction.x,direction.z))*deg};
}
inline Vec2 aimStep(Vec2 current,Vec2 target,float smooth,float dt) {
    smooth=std::clamp(smooth,0.f,100.f);
    dt=std::clamp(dt,0.f,0.1f);
    const float response=60.f*std::pow(0.5f/60.f,smooth/100.f);
    const float blend=dt<=0.f?0.f:(smooth==0.f?1.f:1.f-std::exp(-response*dt));
    return {current.x+wrap(target.x-current.x)*blend,
            std::clamp(current.y+wrap(target.y-current.y)*blend,-85.f,85.f)};
}
inline Vec3 bone(Vec3 feet,Vec3 head,Vec3 right,int index) {
    static constexpr float height[]={1.f,.91f,.73f,.52f,.73f,.27f};
    index=std::clamp(index,0,5);
    Vec3 result=feet+(head-feet)*height[index];
    if (index==4 || index==5) result=result+right*(index==4?.34f:.14f);
    return result;
}
struct Controls { float moveX=0, moveY=0, lookX=0, lookY=0, vertical=0; bool aimHeld=false; };
// Restore a temporary field override before leaving the SAME game callback.
// No configuration/weapon pointer is retained past the original function call.
template<class T> class ScopedValue {
    T* address_=nullptr; T original_{};
public:
    ScopedValue(T* address,T replacement,bool enabled):address_(enabled?address:nullptr) {
        if (address_) { original_=*address_; *address_=replacement; }
    }
    ~ScopedValue() { if (address_) *address_=original_; }
    ScopedValue(const ScopedValue&)=delete;
    ScopedValue& operator=(const ScopedValue&)=delete;
};
}
