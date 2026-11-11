#include "orcmap/style.hpp"

#include <cstring>
#include <initializer_list>

#include "orcmap/cache_key.hpp"
#include "test_util.hpp"

namespace {

void TestBuiltinStyleIdsResolve() {
  ORCMAP_EXPECT_TRUE(std::strcmp(orcmap::styles::OrcSdrDark().id, "orcsdr-dark") == 0);
  ORCMAP_EXPECT_TRUE(std::strcmp(orcmap::styles::StandardLight().id, "standard-light") == 0);
  ORCMAP_EXPECT_TRUE(std::strcmp(orcmap::styles::HighContrastField().id, "high-contrast-field") == 0);
  ORCMAP_EXPECT_TRUE(std::strcmp(orcmap::styles::NightRedSafe().id, "night-red-safe") == 0);

  ORCMAP_EXPECT_TRUE(
      std::strcmp(orcmap::GetBuiltinStyle(orcmap::BuiltinStyle::kOrcSdrDark).id,
                  "orcsdr-dark") == 0);
  ORCMAP_EXPECT_TRUE(orcmap::FindBuiltinStyleById("night-red-safe") != nullptr);
  ORCMAP_EXPECT_TRUE(orcmap::FindBuiltinStyleById("does-not-exist") == nullptr);
}

void TestDefaultStyleExists() {
  const orcmap::MapStyle& def = orcmap::DefaultStyle();
  ORCMAP_EXPECT_TRUE(std::strcmp(def.id, "standard-light") == 0);
}

void TestStyleManagerSwitching() {
  orcmap::StyleManager manager;
  ORCMAP_EXPECT_TRUE(std::strcmp(manager.Active().id, "standard-light") == 0);

  manager.SetStyle(orcmap::BuiltinStyle::kOrcSdrDark);
  ORCMAP_EXPECT_TRUE(std::strcmp(manager.Active().id, "orcsdr-dark") == 0);

  // Invalid id must not change the active style.
  const bool ok = manager.SetStyleById("not-a-real-style");
  ORCMAP_EXPECT_TRUE(!ok);
  ORCMAP_EXPECT_TRUE(std::strcmp(manager.Active().id, "orcsdr-dark") == 0);

  const bool ok2 = manager.SetStyleById("night-red-safe");
  ORCMAP_EXPECT_TRUE(ok2);
  ORCMAP_EXPECT_TRUE(std::strcmp(manager.Active().id, "night-red-safe") == 0);
}

void TestZoomVisibility() {
  using orcmap::FeatureKind;
  const orcmap::MapStyle& dark = orcmap::styles::OrcSdrDark();

  // Minor roads hidden at low zoom, visible once zoomed in.
  const orcmap::MapPaint low_zoom_minor =
      orcmap::ResolveFeatureStyle(FeatureKind::kMinorRoad, 2, dark);
  ORCMAP_EXPECT_TRUE(!low_zoom_minor.visible);
  const orcmap::MapPaint high_zoom_minor =
      orcmap::ResolveFeatureStyle(FeatureKind::kMinorRoad, 14, dark);
  ORCMAP_EXPECT_TRUE(high_zoom_minor.visible);

  // Major roads remain visible at low zoom.
  const orcmap::MapPaint low_zoom_motorway =
      orcmap::ResolveFeatureStyle(FeatureKind::kMotorway, 2, dark);
  ORCMAP_EXPECT_TRUE(low_zoom_motorway.visible);

  // Buildings only appear at high zoom.
  const orcmap::MapPaint low_zoom_building =
      orcmap::ResolveFeatureStyle(FeatureKind::kBuilding, 10, dark);
  ORCMAP_EXPECT_TRUE(!low_zoom_building.visible);

  // High-Contrast/Field disables rail entirely, and pushes minor roads to
  // an even higher zoom than the standard styles.
  const orcmap::MapStyle& field = orcmap::styles::HighContrastField();
  ORCMAP_EXPECT_TRUE(!orcmap::ResolveFeatureStyle(FeatureKind::kRail, 20, field).visible);
  ORCMAP_EXPECT_TRUE(!orcmap::ResolveFeatureStyle(FeatureKind::kMinorRoad, 12, field).visible);
  ORCMAP_EXPECT_TRUE(orcmap::ResolveFeatureStyle(FeatureKind::kMinorRoad, 14, field).visible);
}

void TestAdministrativeBoundaryHierarchy() {
  using K = orcmap::FeatureKind;
  // Appended kinds preserve numeric identities used by existing consumers.
  ORCMAP_EXPECT_TRUE(static_cast<int>(K::kBoundary) == 8);
  ORCMAP_EXPECT_TRUE(static_cast<int>(K::kLabelMuted) == 14);
  for (auto which : {orcmap::BuiltinStyle::kOrcSdrDark,
                     orcmap::BuiltinStyle::kStandardLight,
                     orcmap::BuiltinStyle::kHighContrastField,
                     orcmap::BuiltinStyle::kNightRedSafe}) {
    const auto& style = orcmap::GetBuiltinStyle(which);
    const auto country = orcmap::ResolveFeatureStyle(K::kBoundaryCountry, 0, style);
    const auto region = orcmap::ResolveFeatureStyle(K::kBoundaryRegion, 4, style);
    const auto local = orcmap::ResolveFeatureStyle(K::kBoundaryLocal, 6, style);
    ORCMAP_EXPECT_TRUE(country.visible && region.visible && local.visible);
    ORCMAP_EXPECT_TRUE(!orcmap::ResolveFeatureStyle(K::kBoundaryRegion, 3, style).visible);
    ORCMAP_EXPECT_TRUE(!orcmap::ResolveFeatureStyle(K::kBoundaryLocal, 5, style).visible);
    ORCMAP_EXPECT_TRUE(country.width_px > region.width_px && region.width_px > local.width_px);
    ORCMAP_EXPECT_TRUE(country.color != region.color && region.color != local.color);
    ORCMAP_EXPECT_TRUE(country.color != orcmap::Color{});
    ORCMAP_EXPECT_TRUE(region.color != orcmap::Color{});
    ORCMAP_EXPECT_TRUE(local.color != orcmap::Color{});
    ORCMAP_EXPECT_TRUE(orcmap::ResolveFeatureStyle(K::kBoundary, 0, style).visible);
    ORCMAP_EXPECT_TRUE(style.version == 3);
    auto previous = style;
    previous.version = 2;
    ORCMAP_EXPECT_TRUE(
        orcmap::RenderedTileCacheKey::Make("global", 6, 10, 20, style) !=
        orcmap::RenderedTileCacheKey::Make("global", 6, 10, 20, previous));
  }
}

void TestNightStyleHasNoBlueLight() {
  // Every color in Night/Red-Safe must be red-dominant with zero-or-low
  // blue, per its design brief ("no blue light", "no bright white").
  const orcmap::MapStyle& night = orcmap::styles::NightRedSafe();
  for (const auto& rule : night.rules) {
    ORCMAP_EXPECT_TRUE(rule.color.b <= rule.color.r);
    ORCMAP_EXPECT_TRUE(!(rule.color.r > 240 && rule.color.g > 240 && rule.color.b > 240));
  }
}

void TestStylesAreDistinct() {
  // The four built-ins must not accidentally collapse into duplicates
  // (e.g. a copy-paste that never changed the background color).
  const orcmap::Color dark_bg = orcmap::styles::OrcSdrDark().rules[0].color;
  const orcmap::Color light_bg = orcmap::styles::StandardLight().rules[0].color;
  const orcmap::Color field_bg = orcmap::styles::HighContrastField().rules[0].color;
  const orcmap::Color night_bg = orcmap::styles::NightRedSafe().rules[0].color;
  ORCMAP_EXPECT_TRUE(dark_bg != light_bg);
  ORCMAP_EXPECT_TRUE(dark_bg != field_bg);
  ORCMAP_EXPECT_TRUE(dark_bg != night_bg);
  ORCMAP_EXPECT_TRUE(light_bg != field_bg);
  ORCMAP_EXPECT_TRUE(light_bg != night_bg);
  ORCMAP_EXPECT_TRUE(field_bg != night_bg);
}

void TestCacheKeyChangesWithStyle() {
  const orcmap::MapStyle& dark = orcmap::styles::OrcSdrDark();
  const orcmap::MapStyle& light = orcmap::styles::StandardLight();

  const auto key_dark =
      orcmap::RenderedTileCacheKey::Make("lane_county", 10, 163, 355, dark);
  const auto key_light =
      orcmap::RenderedTileCacheKey::Make("lane_county", 10, 163, 355, light);
  ORCMAP_EXPECT_TRUE(key_dark != key_light);

  // Same style, same tile -> identical key (cache hit expected).
  const auto key_dark_again =
      orcmap::RenderedTileCacheKey::Make("lane_county", 10, 163, 355, dark);
  ORCMAP_EXPECT_TRUE(key_dark == key_dark_again);

  // Bumping a style's version (simulating an edited built-in style)
  // invalidates the cache key even though the id is unchanged.
  orcmap::MapStyle dark_v2 = dark;
  dark_v2.version = dark.version + 1;
  const auto key_dark_v2 =
      orcmap::RenderedTileCacheKey::Make("lane_county", 10, 163, 355, dark_v2);
  ORCMAP_EXPECT_TRUE(key_dark != key_dark_v2);

  // Different tile coordinates -> different key.
  const auto key_other_tile =
      orcmap::RenderedTileCacheKey::Make("lane_county", 10, 163, 356, dark);
  ORCMAP_EXPECT_TRUE(key_dark != key_other_tile);

  // Different pack -> different key, even for the same z/x/y/style.
  const auto key_other_pack =
      orcmap::RenderedTileCacheKey::Make("oregon", 10, 163, 355, dark);
  ORCMAP_EXPECT_TRUE(key_dark != key_other_pack);
}

}  // namespace

void RunStyleTests() {
  TestBuiltinStyleIdsResolve();
  TestDefaultStyleExists();
  TestStyleManagerSwitching();
  TestZoomVisibility();
  TestAdministrativeBoundaryHierarchy();
  TestNightStyleHasNoBlueLight();
  TestStylesAreDistinct();
  TestCacheKeyChangesWithStyle();
}
