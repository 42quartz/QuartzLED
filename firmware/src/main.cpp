// MiniBeyaz LED firmware — stage 2: USB serial control + calibration.
// Every interface funnels into commands::handle() with the v1 JSON schema.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "app.h"
#include "commands.h"
#include "led_engine.h"
#include "storage.h"

namespace {

LedState gState;
LedConfig gConfig;
uint32_t stateDirtyAt = 0;
uint32_t rebootAt = 0;
String lineBuf;

constexpr uint32_t kSaveDelayMs = 3000;  // debounce flash writes while sliders move

const char kHelp[] =
    "# commands: get | info | scan | on | off | bri N | rgb R G B | effect NAME | speed N\n"
    "#           count N | order GRB | chip ws2812|ws2811_400|ucs1903 | power MA | probe N|off | reboot\n"
    "#           or a JSON line: {\"v\":1,\"cmd\":\"set\",\"color\":[255,0,0]}\n";

void reply(JsonDocument& resp) {
  serializeJson(resp, Serial);
  Serial.println();
}

void handleLine(const String& line) {
  if (line.isEmpty()) return;
  if (line == "help" || line == "?") { Serial.print(kHelp); return; }

  JsonDocument req, resp;
  if (line[0] == '{') {
    DeserializationError e = deserializeJson(req, line);
    if (e) { resp["error"] = String("bad json: ") + e.c_str(); reply(resp); return; }
  } else {
    String err;
    if (!commands::parseText(line.c_str(), req, err)) { resp["error"] = err; reply(resp); return; }
  }
  commands::handle(req.as<JsonObjectConst>(), resp);
  reply(resp);
}

void pollSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      lineBuf.trim();
      handleLine(lineBuf);
      lineBuf = "";
    } else if (lineBuf.length() < 512) {
      lineBuf += ch;
    }
  }
}

}  // namespace

namespace app {
const LedState& state() { return gState; }
const LedConfig& config() { return gConfig; }

void commitState(const LedState& s) {
  gState = s;
  led::setState(gState);
  stateDirtyAt = millis() | 1;
}

void commitConfig(const LedConfig& c) {
  gConfig = c;
  led::setConfig(gConfig);
  storage::saveConfig(gConfig);
}

void requestReboot() { rebootAt = millis() + 300; }
}  // namespace app

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);  // stage 2 is USB-only; radio stays off to save power

  storage::begin();
  uint32_t boots = storage::bumpBootCount();
  storage::load(gState, gConfig);

  led::begin(gConfig);
  led::setState(gState);

  delay(200);
  JsonDocument hello;
  hello["event"] = "boot";
  hello["boots"] = boots;
  JsonDocument infoReq, info;
  infoReq["cmd"] = "info";
  commands::handle(infoReq.as<JsonObjectConst>(), info);
  hello["info"] = info;
  reply(hello);
  Serial.print(kHelp);
}

void loop() {
  pollSerial();
  led::loop();

  if (stateDirtyAt && millis() - stateDirtyAt > kSaveDelayMs) {
    LedState s = gState;
    s.probe = -1;
    storage::saveState(s);
    stateDirtyAt = 0;
  }
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    Serial.flush();
    ESP.restart();
  }
}
