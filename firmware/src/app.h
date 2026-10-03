#pragma once
#include "state.h"

// Owner of the live state. Interfaces mutate a copy and commit it here.
namespace app {

const LedState& state();
const LedConfig& config();
void commitState(const LedState& s);    // applies now, persists after a quiet period
void commitConfig(const LedConfig& c);  // applies + persists now
void requestReboot();

}  // namespace app
