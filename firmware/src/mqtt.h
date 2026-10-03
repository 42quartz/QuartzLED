#pragma once
#include <ArduinoJson.h>

// MQTT client: v1 commands in, retained state out, Home Assistant discovery.
//   quartzled/<id>/cmd    <- v1 JSON request          quartzled/<id>/resp  -> reply
//   quartzled/<id>/state  -> retained state JSON      quartzled/<id>/event -> boot/wifi/...
//   quartzled/<id>/avail  -> online/offline (LWT)
// HA entities are pure discovery templates over those topics; the device has no HA-specific logic.
namespace mqtt {

void begin();
void loop();
void configure(JsonObjectConst req, JsonDocument& resp);  // host/port/user/pass; status if empty
void publishEvent(const char* json);
void presetsChanged(const char* removed = nullptr);       // re-announce scenes; drop a deleted one

}  // namespace mqtt
