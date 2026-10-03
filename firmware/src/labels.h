#pragma once
#include "state.h"

// Human-facing (Turkish) names for effects, palettes and built-in scenes.
// The engine and API use the ASCII ids; these are for Home Assistant / Google Home.
struct Label {
  const char* id;
  const char* tr;
};

// Order is the display order; FX_MARKS (calibration) is intentionally absent.
static const Label kEffectLabels[] = {
    {"solid", "Sabit"}, {"rainbow", "Gökkuşağı"}, {"colorloop", "Renk Döngüsü"}, {"breathe", "Nefes"},
    {"chase", "Kayan Işık"}, {"scanner", "Tarayıcı"}, {"meteor", "Meteor"}, {"theater", "Tiyatro"},
    {"twocolor", "İki Renk"}, {"gradient", "Gradyan"}, {"wave", "Dalga"}, {"noise", "Akış"},
    {"confetti", "Konfeti"}, {"juggle", "Hokkabaz"}, {"fire", "Ateş"}, {"candle", "Mum"},
    {"twinkle", "Pırıltı"}, {"sparkle", "Işıltı"}, {"pulse", "Nabız"}, {"heartbeat", "Kalp Atışı"},
    {"police", "Polis"}, {"sunrise", "Gün Doğumu"}, {"sunset", "Gün Batımı"},
};

static const Label kPaletteLabels[] = {
    {"rainbow", "Gökkuşağı"}, {"party", "Parti"}, {"ocean", "Okyanus"}, {"lava", "Lav"},
    {"forest", "Orman"}, {"heat", "Isı"}, {"cloud", "Bulut"}, {"sunset", "Gün Batımı"},
    {"aurora", "Kuzey Işıkları"}, {"pastel", "Pastel"}, {"colors", "Renklerim"},
};

static const Label kPresetLabels[] = {
    {"okuma", "Okuma"}, {"odak", "Odak"}, {"film", "Film"}, {"gece", "Gece Lambası"},
    {"rahat", "Rahatlama"}, {"kutup", "Kuzey Işıkları"}, {"gunbatimi", "Alacakaranlık"},
    {"somine", "Şömine"}, {"mum", "Mum Işığı"}, {"romantik", "Romantik"}, {"parti", "Parti"},
    {"disko", "Disko"},
};

template <size_t N>
const char* labelFor(const Label (&table)[N], const char* id) {
  for (auto& l : table)
    if (!strcmp(l.id, id)) return l.tr;
  return id;
}
