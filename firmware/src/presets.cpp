#include "presets.h"

#include <Preferences.h>

namespace presets {
namespace {

Preferences prefs;
constexpr uint8_t kVer = 1;  // bump with LedState layout (storage.cpp kStateVer)

LedState make(uint8_t fx, uint8_t r, uint8_t g, uint8_t b, uint8_t bri, uint16_t speed = 300,
              uint8_t intensity = 128, uint8_t pal = PAL_RAINBOW) {
  LedState s;
  s.effect = fx;
  s.r = r, s.g = g, s.b = b;
  s.bri = bri;
  s.speed = speed;
  s.intensity = intensity;
  s.palette = pal;
  return s;
}

struct Builtin {
  const char* name;
  LedState state;
};

const Builtin kBuiltins[] = {
    {"okuma", make(FX_SOLID, 255, 170, 90, 170)},
    {"odak", make(FX_SOLID, 210, 225, 255, 220)},
    {"film", make(FX_SOLID, 255, 90, 20, 25)},
    {"gece", make(FX_SOLID, 255, 40, 0, 6)},
    {"rahat", make(FX_NOISE, 0, 0, 0, 90, 60, 60, PAL_OCEAN)},
    {"kutup", make(FX_NOISE, 0, 0, 0, 100, 50, 50, PAL_AURORA)},
    {"gunbatimi", make(FX_WAVE, 0, 0, 0, 120, 40, 32, PAL_SUNSET)},
    {"somine", make(FX_FIRE, 0, 0, 0, 150, 300, 120)},
    {"mum", make(FX_CANDLE, 255, 110, 20, 60, 200, 140)},
    {"romantik", make(FX_BREATHE, 255, 30, 80, 70, 80)},
    {"parti", make(FX_NOISE, 0, 0, 0, 200, 500, 160, PAL_PARTY)},
    {"disko", make(FX_JUGGLE, 0, 0, 0, 200, 600, 200, PAL_PARTY)},
};

String key(char kind, int slot) { return String(kind) + slot; }

int findUser(const char* name) {
  for (int i = 0; i < kMaxUser; i++)
    if (prefs.isKey(key('n', i).c_str()) && prefs.getString(key('n', i).c_str()) == name) return i;
  return -1;
}

}  // namespace

bool showB = true;

void begin() {
  prefs.begin("presets", false);
  showB = prefs.getBool("showb", true);
}

bool showBuiltin() { return showB; }

void setShowBuiltin(bool show) {
  showB = show;
  prefs.putBool("showb", show);
}

bool find(const char* name, LedState& out) {
  int slot = findUser(name);
  if (slot >= 0) {
    uint8_t buf[sizeof(LedState) + 1];
    if (prefs.getBytes(key('s', slot).c_str(), buf, sizeof(buf)) == sizeof(buf) && buf[0] == kVer) {
      memcpy(&out, buf + 1, sizeof(LedState));
      out.probe = -1;
      return true;
    }
  }
  for (auto& b : kBuiltins)
    if (showB && !strcasecmp(b.name, name)) {
      out = b.state;
      return true;
    }
  return false;
}

bool save(const char* name, const LedState& s) {
  size_t len = strlen(name);
  if (len == 0 || len > 24) return false;
  int slot = findUser(name);
  for (int i = 0; slot < 0 && i < kMaxUser; i++)
    if (!prefs.isKey(key('n', i).c_str())) slot = i;
  if (slot < 0) return false;
  uint8_t buf[sizeof(LedState) + 1] = {kVer};
  LedState copy = s;
  copy.probe = -1;
  memcpy(buf + 1, &copy, sizeof(LedState));
  prefs.putBytes(key('s', slot).c_str(), buf, sizeof(buf));
  prefs.putString(key('n', slot).c_str(), name);
  return true;
}

bool remove(const char* name) {
  int slot = findUser(name);
  if (slot < 0) return false;
  prefs.remove(key('n', slot).c_str());
  prefs.remove(key('s', slot).c_str());
  return true;
}

void list(JsonObject out) {
  out["show_builtin"] = showB;
  JsonArray b = out["builtin"].to<JsonArray>();
  if (showB)
    for (auto& p : kBuiltins) b.add(p.name);
  JsonArray u = out["user"].to<JsonArray>();
  for (int i = 0; i < kMaxUser; i++)
    if (prefs.isKey(key('n', i).c_str())) u.add(prefs.getString(key('n', i).c_str()));
}

}  // namespace presets
