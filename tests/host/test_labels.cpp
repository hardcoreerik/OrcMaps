#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "orcmap/labels.hpp"
#include "orcmap/viewport.hpp"
#include "orcmap/style.hpp"
#include "orcmap/simple_text.hpp"

static bool watch_allocations = false;
static size_t allocations = 0;
void* operator new(size_t n) {
  if (watch_allocations) ++allocations;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

namespace {
int failures = 0;
void Check(bool value, const char* why) {
  if (!value) { std::printf("FAIL %s\n", why); ++failures; }
}
struct TextTarget : orcmap::LabelTextTarget {
  std::array<std::array<char, 96>, 64> drawn{};
  size_t count = 0;
  bool MeasureText(std::string_view text, orcmap::TextExtent* out) override {
    if (text == "unsupported") return false;
    *out = {static_cast<int>(text.size()) * 8, 24};
    return true;
  }
  void DrawText(int, int, std::string_view text, orcmap::Color, orcmap::Color) override {
    if (count < drawn.size()) {
      std::memcpy(drawn[count].data(), text.data(), text.size());
      drawn[count][text.size()] = 0;
    }
    ++count;
  }
};
}

int main() {
  orcmap::Feature feature;
  feature.property_keys = {"name", "name_en", "name:en"};
  feature.property_values = {std::string("local"), std::string("old English"),
                              std::string("English")};
  Check(orcmap::PreferredPlaceName(feature) == "English", "English first");
  feature.property_values[2] = std::string();
  Check(orcmap::PreferredPlaceName(feature) == "old English", "legacy alias");
  feature.property_values[1] = int64_t{3};
  Check(orcmap::PreferredPlaceName(feature) == "local", "local fallback/wrong type");
  feature.property_keys.clear();
  Check(orcmap::PreferredPlaceName(feature).empty(), "missing names");
  Check(orcmap::IsSimpleLabelText("S\xC3\xA3o Paulo"), "simple adapter diacritics");
  Check(!orcmap::IsSimpleLabelText("\xD8\xA7"), "unshaped adapters reject Arabic");

  orcmap::LabelLayout layout;
  TextTarget target;
  const auto fg = orcmap::Color::Rgb(220, 230, 240);
  const auto halo = orcmap::Color::Rgb(8, 10, 12);
  layout.BeginFrame(1280, 720);
  Check(layout.Offer("Eugene", 300, 300, 5, 1), "offer place");
  Check(layout.Offer("Springfield", 300, 300, 10, 2), "offer important overlapping place");
  layout.Draw(target, fg, halo);
  Check(target.count == 1 && std::string_view(target.drawn[0].data()) == "Springfield",
        "priority wins overlap");
  Check(layout.stats().overlap == 1, "overlap counter");

  layout.BeginFrame(1280, 720); target.count = 0;
  char name[] = "retained";
  layout.Offer(name, 400, 400, 1, 1);
  name[0] = 'X';
  layout.Offer("retained", 400, 400, 1, 1);
  layout.Draw(target, fg, halo);
  Check(target.count == 1 && std::string_view(target.drawn[0].data()) == "retained",
        "streaming name copied/deduplicated");
  Check(layout.stats().duplicates == 1, "duplicate counter");

  layout.BeginFrame(1280, 720); target.count = 0;
  Check(layout.Offer("S\xC3\xA3o Paulo", 300, 100, 0, 1), "UTF8 diacritic");
  Check(layout.Offer("\xE6\x9D\xB1\xE4\xBA\xAC", 600, 100, 0, 2), "UTF8 local name");
  Check(!layout.Offer(std::string_view("\xC0\xAF", 2), 100, 100, 0, 3), "reject overlong UTF8");
  Check(!layout.Offer(std::string_view("\xED\xA0\x80", 3), 100, 100, 0, 3), "reject surrogate");
  Check(!layout.Offer(std::string_view("\xF4\x90\x80\x80", 4), 100, 100, 0, 3), "reject out of Unicode range");
  Check(!layout.Offer(std::string_view("\xE6\x9D", 2), 100, 100, 0, 3), "reject truncated UTF8");
  Check(!layout.Offer("bad\nname", 100, 100, 0, 3), "reject line break");
  Check(!layout.Offer(std::string_view("nul\0name", 8), 100, 100, 0, 3), "reject embedded NUL");
  Check(!layout.Offer("", 100, 100, 0, 3), "reject empty");
  std::string long_name(96, 'a');
  Check(!layout.Offer(long_name, 100, 100, 0, 3), "reject oversized name without truncating");
  layout.Offer("unsupported", 900, 100, 0, 4);
  layout.Offer("edge", 1, 1, 0, 5);
  layout.Offer("outside", -1, 100, 0, 6);
  layout.Draw(target, fg, halo);
  Check(target.count == 2 && layout.stats().unsupported_glyphs == 1,
        "valid UTF8 drawn; missing glyph explicitly counted");
  Check(layout.stats().invalid_names == 7 && layout.stats().oversized_names == 1,
        "invalid/oversized counters");
  Check(layout.stats().offscreen == 2, "fully visible text only");

  // Equal-priority collision and full-pool retention must not depend on tile order.
  for (bool reverse : {false, true}) {
    layout.BeginFrame(1280, 720); target.count = 0;
    for (int i = 0; i < 140; ++i) {
      const int id = reverse ? 139 - i : i;
      char text[32]; std::snprintf(text, sizeof(text), "place-%03d", id);
      layout.Offer(text, 500, 300, id, static_cast<uint64_t>(id));
    }
    layout.Draw(target, fg, halo);
    Check(target.count == 1 && std::string_view(target.drawn[0].data()) == "place-139",
          "strongest retained independent of stream order");
    Check(layout.stats().pool_overflow == 12, "bounded pool overflow reported");
    layout.BeginFrame(1280, 720); target.count = 0;
    layout.Offer(reverse ? "Zulu" : "Alpha", 300, 300, 1, 1);
    layout.Offer(reverse ? "Alpha" : "Zulu", 300, 300, 1, 1);
    layout.Draw(target, fg, halo);
    Check(target.count == 1 && std::string_view(target.drawn[0].data()) == "Alpha",
          "stable equal-priority winner");
  }

  layout.BeginFrame(1280, 720); target.count = 0;
  allocations = 0; watch_allocations = true;
  for (int i = 0; i < 128; ++i)
    layout.Offer("x", 20 + (i % 16) * 70, 20 + (i / 16) * 70, 0, i);
  layout.Draw(target, fg, halo);
  watch_allocations = false;
  Check(target.count == 64 && layout.stats().draw_limit == 64, "bounded draw count");
  Check(allocations == 0, "no core allocations during offer/layout/draw");
  layout.BeginFrame(1280, 720); target.count = 0;
  layout.Draw(target, fg, halo);
  Check(target.count == 0 && layout.stats().offered == 0, "reset removes old frame");
  orcmap::Viewport view;
  view.width_px = 1280; view.height_px = 720;
  orcmap::TileScreenMap map;
  Check(orcmap::MakeTileScreenMap(view, {0, 0, 0}, 4096, &map), "place map prepared");
  feature.layer = "places";
  feature.kind = orcmap::FeatureKind::kLabelPrimary;
  feature.kind_assigned = true;
  feature.geometry.type = orcmap::GeomType::kPoint;
  feature.geometry.paths = {{{2048, 2048}, {-1, 2048}}};
  feature.property_keys = {"name:en", "min_zoom", "population_rank"};
  feature.property_values = {std::string("Eugene"), double{0.5}, int64_t{10}};
  layout.OfferPlace(feature, map, 0, orcmap::styles::OrcSdrDark());
  Check(layout.stats().offered == 0, "per-feature fractional min zoom");
  feature.property_values[1] = double{0};
  layout.OfferPlace(feature, map, 0, orcmap::styles::OrcSdrDark());
  layout.Draw(target, fg, halo);
  Check(target.count == 1 && std::string_view(target.drawn[0].data()) == "Eugene" &&
        layout.stats().offered == 1, "same-camera projection; buffered duplicate excluded");
  feature.layer = "pois";
  layout.OfferPlace(feature, map, 0, orcmap::styles::OrcSdrDark());
  Check(layout.stats().offered == 1, "POIs not silently treated as cities");
  std::printf("label tests: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
