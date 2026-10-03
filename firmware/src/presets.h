#pragma once
#include <ArduinoJson.h>

#include "state.h"

// Named looks: built-in scenes plus up to kMaxUser user-saved ones (NVS).
namespace presets {

constexpr int kMaxUser = 8;

void begin();
bool find(const char* name, LedState& out);              // user presets shadow built-ins
bool save(const char* name, const LedState& s);          // false if full or bad name
bool remove(const char* name);                           // user presets only
void list(JsonObject out);                               // {"builtin":[...],"user":[...]}

}  // namespace presets
