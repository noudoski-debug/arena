#pragma once
#include <vector>
namespace effects {
struct Event { float x,y; int damage; bool kill; };
void push(Event event);
std::vector<Event> consume();
}
// Optional integration seam for a future verified game damage/death hook.
// The supplied Encore source does not contain those event hooks.
extern "C" void EncoreNotifyHit(float x,float y,int damage,int kill);
