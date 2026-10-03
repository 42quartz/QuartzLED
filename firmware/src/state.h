#pragma once
#include <Arduino.h>

// What the strip should show. Changed by any interface (serial, HTTP, MQTT, HomeKit, AI).
struct LedState {
  bool on = true;
  uint8_t bri = 64;                     // 0-255 output level (linear; UIs apply sliderCurve)
  uint8_t r = 255, g = 140, b = 40;     // primary color
  uint8_t r2 = 0, g2 = 80, b2 = 255;    // secondary color (gradient, theater, two-color)
  uint8_t effect = 0;                   // index into kEffects
  uint16_t speed = 300;                 // 0-1000, 0 freezes animation
  uint8_t intensity = 128;              // effect-specific: tail length, density, flicker...
  uint8_t palette = 0;                  // index into kPalettes (palette-based effects)
  bool reverse = false;
  bool mirror = false;                  // render half the strip and mirror it from the center
  int16_t probe = -1;                   // >=0: only this LED lit (calibration)
};

// How the hardware is wired. Changing chip needs a reboot.
struct LedConfig {
  uint16_t count = 150;
  uint8_t chip = 0;          // index into kChips
  char order[4] = "GRB";     // wire byte order
  uint16_t powerMa = 400;    // power budget at 5V
};

enum Effect : uint8_t {
  FX_SOLID, FX_RAINBOW, FX_CHASE, FX_BREATHE, FX_FIRE, FX_TWINKLE, FX_MARKS,
  FX_COLORLOOP, FX_GRADIENT, FX_SCANNER, FX_THEATER, FX_WAVE, FX_NOISE, FX_CONFETTI,
  FX_CANDLE, FX_SPARKLE, FX_POLICE, FX_PULSE, FX_JUGGLE, FX_METEOR, FX_SUNRISE,
  FX_TWOCOLOR, FX_HEARTBEAT, FX_COUNT
};
extern const char* const kEffects[FX_COUNT];

enum Palette : uint8_t {
  PAL_RAINBOW, PAL_PARTY, PAL_OCEAN, PAL_LAVA, PAL_FOREST, PAL_HEAT, PAL_CLOUD,
  PAL_SUNSET, PAL_AURORA, PAL_PASTEL, PAL_COLORS, PAL_COUNT
};
extern const char* const kPalettes[PAL_COUNT];

enum Chip : uint8_t { CHIP_WS2812, CHIP_WS2811_400, CHIP_UCS1903, CHIP_COUNT };
extern const char* const kChips[CHIP_COUNT];

int effectFromName(const char* name);  // -1 if unknown
bool effectUsesColor(uint8_t fx);       // false for rainbow/fire/palette effects...
int paletteFromName(const char* name);
int chipFromName(const char* name);

// Slider position (0..1) <-> value fraction (0..1). Dense at the low end: 50% -> 10%.
// Shared by HomeKit and the web UI so every interface feels the same.
inline float sliderToValue(float p) { return (powf(81.0f, constrain(p, 0.0f, 1.0f)) - 1.0f) / 80.0f; }
inline float valueToSlider(float v) { return logf(1.0f + 80.0f * constrain(v, 0.0f, 1.0f)) / logf(81.0f); }
