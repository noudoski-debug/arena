#pragma once
namespace input {
bool install();
void pump(); // Render thread only; never access ImGui from input callbacks.
}
