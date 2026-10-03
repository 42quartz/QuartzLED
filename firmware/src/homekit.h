#pragma once

// HomeSpan bridge: one LightBulb (on/brightness/hue/saturation) + one Switch per effect.
// HomeSpan owns Wi-Fi, mDNS (quartzled.local) and OTA; see net.cpp for the HTTP API that runs beside it.
namespace homekit {

void begin(const char* ssid, const char* pass);  // ssid may be empty (Wi-Fi stays off)
void loop();                                     // HomeSpan poll + push state changes to HomeKit
void eraseWifi();                                // clears HomeSpan's copy of the credentials and reboots
void unpair();                                   // forgets all Home controllers; device becomes pairable again

}  // namespace homekit
