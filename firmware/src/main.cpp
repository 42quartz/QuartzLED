// QuartzLED firmware — USB serial control, Wi-Fi + HTTP API.
// Every interface funnels into commands::handle() with the v1 JSON schema.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "app.h"
#include "circadian.h"
#include "commands.h"
#include "events.h"
#include "led_engine.h"
#include "mqtt.h"
#include "net.h"
#include "presets.h"
#include "storage.h"

namespace {

LedState gState;
LedConfig gConfig;
uint32_t stateDirtyAt = 0;
uint32_t rebootAt = 0;
uint32_t timerOffAt = 0;
String lineBuf;

constexpr uint32_t kSaveDelayMs = 3000;  // debounce flash writes while sliders move

const char kHelp[] =
    "# commands: get | info | scan | on | off | bri N | rgb R G B | effect NAME | speed N\n"
    "#           count N | order GRB | chip ws2812|ws2811_400|ucs1903 | power MA | probe N|off | reboot\n"
    "#           rgb2 R G B | intensity N | palette NAME | reverse on|off | mirror on|off\n"
    "#           preset NAME | save NAME | delete NAME | timer MIN | sunrise MIN | sunset MIN | circadian\n"
    "#           mqtt (status; set via JSON {\"cmd\":\"mqtt\",\"host\":..,\"user\":..,\"pass\":..})\n"
    "#           wifi (status) | forget | credentials: tools/ledctl.py wifi-setup\n"
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

void setTimer(uint16_t minutes) { timerOffAt = minutes ? (millis() + minutes * 60000UL) | 1 : 0; }

uint32_t timerRemaining() {
  if (!timerOffAt) return 0;
  int32_t left = (int32_t)(timerOffAt - millis());
  return left > 0 ? (left + 999) / 1000 : 0;
}
}  // namespace app

void setup() {
  Serial.begin(115200);
  storage::begin();
  uint32_t boots = storage::bumpBootCount();
  storage::load(gState, gConfig);
  presets::begin();
  circadian::begin();

  led::begin(gConfig);
  led::setState(gState);
  net::begin();
  mqtt::begin();

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
  net::loop();
  mqtt::loop();
  circadian::loop();
  events::flush(Serial);

  if (stateDirtyAt && millis() - stateDirtyAt > kSaveDelayMs) {
    LedState s = gState;
    s.probe = -1;
    storage::saveState(s);
    stateDirtyAt = 0;
  }
  if (timerOffAt && (int32_t)(millis() - timerOffAt) >= 0) {
    timerOffAt = 0;
    LedState s = gState;
    s.on = false;
    app::commitState(s);
  }
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    Serial.flush();
    ESP.restart();
  }
}
