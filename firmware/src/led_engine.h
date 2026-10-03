#pragma once
#include "state.h"

namespace led {

void begin(const LedConfig& cfg);
void setState(const LedState& s);
void setConfig(const LedConfig& cfg);  // count/order/power live; chip needs reboot
void loop();                           // call often; renders at ~60 fps
void blank();                          // all off immediately (e.g. before OTA)
void startSunrise(uint16_t minutes);   // restarts the sunrise/sunset ramp (FX_SUNRISE, FX_SUNSET)

}  // namespace led
