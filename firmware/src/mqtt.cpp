#include "mqtt.h"

#include <PubSubClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

#include "commands.h"
#include "labels.h"
#include "presets.h"

namespace mqtt {
namespace {

WiFiClient net;
PubSubClient client(net);
Preferences prefs;
String host, user, pass, id, base;
uint16_t port = 1883;
uint32_t nextAttempt = 0, backoff = 2000;
uint32_t lastStateCheck = 0, lastStatePub = 0;
String lastState;
bool announced = false;

String topic(const char* leaf) { return base + "/" + leaf; }

// Jinja dict literal mapping ids <-> labels, e.g. {"Sabit":"solid",...}
template <size_t N>
String jinjaMap(const Label (&table)[N], bool labelToId) {
  String s = "{";
  for (size_t i = 0; i < N; i++) {
    if (i) s += ",";
    s += "\"";
    s += labelToId ? table[i].tr : table[i].id;
    s += "\":\"";
    s += labelToId ? table[i].id : table[i].tr;
    s += "\"";
  }
  return s + "}";
}

void publishJson(const String& t, JsonDocument& doc, bool retained) {
  size_t len = measureJson(doc);
  if (!client.beginPublish(t.c_str(), len, retained)) return;
  serializeJson(doc, client);
  client.endPublish();
}

void addCommon(JsonDocument& d, const char* name, const char* suffix) {
  d["name"] = name;
  d["uniq_id"] = id + "_" + suffix;
  d["obj_id"] = id + "_" + suffix;
  d["avty_t"] = topic("avail");
  JsonObject dev = d["dev"].to<JsonObject>();
  dev["ids"].to<JsonArray>().add(id);
  dev["name"] = "QuartzLED";
  dev["mf"] = "QuartzLED";
  dev["mdl"] = "Deneyap Kart ESP32 + WS2812";
  dev["sw"] = FW_VERSION;
  dev["cu"] = "http://" + WiFi.localIP().toString() + "/";
}

String discoveryTopic(const char* component, const char* suffix) {
  return String("homeassistant/") + component + "/" + id + "_" + suffix + "/config";
}

void announceLight() {
  JsonDocument d;
  addCommon(d, nullptr, "light");
  d["name"] = nullptr;  // explicit null: entity takes the device name ("QuartzLED")
  d["schema"] = "template";
  d["cmd_t"] = topic("cmd");
  d["stat_t"] = topic("state");
  // HA brightness is a 0-255 slider position; the device value goes through the shared curve.
  d["command_on_template"] =
      String("{\"v\":1,\"cmd\":\"set\",\"source\":\"ha\",\"on\":true"
             "{%- if brightness is defined -%},\"bri\":{{ [1, ((81 ** (brightness / 255) - 1) / 80 * 255) | round | int] | max }}{%- endif -%}"
             "{%- if red is defined -%},\"color\":[{{ red }},{{ green }},{{ blue }}]{%- endif -%}"
             "{%- if effect is defined -%},\"effect\":\"{{ ") +
      jinjaMap(kEffectLabels, true) + "[effect] }}\"{%- endif -%}}";
  d["command_off_template"] = "{\"v\":1,\"cmd\":\"set\",\"source\":\"ha\",\"on\":false}";
  d["state_template"] = "{{ 'on' if value_json.on else 'off' }}";
  d["brightness_template"] = "{{ ((log(1 + 80 * value_json.bri / 255) / log(81)) * 255) | round | int }}";
  d["red_template"] = "{{ value_json.color[0] }}";
  d["green_template"] = "{{ value_json.color[1] }}";
  d["blue_template"] = "{{ value_json.color[2] }}";
  d["effect_template"] = "{{ " + jinjaMap(kEffectLabels, false) + ".get(value_json.effect, value_json.effect) }}";
  JsonArray fx = d["effect_list"].to<JsonArray>();
  for (auto& l : kEffectLabels) fx.add(l.tr);
  publishJson(discoveryTopic("light", "light"), d, true);
}

void announceNumber(const char* suffix, const char* name, const char* icon, int min, int max, int step,
                    const char* unit, const char* cmdTpl, const char* valTpl, const char* mode = "slider") {
  JsonDocument d;
  addCommon(d, name, suffix);
  d["cmd_t"] = topic("cmd");
  d["cmd_tpl"] = cmdTpl;
  d["stat_t"] = topic("state");
  d["val_tpl"] = valTpl;
  d["min"] = min;
  d["max"] = max;
  d["step"] = step;
  d["mode"] = mode;
  if (unit) d["unit_of_meas"] = unit;
  d["icon"] = icon;
  publishJson(discoveryTopic("number", suffix), d, true);
}

void announceSwitch(const char* suffix, const char* name, const char* icon, const char* field) {
  JsonDocument d;
  addCommon(d, name, suffix);
  d["cmd_t"] = topic("cmd");
  d["pl_on"] = String("{\"v\":1,\"cmd\":\"set\",\"") + field + "\":true}";
  d["pl_off"] = String("{\"v\":1,\"cmd\":\"set\",\"") + field + "\":false}";
  d["stat_t"] = topic("state");
  d["val_tpl"] = String("{{ 'ON' if value_json.") + field + " else 'OFF' }}";
  d["stat_on"] = "ON";
  d["stat_off"] = "OFF";
  d["icon"] = icon;
  publishJson(discoveryTopic("switch", suffix), d, true);
}

void announceScene(const char* name, const char* label) {
  JsonDocument d;
  String suffix = String("scene_") + name;
  addCommon(d, label, suffix.c_str());
  d["cmd_t"] = topic("cmd");
  d["pl_on"] = String("{\"v\":1,\"cmd\":\"preset\",\"name\":\"") + name + "\"}";
  d["icon"] = "mdi:palette";
  publishJson(discoveryTopic("scene", suffix.c_str()), d, true);
}

void announce() {
  announceLight();
  announceNumber("speed", "Efekt Hızı", "mdi:speedometer", 0, 100, 1, "%",
                 "{\"v\":1,\"cmd\":\"set\",\"speed\":{{ ((81 ** (value / 100) - 1) / 80 * 1000) | round | int }}}",
                 "{{ ((log(1 + 80 * value_json.speed / 1000) / log(81)) * 100) | round | int }}");
  announceNumber("intensity", "Efekt Yoğunluğu", "mdi:tune-variant", 0, 100, 1, "%",
                 "{\"v\":1,\"cmd\":\"set\",\"intensity\":{{ (value * 2.55) | round | int }}}",
                 "{{ (value_json.intensity / 2.55) | round | int }}");
  announceNumber("timer", "Uyku Zamanlayıcısı", "mdi:timer-sand", 0, 240, 5, "min",
                 "{\"v\":1,\"cmd\":\"timer\",\"minutes\":{{ value | int }}}",
                 "{{ ((value_json.timer_s | default(0)) / 60) | round(0, 'ceil') | int }}", "box");

  {  // palette select
    JsonDocument d;
    addCommon(d, "Palet", "palette");
    d["cmd_t"] = topic("cmd");
    d["cmd_tpl"] = "{\"v\":1,\"cmd\":\"set\",\"palette\":\"{{ " + jinjaMap(kPaletteLabels, true) + "[value] }}\"}";
    d["stat_t"] = topic("state");
    d["val_tpl"] = "{{ " + jinjaMap(kPaletteLabels, false) + ".get(value_json.palette) }}";
    JsonArray opts = d["options"].to<JsonArray>();
    for (auto& l : kPaletteLabels) opts.add(l.tr);
    d["icon"] = "mdi:palette-swatch";
    publishJson(discoveryTopic("select", "palette"), d, true);
  }

  {  // effect select: Google Home has no effect UI on lights but exposes selects as Modes
    JsonDocument d;
    addCommon(d, "Efekt", "effect");
    d["cmd_t"] = topic("cmd");
    d["cmd_tpl"] = "{\"v\":1,\"cmd\":\"set\",\"on\":true,\"effect\":\"{{ " + jinjaMap(kEffectLabels, true) + "[value] }}\"}";
    d["stat_t"] = topic("state");
    d["val_tpl"] = "{{ " + jinjaMap(kEffectLabels, false) + ".get(value_json.effect) }}";
    JsonArray opts = d["options"].to<JsonArray>();
    for (auto& l : kEffectLabels) opts.add(l.tr);
    d["icon"] = "mdi:auto-fix";
    publishJson(discoveryTopic("select", "effect"), d, true);
  }

  announceSwitch("reverse", "Ters Yön", "mdi:swap-horizontal", "reverse");
  announceSwitch("mirror", "Ortadan Aynala", "mdi:arrow-expand-horizontal", "mirror");

  {  // sunrise button
    JsonDocument d;
    addCommon(d, "Gün Doğumu (20 dk)", "sunrise");
    d["cmd_t"] = topic("cmd");
    d["pl_prs"] = "{\"v\":1,\"cmd\":\"sunrise\",\"minutes\":20}";
    d["icon"] = "mdi:weather-sunset-up";
    publishJson(discoveryTopic("button", "sunrise"), d, true);
  }

  for (auto& l : kPresetLabels) announceScene(l.id, l.tr);
  JsonDocument list;
  presets::list(list.to<JsonObject>());
  for (const char* name : list["user"].as<JsonArray>()) {
    String label = String("★ ") + name;
    announceScene(name, label.c_str());
  }
  announced = true;
}

void publishState(bool force) {
  JsonDocument req, resp;
  req["cmd"] = "get";
  commands::handle(req.as<JsonObjectConst>(), resp);
  String s;
  serializeJson(resp["state"], s);
  if (!force && s == lastState && millis() - lastStatePub < 60000) return;
  client.publish(topic("state").c_str(), s.c_str(), true);
  lastState = s;
  lastStatePub = millis();
}

void onMessage(char* t, uint8_t* payload, unsigned int len) {
  if (!strcmp(t, "homeassistant/status")) {
    if (len == 6 && !memcmp(payload, "online", 6)) {  // HA restarted: re-announce
      announce();
      publishState(true);
    }
    return;
  }
  JsonDocument req, resp;
  if (deserializeJson(req, payload, len)) {
    resp["error"] = "bad json";
  } else {
    const char* cmd = req["cmd"] | "set";
    if (!strcmp(cmd, "wifi") || !strcmp(cmd, "mqtt") || !strcmp(cmd, "homekit")) {
      resp["error"] = "not allowed over mqtt";
    } else {
      if (req["source"].isNull()) req["source"] = "mqtt";
      commands::handle(req.as<JsonObjectConst>(), resp);
    }
  }
  publishJson(topic("resp"), resp, false);
  publishState(false);
}

bool connect() {
  client.setServer(host.c_str(), port);
  String avail = topic("avail");
  if (!client.connect(id.c_str(), user.c_str(), pass.c_str(), avail.c_str(), 1, true, "offline")) return false;
  client.publish(avail.c_str(), "online", true);
  client.subscribe(topic("cmd").c_str());
  client.subscribe("homeassistant/status");
  announce();
  publishState(true);
  return true;
}

}  // namespace

void begin() {
  prefs.begin("mqtt", false);
  host = prefs.isKey("host") ? prefs.getString("host") : "";
  port = prefs.getUShort("port", 1883);
  user = prefs.isKey("user") ? prefs.getString("user") : "";
  pass = prefs.isKey("pass") ? prefs.getString("pass") : "";
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);  // valid before the radio starts
  char buf[24];
  snprintf(buf, sizeof(buf), "quartzled_%02x%02x%02x", mac[3], mac[4], mac[5]);
  id = buf;
  base = String("quartzled/") + (buf + 10);
  client.setCallback(onMessage);
  client.setBufferSize(1024);  // inbound commands; outbound discovery is streamed
  client.setSocketTimeout(2);
  client.setKeepAlive(30);
}

void loop() {
  if (host.isEmpty() || WiFi.status() != WL_CONNECTED) return;
  if (!client.connected()) {
    if ((int32_t)(millis() - nextAttempt) < 0) return;
    if (connect()) {
      backoff = 2000;
    } else {
      nextAttempt = millis() + backoff;
      backoff = min<uint32_t>(backoff * 2, 60000);
      return;
    }
  }
  client.loop();
  if (millis() - lastStateCheck > 250) {
    lastStateCheck = millis();
    publishState(false);
  }
}

void configure(JsonObjectConst req, JsonDocument& resp) {
  if (req["host"].is<const char*>()) {
    host = req["host"].as<String>();
    port = req["port"] | 1883;
    user = req["user"] | "";
    pass = req["pass"] | "";
    prefs.putString("host", host);
    prefs.putUShort("port", port);
    prefs.putString("user", user);
    prefs.putString("pass", pass);
    client.disconnect();
    nextAttempt = 0;
    backoff = 2000;
  } else if (req["forget"] == true) {
    prefs.clear();
    host = user = pass = "";
    client.disconnect();
  }
  resp["ok"] = true;
  JsonObject s = resp["mqtt"].to<JsonObject>();
  s["configured"] = !host.isEmpty();
  if (!host.isEmpty()) {
    s["host"] = host;
    s["port"] = port;
    s["user"] = user;
  }
  s["connected"] = client.connected();
  s["state"] = client.state();
  s["base"] = base;
}

void publishEvent(const char* json) {
  if (client.connected()) client.publish(topic("event").c_str(), json);
}

void presetsChanged(const char* removed) {
  if (!client.connected()) return;
  if (removed) client.publish(discoveryTopic("scene", (String("scene_") + removed).c_str()).c_str(), "", true);
  JsonDocument list;
  presets::list(list.to<JsonObject>());
  for (const char* name : list["user"].as<JsonArray>()) {
    String label = String("★ ") + name;
    announceScene(name, label.c_str());
  }
}

}  // namespace mqtt
