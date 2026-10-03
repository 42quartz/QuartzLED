#pragma once
#include <Arduino.h>

// What the strip should show. Changed by any interface (serial, HTTP, MQTT, HomeKit, AI).
struct LedState {
  bool on = true;
  uint8_t bri = 64;                  // 0-255, before power limiting
  uint8_t r = 255, g = 140, b = 40;  // warm white
  uint8_t effect = 0;                // index into kEffects
  uint16_t speed = 128;              // 0-1000
  int16_t probe = -1;                // >=0: only this LED lit (calibration)
};

// How the hardware is wired. Changing chip needs a reboot.
struct LedConfig {
  uint16_t count = 150;
  uint8_t chip = 0;          // index into kChips
  char order[4] = "GRB";     // wire byte order
  uint16_t powerMa = 400;    // FastLED power budget at 5V
};

enum Effect : uint8_t { FX_SOLID, FX_RAINBOW, FX_CHASE, FX_BREATHE, FX_FIRE, FX_TWINKLE, FX_MARKS, FX_COUNT };
extern const char* const kEffects[FX_COUNT];

enum Chip : uint8_t { CHIP_WS2812, CHIP_WS2811_400, CHIP_UCS1903, CHIP_COUNT };
extern const char* const kChips[CHIP_COUNT];

int effectFromName(const char* name);  // -1 if unknown
int chipFromName(const char* name);
