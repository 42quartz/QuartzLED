#include "net.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include "commands.h"
#include "events.h"
#include "homekit.h"
#include "web_ui.h"

// Wi-Fi association, mDNS and OTA are run by HomeSpan (homekit.cpp). This module stores the
// credentials, reports connection events and serves the HTTP API on port 80.
namespace net {
namespace {

constexpr const char* kHost = "quartzled.local";
Preferences prefs;
WebServer http(80);
String ssid;
bool httpStarted = false;
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
      ev["host"] = kHost;
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
    // WebServer only keeps the raw body for non-form content types: clients must send application/json.
    DeserializationError e = deserializeJson(req, http.arg("plain"));
    if (e) {
      http.send(400, "application/json", "{\"error\":\"bad json (send Content-Type: application/json)\"}");
      return;
    }
  }
  const char* cmd = req["cmd"] | "set";
  if (!strcmp(cmd, "wifi") || !strcmp(cmd, "reboot")) {  // USB-only for now
    http.send(403, "application/json", "{\"error\":\"not allowed over http\"}");
    return;
  }
  // Destructive HomeKit ops and MQTT credentials over the network need the OTA password.
  if ((!strcmp(cmd, "homekit") || !strcmp(cmd, "mqtt")) && strcmp(req["key"] | "", OTA_PASSWORD) != 0) {
    http.send(403, "application/json", "{\"error\":\"key required\"}");
    return;
  }
  if (req["source"].isNull()) req["source"] = "http";
  commands::handle(req.as<JsonObjectConst>(), resp);
  String out;
  serializeJson(resp, out);
  http.sendHeader("Access-Control-Allow-Origin", "*");
  http.send(resp["error"].isNull() ? 200 : 400, "application/json", out);
}

void startHttp() {
  http.on("/api", HTTP_GET, handleApi);
  http.on("/api", HTTP_POST, handleApi);
  http.on("/api", HTTP_OPTIONS, [] {
    http.sendHeader("Access-Control-Allow-Origin", "*");
    http.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    http.send(204);
  });
  http.on("/", HTTP_GET, [] { http.send_P(200, "text/html; charset=utf-8", kWebUi); });
  http.onNotFound([] { http.send(404, "text/plain", "QuartzLED - UI: /  API: /api\n"); });
  http.begin();
  MDNS.addService("http", "tcp", 80);  // MDNS itself was started by HomeSpan
  MDNS.addServiceTxt("http", "tcp", "api", "/api");
  httpStarted = true;
}

}  // namespace

void begin() {
  prefs.begin("wifi", false);
  ssid = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
  String pass = ssid.length() ? prefs.getString("pass", "") : "";
  WiFi.onEvent(onEvent);
  homekit::begin(ssid.c_str(), pass.c_str());
}

void loop() {
  homekit::loop();
  if (WiFi.status() != WL_CONNECTED) return;
  if (!httpStarted) startHttp();
  http.handleClient();
}

bool setCredentials(const char* newSsid, const char* pass) {
  if (!newSsid || !*newSsid || strlen(newSsid) > 32 || (pass && strlen(pass) > 63)) return false;
  ssid = newSsid;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass ? pass : "");
  app::requestReboot();  // HomeSpan reads credentials once, in begin()
  return true;
}

void forget() {
  prefs.remove("ssid");
  prefs.remove("pass");
  ssid = "";
  homekit::eraseWifi();  // also reboots
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

void status(JsonObject out) {
  out["configured"] = ssid.length() > 0;
  if (ssid.length()) out["ssid"] = ssid;
  out["connected"] = connected();
  if (connected()) {
    out["ip"] = WiFi.localIP().toString();
    out["rssi"] = WiFi.RSSI();
    out["host"] = kHost;
    out["uptime_s"] = (millis() - connectedSince) / 1000;
  } else if (lastReason) {
    out["last_reason"] = lastReason;
    out["last_reason_name"] = reasonName(lastReason);
  }
}

}  // namespace net
