#pragma once
#include "Vars.h"
#include <string>
#include <vector>
namespace config {
bool save(const std::string& name,const AllVars& vars);
bool load(const std::string& name,AllVars& vars);
bool remove(const std::string& name);
std::vector<std::string> list();
const std::string& status();
#ifdef ENCORE_HOST_TEST
void setDirectory(const std::string& path);
#endif
}
