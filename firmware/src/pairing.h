#pragma once
#include <ArduinoJson.h>

// Per-device secrets set at runtime by the Quartz app over USB ({"cmd":"esle"}), stored in NVS
// namespace "quartz" (keys anahtar / ota / hk). Compile-time OTA_PASSWORD / HOMEKIT_CODE are the
// fallback, so an owner build with secrets.ini keeps working unpaired. Never echoed anywhere.
namespace pairing {

void begin();                       // before net::begin() (HomeSpan reads the effective values)
bool paired();                      // an API key (anahtar) is stored
bool checkKey(const char* given);   // constant-time compare against the stored anahtar
const String& otaPassword();        // NVS "ota", else OTA_PASSWORD; empty = OTA disabled
const String& homekitCode();        // NVS "hk", else HOMEKIT_CODE if valid, else a random hidden code
const String& deviceId();           // quartzled_xxxxxx (last 3 MAC bytes, same as the MQTT id)
const String& hostName();           // mDNS name without .local: quartzled-xxxxxx (genel) / quartzled
bool takeUnpairRequest();           // esle/sifirla asked to forget Home controllers on this boot
bool equals(const char* a, const char* b);  // constant-time string compare
bool validHomekitCode(const char* s);
// The caller knows the OTA password: the effective one, or (distribution build flashed over an older
// owner build) the one whose MD5 ArduinoOTA keeps in NVS "OTA"/"OTADATA". Whoever knows it can already
// flash any firmware, so it may also pair over the network. Empty = nobody.
bool otaKeyOk(const char* key);
void command(JsonObjectConst req, JsonDocument& resp);  // "esle" — USB, or HTTP with otaKeyOk (net.cpp)

}  // namespace pairing
