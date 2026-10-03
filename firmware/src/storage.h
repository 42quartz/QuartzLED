#pragma once
#include "state.h"

namespace storage {

void begin();
void load(LedState& s, LedConfig& c);
void saveState(const LedState& s);
void saveConfig(const LedConfig& c);
uint32_t bumpBootCount();

}  // namespace storage
