#pragma once
#include <ArduinoJson.h>

// Wi-Fi station + mDNS + HTTP API. Credentials live in their own NVS namespace and are never echoed.
namespace net {

void begin();
void loop();
bool setCredentials(const char* ssid, const char* pass);  // saves and reconnects
void forget();
void status(JsonObject out);
bool connected();

}  // namespace net
