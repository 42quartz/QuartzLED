#pragma once
#include "state.h"

// Owner of the live state. Interfaces mutate a copy and commit it here.
namespace app {

const LedState& state();
const LedConfig& config();
void commitState(const LedState& s);    // applies now, persists after a quiet period
void commitConfig(const LedConfig& c);  // applies + persists now
void requestReboot();
void setTimer(uint16_t minutes);        // 0 cancels
uint32_t timerRemaining();              // seconds, 0 if none

}  // namespace app
