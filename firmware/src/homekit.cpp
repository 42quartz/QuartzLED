#include "homekit.h"

#include <ArduinoJson.h>
#include <HomeSpan.h>

#include "app.h"
#include "color.h"
#include "commands.h"

namespace homekit {
namespace {

LedState lastSynced;  // last state mirrored to HomeKit; avoids echoing our own updates
bool haveSynced = false;

bool sameState(const LedState& a, const LedState& b) {
  return a.on == b.on && a.bri == b.bri && a.r == b.r && a.g == b.g && a.b == b.b && a.effect == b.effect &&
         a.speed == b.speed;
}

// Slider % <-> engine value through the shared low-end-dense curve.
int toPct(float frac) { return max(1L, lroundf(valueToSlider(frac) * 100)); }
float fromPct(float pct) { return sliderToValue(pct / 100.0f); }

bool apply(JsonDocument& req) {
  req["v"] = 1;
  req["cmd"] = "set";
  req["source"] = "homekit";
  JsonDocument resp;
  commands::handle(req.as<JsonObjectConst>(), resp);
  lastSynced = app::state();
  return resp["ok"] == true;
}

struct Light : Service::LightBulb {
  SpanCharacteristic *power, *level, *hue, *sat;

  Light() {
    const LedState& s = app::state();
    float h, sa;
    rgbToHs(s.r, s.g, s.b, h, sa);
    power = new Characteristic::On(s.on);
    level = new Characteristic::Brightness(toPct(s.bri / 255.0f));
    level->setRange(1, 100, 1);
    hue = new Characteristic::Hue(h);
    sat = new Characteristic::Saturation(sa);
  }

  boolean update() override {
    JsonDocument req;
    if (power->updated()) req["on"] = power->getNewVal<bool>();
    if (level->updated()) req["bri"] = max(1L, lroundf(fromPct(level->getNewVal<float>()) * 255));
    if (hue->updated() || sat->updated()) {
      uint8_t r, g, b;
      hsvToRgb(hue->getNewVal<float>(), sat->getNewVal<float>(), r, g, b);
      JsonArray c = req["color"].to<JsonArray>();
      c.add(r); c.add(g); c.add(b);
    }
    return apply(req);
  }

  void push(const LedState& s) {
    if (power->getVal<bool>() != s.on) power->setVal(s.on);
    int lv = toPct(s.bri / 255.0f);
    if (level->getVal() != lv) level->setVal(lv);
    float h, sa;
    rgbToHs(s.r, s.g, s.b, h, sa);
    if (fabsf(hue->getVal<float>() - h) > 0.5f) hue->setVal(h);
    if (fabsf(sat->getVal<float>() - sa) > 0.5f) sat->setVal(sa);
  }
};

struct EffectSwitch : Service::Switch {
  uint8_t fx;
  SpanCharacteristic* power;

  EffectSwitch(uint8_t effect, const char* label) : fx(effect) {
    power = new Characteristic::On(app::state().effect == fx);
    new Characteristic::ConfiguredName(label);
  }

  boolean update() override {
    JsonDocument req;
    bool on = power->getNewVal<bool>();
    req["effect"] = kEffects[on ? fx : FX_SOLID];
    if (on) req["on"] = true;
    return apply(req);
  }

  void push(const LedState& s) {
    bool on = s.effect == fx && s.on;
    if (power->getVal<bool>() != on) power->setVal(on);
  }
};

// HomeKit lights have no speed; a fan's rotation slider stands in for it (0-100% = speed 0-1000).
struct SpeedFan : Service::Fan {
  SpanCharacteristic *active, *rate;
  uint16_t lastNonZero = 300;

  SpeedFan() {
    uint16_t sp = app::state().speed;
    if (sp) lastNonZero = sp;
    active = new Characteristic::Active(sp > 0);
    rate = new Characteristic::RotationSpeed(toPct(sp / 1000.0f));
    rate->setRange(0, 100, 1);
    new Characteristic::ConfiguredName("Efekt Hızı");
  }

  boolean update() override {
    JsonDocument req;
    if (rate->updated()) req["speed"] = lroundf(fromPct(rate->getNewVal<float>()) * 1000);
    if (active->updated()) {
      if (!active->getNewVal()) req["speed"] = 0;  // off = freeze the animation
      else if (!rate->updated()) req["speed"] = lastNonZero;
    }
    if (req["speed"].as<int>() > 0) lastNonZero = req["speed"].as<int>();
    return apply(req);
  }

  void push(const LedState& s) {
    if (s.speed) lastNonZero = s.speed;
    int pct = toPct(s.speed / 1000.0f);
    if (active->getVal() != (s.speed > 0)) active->setVal(s.speed > 0);
    if (s.speed && rate->getVal() != pct) rate->setVal(pct);
  }
};

Light* light = nullptr;
SpeedFan* fan = nullptr;
std::vector<EffectSwitch*> switches;

}  // namespace

void begin(const char* ssid, const char* pass) {
  homeSpan.setLogLevel(0);
  homeSpan.setSerialInputDisable(true);  // our JSON shell owns Serial
  homeSpan.setPortNum(1201);             // port 80 is the HTTP API
  homeSpan.setHostNameSuffix("");        // -> quartzled.local
  homeSpan.setPairingCode(HOMEKIT_CODE);
  homeSpan.setQRID("QZLD");
  homeSpan.setSketchVersion(FW_VERSION);
  homeSpan.enableOTA(OTA_PASSWORD);
  if (ssid && *ssid) homeSpan.setWifiCredentials(ssid, pass ? pass : "");

  homeSpan.begin(Category::Lighting, "QuartzLED", "quartzled", "QuartzLED");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Manufacturer("QuartzLED");
  new Characteristic::Model("Deneyap ESP32 WS2812");
  new Characteristic::FirmwareRevision(FW_VERSION);
  light = new Light();
  static const struct { uint8_t fx; const char* label; } kSwitches[] = {
      {FX_RAINBOW, "Gökkuşağı"}, {FX_FIRE, "Ateş"}, {FX_BREATHE, "Nefes"},
      {FX_CHASE, "Kayan Işık"}, {FX_TWINKLE, "Pırıltı"},
  };
  for (auto& s : kSwitches) switches.push_back(new EffectSwitch(s.fx, s.label));
  fan = new SpeedFan();
}

void loop() {
  homeSpan.poll();
  const LedState& s = app::state();
  if (!light || (haveSynced && sameState(s, lastSynced))) return;
  light->push(s);
  fan->push(s);
  for (auto* sw : switches) sw->push(s);
  lastSynced = s;
  haveSynced = true;
}

void eraseWifi() { homeSpan.processSerialCommand("X"); }

void unpair() { homeSpan.processSerialCommand("U"); }

}  // namespace homekit
