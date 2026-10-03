#include "commands.h"

#include <WiFi.h>
#include <esp_system.h>

#include "app.h"
#include "net.h"

namespace commands {
namespace {

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_EXT: return "external";
    default: return "unknown";
  }
}

const char* authName(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa_wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2_ent";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2_wpa3";
    default: return "other";
  }
}

void writeState(JsonObject o) {
  const LedState& s = app::state();
  const LedConfig& c = app::config();
  o["on"] = s.on;
  o["bri"] = s.bri;
  JsonArray col = o["color"].to<JsonArray>();
  col.add(s.r);
  col.add(s.g);
  col.add(s.b);
  o["effect"] = kEffects[s.effect];
  o["speed"] = s.speed;
  if (s.probe >= 0) o["probe"] = s.probe;
  o["count"] = c.count;
  o["order"] = c.order;
  o["chip"] = kChips[c.chip];
  o["power_ma"] = c.powerMa;
}

void doSet(JsonObjectConst req, JsonDocument& resp) {
  LedState s = app::state();
  LedConfig c = app::config();
  bool cfgChanged = false;

  if (req["on"].is<bool>()) s.on = req["on"];
  if (req["bri"].is<int>()) s.bri = constrain(req["bri"].as<int>(), 0, 255);
  if (req["color"].is<JsonArrayConst>()) {
    JsonArrayConst a = req["color"];
    if (a.size() != 3) { resp["error"] = "color needs [r,g,b]"; return; }
    s.r = constrain(a[0].as<int>(), 0, 255);
    s.g = constrain(a[1].as<int>(), 0, 255);
    s.b = constrain(a[2].as<int>(), 0, 255);
  }
  if (req["effect"].is<const char*>()) {
    int e = effectFromName(req["effect"]);
    if (e < 0) { resp["error"] = "unknown effect"; return; }
    s.effect = e;
  }
  if (req["speed"].is<int>()) s.speed = constrain(req["speed"].as<int>(), 0, 1000);
  if (!req["probe"].isNull()) s.probe = req["probe"].is<int>() ? max(-1, req["probe"].as<int>()) : -1;

  if (req["count"].is<int>()) { c.count = constrain(req["count"].as<int>(), 1, LED_MAX); cfgChanged = true; }
  if (req["power_ma"].is<int>()) { c.powerMa = constrain(req["power_ma"].as<int>(), 100, 10000); cfgChanged = true; }
  if (req["order"].is<const char*>()) {
    String o = req["order"].as<String>();
    o.toUpperCase();
    if (o.length() != 3 || o.indexOf('R') < 0 || o.indexOf('G') < 0 || o.indexOf('B') < 0) {
      resp["error"] = "order must be a permutation of RGB";
      return;
    }
    strlcpy(c.order, o.c_str(), sizeof(c.order));
    cfgChanged = true;
  }
  if (req["chip"].is<const char*>()) {
    int ch = chipFromName(req["chip"]);
    if (ch < 0) { resp["error"] = "unknown chip"; return; }
    if (ch != c.chip) {
      c.chip = ch;
      cfgChanged = true;
      resp["reboot"] = true;
      app::requestReboot();
    }
  }

  if (cfgChanged) app::commitConfig(c);
  app::commitState(s);
  resp["ok"] = true;
  writeState(resp["state"].to<JsonObject>());
}

void doScan(JsonDocument& resp) {
  bool wasOff = WiFi.getMode() == WIFI_OFF;
  if (wasOff) WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true);
  JsonArray arr = resp["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["ch"] = WiFi.channel(i);
    o["auth"] = authName(WiFi.encryptionType(i));
    o["bssid"] = WiFi.BSSIDstr(i);
  }
  WiFi.scanDelete();
  if (wasOff) WiFi.mode(WIFI_OFF);
  resp["ok"] = true;
}

void doWifi(JsonObjectConst req, JsonDocument& resp) {
  if (req["forget"] == true) {
    net::forget();
  } else if (req["ssid"].is<const char*>()) {
    if (!net::setCredentials(req["ssid"], req["pass"] | "")) {
      resp["error"] = "invalid ssid/pass length";
      return;
    }
    resp["reboot"] = true;
  }
  resp["ok"] = true;
  net::status(resp["wifi"].to<JsonObject>());
}

}  // namespace

void handle(JsonObjectConst req, JsonDocument& resp) {
  int v = req["v"] | 1;
  if (v != 1) { resp["error"] = "unsupported schema version"; return; }
  const char* cmd = req["cmd"] | "set";
  if (!req["id"].isNull()) resp["id"] = req["id"];  // lets async clients match replies
  resp["cmd"] = cmd;

  if (!strcmp(cmd, "set")) doSet(req, resp);
  else if (!strcmp(cmd, "get")) { resp["ok"] = true; writeState(resp["state"].to<JsonObject>()); }
  else if (!strcmp(cmd, "info")) {
    resp["ok"] = true;
    resp["fw"] = FW_VERSION;
    resp["reset"] = resetReasonName(esp_reset_reason());
    resp["heap"] = ESP.getFreeHeap();
    resp["heap_min"] = ESP.getMinFreeHeap();
    resp["uptime_s"] = millis() / 1000;
    resp["mac"] = WiFi.macAddress();
    JsonArray fx = resp["effects"].to<JsonArray>();
    for (auto e : kEffects) fx.add(e);
    JsonArray ch = resp["chips"].to<JsonArray>();
    for (auto c : kChips) ch.add(c);
  }
  else if (!strcmp(cmd, "scan")) doScan(resp);
  else if (!strcmp(cmd, "wifi")) doWifi(req, resp);
  else if (!strcmp(cmd, "reboot")) { resp["ok"] = true; app::requestReboot(); }
  else resp["error"] = "unknown cmd";
}

bool parseText(const char* line, JsonDocument& req, String& err) {
  char buf[128];
  strlcpy(buf, line, sizeof(buf));
  char* save;
  char* w = strtok_r(buf, " \t", &save);
  if (!w) { err = "empty"; return false; }
  auto next = [&]() { return strtok_r(nullptr, " \t", &save); };
  auto num = [&](const char* name, int& out) {
    char* a = next();
    if (!a) { err = String(name) + " needs a number"; return false; }
    out = atoi(a);
    return true;
  };

  req["v"] = 1;
  String cmd(w);
  cmd.toLowerCase();
  int n;
  if (cmd == "get" || cmd == "info" || cmd == "scan" || cmd == "reboot" || cmd == "wifi") req["cmd"] = cmd;
  else if (cmd == "forget") { req["cmd"] = "wifi"; req["forget"] = true; }
  else if (cmd == "on") req["on"] = true;
  else if (cmd == "off") req["on"] = false;
  else if (cmd == "bri") { if (!num("bri", n)) return false; req["bri"] = n; }
  else if (cmd == "speed") { if (!num("speed", n)) return false; req["speed"] = n; }
  else if (cmd == "count") { if (!num("count", n)) return false; req["count"] = n; }
  else if (cmd == "power") { if (!num("power", n)) return false; req["power_ma"] = n; }
  else if (cmd == "probe") {
    char* a = next();
    if (!a || !strcmp(a, "off")) req["probe"] = -1;
    else req["probe"] = atoi(a);
  }
  else if (cmd == "rgb") {
    int r, g, b;
    if (!num("r", r) || !num("g", g) || !num("b", b)) return false;
    JsonArray c = req["color"].to<JsonArray>();
    c.add(r); c.add(g); c.add(b);
    req["on"] = true;
  }
  else if (cmd == "effect" || cmd == "fx") {
    char* a = next();
    if (!a) { err = "effect needs a name"; return false; }
    req["effect"] = a;
    req["on"] = true;
  }
  else if (cmd == "order" || cmd == "chip") {
    char* a = next();
    if (!a) { err = cmd + " needs a value"; return false; }
    req[cmd] = a;
  }
  else { err = "unknown command; try: help"; return false; }
  return true;
}

}  // namespace commands
