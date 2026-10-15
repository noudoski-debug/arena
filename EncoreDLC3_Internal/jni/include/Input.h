#pragma once
#include <vector>
namespace input {
struct Touch { int id; float x,y; };
bool install();
void pump();
std::vector<Touch> touches();
bool isDown(int linuxCode);
bool consumeMenuToggle();
}
