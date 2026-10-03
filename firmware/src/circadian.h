#pragma once
#include <ArduinoJson.h>

// "Ayılma": daily sunrise/sunset light ramps that run even while the light is off.
//   interval: user clock times (wake HH:MM ends the sunrise ramp; sleep HH:MM starts the sunset ramp)
//   auto:     real sky at a saved location — sunrise ramp spans civil dawn -> sunrise,
//             sunset ramp spans sunset -> civil dusk, then the light switches off.
// Time comes from NTP; the plan is independent of the current effect once armed.
namespace circadian {

void begin();
void loop();
void command(JsonObjectConst req, JsonDocument& resp);  // get/set config, reports today's times

}  // namespace circadian
