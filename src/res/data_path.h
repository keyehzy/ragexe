#pragma once

#include <string>
#include <string_view>

namespace ragexe::res {

// MakeDataPath (004dd5a0): "data\" + name; from a ':' on, only the part starting one
// character before it (an absolute "C:\..." path); every "//" collapsed to "/".
std::string MakeDataPath(std::string_view name);

}  // namespace ragexe::res
