#include "Effects.h"
#include <mutex>
#include <cmath>
namespace effects {
namespace { struct State { std::mutex mutex; std::vector<Event> events; };
State& state(){static State value;return value;} }
void push(Event event) {
    if(!std::isfinite(event.x)||!std::isfinite(event.y)||event.damage<0||event.damage>100000)return;
    auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);
    if(s.events.size()<64) s.events.push_back(event);
}
std::vector<Event> consume() {
    auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);std::vector<Event> copy;copy.swap(s.events);return copy;
}
}
extern "C" __attribute__((visibility("default")))
void EncoreNotifyHit(float x,float y,int damage,int kill) { effects::push({x,y,damage,kill!=0}); }
