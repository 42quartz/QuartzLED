#include "net.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "commands.h"
#include "events.h"
#include "led_engine.h"

namespace net {
namespace {

constexpr const char* kHostname = "led";  // -> led.local
Preferences prefs;
WebServer http(80);
String ssid;
bool httpStarted = false;
bool mdnsStarted = false;
bool otaStarted = false;
uint8_t lastReason = 0;
uint32_t connectedSince = 0;

const char* reasonName(uint8_t r) {
  switch (r) {
    case 2: return "auth_expire";
    case 3: return "auth_leave";
    case 4: return "assoc_expire";
    case 8: return "assoc_leave";
    case 15: return "4way_handshake_timeout (wrong password?)";
    case 200: return "beacon_timeout";
    case 201: return "no_ap_found";
    case 202: return "auth_fail (wrong password / security mismatch)";
    case 203: return "assoc_fail";
    case 204: return "handshake_timeout (wrong password?)";
    case 205: return "connection_fail";
    default: return "other";
  }
}

void onEvent(arduino_event_id_t id, arduino_event_info_t info) {
  JsonDocument ev;
  ev["event"] = "wifi";
  switch (id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      connectedSince = millis();
      lastReason = 0;
      ev["state"] = "connected";
      ev["ip"] = WiFi.localIP().toString();
      ev["rssi"] = WiFi.RSSI();
      ev["host"] = String(kHostname) + ".local";
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      connectedSince = 0;
      lastReason = info.wifi_sta_disconnected.reason;
      ev["state"] = "disconnected";
      ev["reason"] = lastReason;
      ev["reason_name"] = reasonName(lastReason);
      break;
    default:
      return;
  }
  events::emit(ev);
}

void handleApi() {
  JsonDocument req, resp;
  if (http.method() == HTTP_GET) {
    req["v"] = 1;
    req["cmd"] = http.hasArg("cmd") ? http.arg("cmd") : "get";
  } else {
    // Without a JSON Content-Type the body arrives as a form field name (e.g. plain `curl -d`).
    String body = http.arg("plain");
    if (body.isEmpty() && http.args() > 0) body = http.argName(0);
    DeserializationError e = deserializeJson(req, body);
    if (e) {
      http.send(400, "application/json", "{\"error\":\"bad json\"}");
      return;
    }
  }
  const char* cmd = req["cmd"] | "set";
  if (!strcmp(cmd, "wifi") || !strcmp(cmd, "reboot")) {  // USB-only for now
    http.send(403, "application/json", "{\"error\":\"not allowed over http\"}");
    return;
  }
  commands::handle(req.as<JsonObjectConst>(), resp);
  String out;
  serializeJson(resp, out);
  http.sendHeader("Access-Control-Allow-Origin", "*");
  http.send(resp["error"].isNull() ? 200 : 400, "application/json", out);
}

void startServices() {
  if (!mdnsStarted && MDNS.begin(kHostname)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "api", "/api");
    mdnsStarted = true;
  }
  if (!httpStarted) {
    http.on("/api", HTTP_GET, handleApi);
    http.on("/api", HTTP_POST, handleApi);
    http.on("/api", HTTP_OPTIONS, [] {
      http.sendHeader("Access-Control-Allow-Origin", "*");
      http.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      http.send(204);
    });
    http.onNotFound([] { http.send(404, "text/plain", "MiniBeyaz LED - API: /api\n"); });
    http.begin();
    httpStarted = true;
  }
  if (!otaStarted) {
    ArduinoOTA.setHostname(kHostname);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setMdnsEnabled(false);  // MDNS already started above
    ArduinoOTA.onStart([] {
      led::blank();
      JsonDocument ev;
      ev["event"] = "ota";
      ev["state"] = "start";
      events::emit(ev);
    });
    ArduinoOTA.onError([](ota_error_t e) {
      JsonDocument ev;
      ev["event"] = "ota";
      ev["state"] = "error";
      ev["code"] = (int)e;
      events::emit(ev);
    });
    ArduinoOTA.begin();
    MDNS.enableArduino(3232, true);
    otaStarted = true;
  }
}

void connect() {
  String pass = prefs.getString("pass", "");
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kHostname);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
}

}  // namespace

void begin() {
  prefs.begin("wifi", false);
  ssid = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
  WiFi.onEvent(onEvent);
  if (ssid.length()) connect();
  else WiFi.mode(WIFI_OFF);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    startServices();
    http.handleClient();
    ArduinoOTA.handle();
  }
}

bool setCredentials(const char* newSsid, const char* pass) {
  if (!newSsid || !*newSsid || strlen(newSsid) > 32 || (pass && strlen(pass) > 63)) return false;
  ssid = newSsid;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass ? pass : "");
  WiFi.disconnect();
  connect();
  return true;
}

void forget() {
  prefs.remove("ssid");
  prefs.remove("pass");
  ssid = "";
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

void status(JsonObject out) {
  out["configured"] = ssid.length() > 0;
  if (ssid.length()) out["ssid"] = ssid;
  out["connected"] = connected();
  if (connected()) {
    out["ip"] = WiFi.localIP().toString();
    out["rssi"] = WiFi.RSSI();
    out["host"] = String(kHostname) + ".local";
    out["uptime_s"] = (millis() - connectedSince) / 1000;
  } else if (lastReason) {
    out["last_reason"] = lastReason;
    out["last_reason_name"] = reasonName(lastReason);
  }
}

}  // namespace net
