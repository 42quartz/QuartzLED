#include "pairing.h"

#include <Preferences.h>
#include <esp_system.h>

#include "app.h"
#include <esp_random.h>

namespace pairing {
namespace {

Preferences prefs;
String anahtar, ota, hk, id, host;
bool unpairRequested = false;

bool isHex64(const char* s) {
  if (strlen(s) != 64) return false;
  for (const char* p = s; *p; p++)
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
  return true;
}

bool validOta(const char* s) {
  size_t n = strlen(s);
  if (n < 16 || n > 64) return false;
  for (const char* p = s; *p; p++)
    if (*p < 0x20 || *p > 0x7e) return false;
  return true;
}

}  // namespace

void begin() {
  prefs.begin("quartz", false);
  String a = prefs.isKey("anahtar") ? prefs.getString("anahtar", "") : "";
  String o = prefs.isKey("ota") ? prefs.getString("ota", "") : "";
  String h = prefs.isKey("hk") ? prefs.getString("hk", "") : "";
  anahtar = isHex64(a.c_str()) ? a : "";
  ota = o.length() ? o : String(OTA_PASSWORD);
  if (validHomekitCode(h.c_str())) hk = h;
  else if (validHomekitCode(HOMEKIT_CODE)) hk = HOMEKIT_CODE;  // an invalid code would halt HomeSpan
  else {
    // Unpaired distribution board: never fall back to HomeSpan's well-known default code. A random,
    // never-shown code keeps HomeKit unpairable until the Quartz app sets a real one over USB.
    char code[9];
    do {
      for (int i = 0; i < 8; i++) code[i] = '0' + (esp_random() % 10);
      code[8] = 0;
    } while (!validHomekitCode(code));
    hk = code;
  }
  unpairRequested = prefs.getBool("unpair", false);
  if (unpairRequested) prefs.remove("unpair");

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[24];
  snprintf(buf, sizeof(buf), "quartzled_%02x%02x%02x", mac[3], mac[4], mac[5]);
  id = buf;
#ifdef QUARTZ_GENEL
  snprintf(buf, sizeof(buf), "quartzled-%02x%02x%02x", mac[3], mac[4], mac[5]);  // unique per board
  host = buf;
#else
  host = "quartzled";  // owner build keeps the fixed name used by OTA and Home Assistant
#endif
}

bool paired() { return anahtar.length() > 0; }

bool equals(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  size_t n = la > lb ? la : lb;
  uint8_t diff = la != lb;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(i < la ? a[i] : 0) ^ (uint8_t)(i < lb ? b[i] : 0);
  return diff == 0;
}

bool checkKey(const char* given) { return paired() && given && equals(given, anahtar.c_str()); }

const String& otaPassword() { return ota; }
const String& homekitCode() { return hk; }
const String& deviceId() { return id; }
const String& hostName() { return host; }
bool takeUnpairRequest() { return unpairRequested; }

bool validHomekitCode(const char* s) {
  if (!s || strlen(s) != 8) return false;
  bool allSame = true;
  for (int i = 0; i < 8; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    if (s[i] != s[0]) allSame = false;
  }
  return !allSame && strcmp(s, "12345678") && strcmp(s, "87654321");
}

// Pairing (or clearing it) revokes every older control path: Home controllers added before (on the
// next boot, see homekit::begin) and any MQTT broker settings.
void revokeOthers() {
  prefs.putBool("unpair", true);
  Preferences mq;
  if (mq.begin("mqtt", false)) {
    mq.clear();
    mq.end();
  }
}

void command(JsonObjectConst req, JsonDocument& resp) {
  if (req["sifirla"] == true) {
    prefs.remove("anahtar");
    prefs.remove("ota");
    prefs.remove("hk");
  } else {
    const char* a = req["anahtar"] | "";
    const char* o = req["ota"] | "";
    const char* h = req["homekit"] | "";
    if (!isHex64(a)) { resp["error"] = "anahtar must be 64 lowercase hex chars"; return; }
    if (!validOta(o)) { resp["error"] = "ota must be 16-64 printable ascii chars"; return; }
    if (!validHomekitCode(h)) { resp["error"] = "homekit must be 8 digits, not trivial"; return; }
    if (!prefs.putString("anahtar", a) || !prefs.putString("ota", o) || !prefs.putString("hk", h)) {
      resp["error"] = "nvs write failed";
      return;
    }
  }
  revokeOthers();
  resp["ok"] = true;
  resp["reboot"] = true;  // HomeSpan reads OTA password + setup code once, in begin()
  app::requestReboot();
}

}  // namespace pairing
