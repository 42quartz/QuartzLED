#pragma once
#include <ArduinoJson.h>

// Unsolicited messages (boot, wifi, later: button, remote, voice). Serial today; MQTT later.
namespace events {

void emit(JsonDocument& ev);
void flush(Print& out);  // call from loop()

}  // namespace events
