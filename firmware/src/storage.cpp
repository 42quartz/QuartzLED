#include "storage.h"

#include <Preferences.h>

namespace storage {
namespace {
Preferences prefs;
constexpr uint8_t kStateVer = 1;
constexpr uint8_t kConfigVer = 1;
}  // namespace

void begin() { prefs.begin("led", false); }

// Blobs carry a version byte so a layout change falls back to defaults instead of garbage.
void load(LedState& s, LedConfig& c) {
  uint8_t buf[sizeof(LedConfig) + 1];
  if (prefs.isKey("cfg") && prefs.getBytes("cfg", buf, sizeof(buf)) == sizeof(buf) && buf[0] == kConfigVer)
    memcpy(&c, buf + 1, sizeof(LedConfig));
  uint8_t sbuf[sizeof(LedState) + 1];
  if (prefs.isKey("state") && prefs.getBytes("state", sbuf, sizeof(sbuf)) == sizeof(sbuf) && sbuf[0] == kStateVer)
    memcpy(&s, sbuf + 1, sizeof(LedState));
  s.probe = -1;  // calibration mode never survives a reboot
}

void saveState(const LedState& s) {
  uint8_t buf[sizeof(LedState) + 1] = {kStateVer};
  memcpy(buf + 1, &s, sizeof(LedState));
  prefs.putBytes("state", buf, sizeof(buf));
}

void saveConfig(const LedConfig& c) {
  uint8_t buf[sizeof(LedConfig) + 1] = {kConfigVer};
  memcpy(buf + 1, &c, sizeof(LedConfig));
  prefs.putBytes("cfg", buf, sizeof(buf));
}

uint32_t bumpBootCount() {
  uint32_t n = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", n);
  return n;
}

}  // namespace storage
