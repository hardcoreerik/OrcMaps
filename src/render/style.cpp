#include "orcmap/style.hpp"

#include <cstring>

namespace orcmap {

MapPaint ResolveFeatureStyle(FeatureKind kind, uint8_t zoom,
                              const MapStyle& style) {
  const FeatureRule& rule = style.Rule(kind);
  MapPaint paint;
  paint.visible = rule.visible && zoom >= rule.min_zoom && zoom <= rule.max_zoom;
  paint.color = rule.color;
  paint.width_px = rule.width_px;
  return paint;
}

namespace {

using K = FeatureKind;

// Common zoom-visibility policy shared by the three "normal" styles
// (Dark/Light/Night). High-Contrast/Field overrides several of these to
// deliberately show less at once -- see BuildHighContrastField().
void ApplyStandardZoomGating(MapStyle* style) {
  style->rules[static_cast<size_t>(K::kBackground)].min_zoom = 0;
  style->rules[static_cast<size_t>(K::kLand)].min_zoom = 0;
  style->rules[static_cast<size_t>(K::kWater)].min_zoom = 0;
  style->rules[static_cast<size_t>(K::kMotorway)].min_zoom = 0;
  style->rules[static_cast<size_t>(K::kPrimaryRoad)].min_zoom = 4;
  style->rules[static_cast<size_t>(K::kSecondaryRoad)].min_zoom = 8;
  style->rules[static_cast<size_t>(K::kMinorRoad)].min_zoom = 12;
  style->rules[static_cast<size_t>(K::kRail)].min_zoom = 9;
  style->rules[static_cast<size_t>(K::kBoundary)].min_zoom = 0;
  style->rules[static_cast<size_t>(K::kBoundaryCountry)].min_zoom = 0;
  style->rules[static_cast<size_t>(K::kBoundaryCountry)].width_px = 2.0f;
  style->rules[static_cast<size_t>(K::kBoundaryRegion)].min_zoom = 4;
  style->rules[static_cast<size_t>(K::kBoundaryRegion)].width_px = 1.5f;
  style->rules[static_cast<size_t>(K::kBoundaryLocal)].min_zoom = 6;
  style->rules[static_cast<size_t>(K::kBoundaryLocal)].width_px = 1.0f;
  style->rules[static_cast<size_t>(K::kBuilding)].min_zoom = 15;
  style->rules[static_cast<size_t>(K::kPark)].min_zoom = 6;
  style->rules[static_cast<size_t>(K::kAirport)].min_zoom = 5;
  style->rules[static_cast<size_t>(K::kLabelPrimary)].min_zoom = 0;
  style->rules[static_cast<size_t>(K::kLabelSecondary)].min_zoom = 8;
  style->rules[static_cast<size_t>(K::kLabelMuted)].min_zoom = 0;
}

MapStyle BuildOrcSdrDark() {
  MapStyle s;
  s.id = "orcsdr-dark";
  s.display_name = "OrcSDR Dark";
  s.version = 3;
  s.label_scale = 1.0f;
  s.label_priority_threshold = 0;

  // Professional RF-instrument aesthetic: the base map stays visually
  // quiet (muted charcoal/graphite, desaturated blue water, restrained
  // labels) so ADS-B/LoRa/RF overlays -- which use their own semantic
  // colors (cyan=selection, green=active, amber=warning, red=alarm) --
  // are what the eye lands on. See docs/STYLING.md.
  s.rules[static_cast<size_t>(K::kBackground)].color = Color::Rgb(8, 10, 12);
  s.rules[static_cast<size_t>(K::kLand)].color = Color::Rgb(16, 19, 22);
  s.rules[static_cast<size_t>(K::kWater)].color = Color::Rgb(19, 32, 43);
  s.rules[static_cast<size_t>(K::kMotorway)].color = Color::Rgb(58, 70, 80);
  s.rules[static_cast<size_t>(K::kMotorway)].width_px = 3.0f;
  s.rules[static_cast<size_t>(K::kPrimaryRoad)].color = Color::Rgb(49, 59, 68);
  s.rules[static_cast<size_t>(K::kPrimaryRoad)].width_px = 2.5f;
  s.rules[static_cast<size_t>(K::kSecondaryRoad)].color = Color::Rgb(38, 46, 53);
  s.rules[static_cast<size_t>(K::kSecondaryRoad)].width_px = 1.8f;
  s.rules[static_cast<size_t>(K::kMinorRoad)].color = Color::Rgb(29, 35, 41);
  s.rules[static_cast<size_t>(K::kMinorRoad)].width_px = 1.0f;
  s.rules[static_cast<size_t>(K::kRail)].color = Color::Rgb(42, 47, 51);
  s.rules[static_cast<size_t>(K::kBoundary)].color = Color::Rgb(46, 58, 66);
  s.rules[static_cast<size_t>(K::kBoundaryCountry)].color = Color::Rgb(117, 123, 130);
  s.rules[static_cast<size_t>(K::kBoundaryRegion)].color = Color::Rgb(91, 97, 105);
  s.rules[static_cast<size_t>(K::kBoundaryLocal)].color = Color::Rgb(66, 74, 82);
  s.rules[static_cast<size_t>(K::kBuilding)].color = Color::Rgb(23, 27, 30);
  s.rules[static_cast<size_t>(K::kPark)].color = Color::Rgb(22, 32, 26);
  s.rules[static_cast<size_t>(K::kAirport)].color = Color::Rgb(61, 74, 82);
  s.rules[static_cast<size_t>(K::kAirport)].width_px = 2.0f;
  s.rules[static_cast<size_t>(K::kLabelPrimary)].color = Color::Rgb(216, 222, 227);
  s.rules[static_cast<size_t>(K::kLabelSecondary)].color = Color::Rgb(138, 147, 155);
  s.rules[static_cast<size_t>(K::kLabelMuted)].color = Color::Rgb(90, 97, 105);

  ApplyStandardZoomGating(&s);
  return s;
}

MapStyle BuildStandardLight() {
  MapStyle s;
  s.id = "standard-light";
  s.display_name = "Standard Light";
  s.version = 3;
  s.label_scale = 1.0f;

  // Conventional daytime cartography -- OrcMaps' generic, application-
  // neutral default (orcmap::DefaultStyle()), not tuned for OrcSDR.
  s.rules[static_cast<size_t>(K::kBackground)].color = Color::Rgb(247, 247, 245);
  s.rules[static_cast<size_t>(K::kLand)].color = Color::Rgb(242, 240, 234);
  s.rules[static_cast<size_t>(K::kWater)].color = Color::Rgb(169, 205, 232);
  s.rules[static_cast<size_t>(K::kMotorway)].color = Color::Rgb(232, 151, 78);
  s.rules[static_cast<size_t>(K::kMotorway)].width_px = 3.0f;
  s.rules[static_cast<size_t>(K::kPrimaryRoad)].color = Color::Rgb(247, 195, 107);
  s.rules[static_cast<size_t>(K::kPrimaryRoad)].width_px = 2.5f;
  s.rules[static_cast<size_t>(K::kSecondaryRoad)].color = Color::Rgb(253, 253, 251);
  s.rules[static_cast<size_t>(K::kSecondaryRoad)].width_px = 1.8f;
  s.rules[static_cast<size_t>(K::kMinorRoad)].color = Color::Rgb(227, 224, 216);
  s.rules[static_cast<size_t>(K::kMinorRoad)].width_px = 1.0f;
  s.rules[static_cast<size_t>(K::kRail)].color = Color::Rgb(185, 180, 168);
  s.rules[static_cast<size_t>(K::kBoundary)].color = Color::Rgb(183, 159, 203);
  s.rules[static_cast<size_t>(K::kBoundaryCountry)].color = Color::Rgb(111, 91, 125);
  s.rules[static_cast<size_t>(K::kBoundaryRegion)].color = Color::Rgb(141, 122, 154);
  s.rules[static_cast<size_t>(K::kBoundaryLocal)].color = Color::Rgb(175, 159, 185);
  s.rules[static_cast<size_t>(K::kBuilding)].color = Color::Rgb(217, 211, 197);
  s.rules[static_cast<size_t>(K::kPark)].color = Color::Rgb(200, 227, 176);
  s.rules[static_cast<size_t>(K::kAirport)].color = Color::Rgb(201, 182, 217);
  s.rules[static_cast<size_t>(K::kAirport)].width_px = 2.0f;
  s.rules[static_cast<size_t>(K::kLabelPrimary)].color = Color::Rgb(43, 43, 38);
  s.rules[static_cast<size_t>(K::kLabelSecondary)].color = Color::Rgb(92, 92, 84);
  s.rules[static_cast<size_t>(K::kLabelMuted)].color = Color::Rgb(140, 140, 130);

  ApplyStandardZoomGating(&s);
  return s;
}

MapStyle BuildHighContrastField() {
  MapStyle s;
  s.id = "high-contrast-field";
  s.display_name = "High Contrast / Field";
  s.version = 3;
  s.label_scale = 1.3f;  // Larger labels, per docs/STYLING.md.
  s.label_priority_threshold = 2;  // Only higher-priority labels draw.

  // Maximum readability at a glance, minimum clutter: strong road
  // hierarchy, few minor features, large labels. For field radio /
  // Meshtastic / outdoor use, not OrcSDR-specific.
  s.rules[static_cast<size_t>(K::kBackground)].color = Color::Rgb(255, 255, 255);
  s.rules[static_cast<size_t>(K::kLand)].color = Color::Rgb(255, 255, 255);
  s.rules[static_cast<size_t>(K::kWater)].color = Color::Rgb(21, 101, 192);
  s.rules[static_cast<size_t>(K::kMotorway)].color = Color::Rgb(0, 0, 0);
  s.rules[static_cast<size_t>(K::kMotorway)].width_px = 4.0f;
  s.rules[static_cast<size_t>(K::kPrimaryRoad)].color = Color::Rgb(32, 32, 32);
  s.rules[static_cast<size_t>(K::kPrimaryRoad)].width_px = 3.0f;
  s.rules[static_cast<size_t>(K::kSecondaryRoad)].color = Color::Rgb(75, 75, 75);
  s.rules[static_cast<size_t>(K::kSecondaryRoad)].width_px = 2.0f;
  s.rules[static_cast<size_t>(K::kMinorRoad)].color = Color::Rgb(122, 122, 122);
  s.rules[static_cast<size_t>(K::kMinorRoad)].width_px = 1.0f;
  s.rules[static_cast<size_t>(K::kRail)].visible = false;  // Reduce clutter.
  s.rules[static_cast<size_t>(K::kBoundary)].color = Color::Rgb(176, 0, 32);
  s.rules[static_cast<size_t>(K::kBoundary)].width_px = 1.5f;
  s.rules[static_cast<size_t>(K::kBoundaryCountry)].color = Color::Rgb(83, 58, 91);
  s.rules[static_cast<size_t>(K::kBoundaryRegion)].color = Color::Rgb(119, 95, 128);
  s.rules[static_cast<size_t>(K::kBoundaryLocal)].color = Color::Rgb(155, 137, 163);
  s.rules[static_cast<size_t>(K::kBuilding)].color = Color::Rgb(208, 208, 208);
  s.rules[static_cast<size_t>(K::kPark)].color = Color::Rgb(63, 143, 70);
  s.rules[static_cast<size_t>(K::kAirport)].color = Color::Rgb(13, 71, 161);
  s.rules[static_cast<size_t>(K::kAirport)].width_px = 3.0f;
  s.rules[static_cast<size_t>(K::kLabelPrimary)].color = Color::Rgb(0, 0, 0);
  s.rules[static_cast<size_t>(K::kLabelSecondary)].color = Color::Rgb(51, 51, 51);
  s.rules[static_cast<size_t>(K::kLabelMuted)].color = Color::Rgb(102, 102, 102);

  ApplyStandardZoomGating(&s);
  s.rules[static_cast<size_t>(K::kMinorRoad)].min_zoom = 14;  // Fewer minor features.
  s.rules[static_cast<size_t>(K::kBuilding)].min_zoom = 17;   // Effectively off.
  s.rules[static_cast<size_t>(K::kSecondaryRoad)].min_zoom = 10;
  return s;
}

MapStyle BuildNightRedSafe() {
  MapStyle s;
  s.id = "night-red-safe";
  s.display_name = "Night / Red-Safe";
  s.version = 3;
  s.label_scale = 1.0f;

  // Dark-adaptation-preserving: black background, deep reds only, no
  // blue/white light anywhere (not even in water, unlike every other
  // style here). Astronomy / night field work / vehicle use at night.
  s.rules[static_cast<size_t>(K::kBackground)].color = Color::Rgb(0, 0, 0);
  s.rules[static_cast<size_t>(K::kLand)].color = Color::Rgb(10, 0, 0);
  s.rules[static_cast<size_t>(K::kWater)].color = Color::Rgb(26, 5, 5);
  s.rules[static_cast<size_t>(K::kMotorway)].color = Color::Rgb(107, 20, 20);
  s.rules[static_cast<size_t>(K::kMotorway)].width_px = 3.0f;
  s.rules[static_cast<size_t>(K::kPrimaryRoad)].color = Color::Rgb(85, 16, 16);
  s.rules[static_cast<size_t>(K::kPrimaryRoad)].width_px = 2.5f;
  s.rules[static_cast<size_t>(K::kSecondaryRoad)].color = Color::Rgb(64, 12, 12);
  s.rules[static_cast<size_t>(K::kSecondaryRoad)].width_px = 1.8f;
  s.rules[static_cast<size_t>(K::kMinorRoad)].color = Color::Rgb(46, 8, 8);
  s.rules[static_cast<size_t>(K::kMinorRoad)].width_px = 1.0f;
  s.rules[static_cast<size_t>(K::kRail)].color = Color::Rgb(58, 10, 10);
  s.rules[static_cast<size_t>(K::kBoundary)].color = Color::Rgb(74, 16, 16);
  s.rules[static_cast<size_t>(K::kBoundaryCountry)].color = Color::Rgb(116, 24, 24);
  s.rules[static_cast<size_t>(K::kBoundaryRegion)].color = Color::Rgb(91, 19, 19);
  s.rules[static_cast<size_t>(K::kBoundaryLocal)].color = Color::Rgb(66, 14, 14);
  s.rules[static_cast<size_t>(K::kBuilding)].color = Color::Rgb(28, 5, 5);
  s.rules[static_cast<size_t>(K::kPark)].color = Color::Rgb(36, 8, 8);  // Deliberately not green.
  s.rules[static_cast<size_t>(K::kAirport)].color = Color::Rgb(122, 24, 24);
  s.rules[static_cast<size_t>(K::kAirport)].width_px = 2.0f;
  s.rules[static_cast<size_t>(K::kLabelPrimary)].color = Color::Rgb(179, 58, 58);
  s.rules[static_cast<size_t>(K::kLabelSecondary)].color = Color::Rgb(122, 38, 38);
  s.rules[static_cast<size_t>(K::kLabelMuted)].color = Color::Rgb(74, 23, 23);

  ApplyStandardZoomGating(&s);
  return s;
}

}  // namespace

namespace styles {

const MapStyle& OrcSdrDark() {
  static const MapStyle s = BuildOrcSdrDark();
  return s;
}
const MapStyle& StandardLight() {
  static const MapStyle s = BuildStandardLight();
  return s;
}
const MapStyle& HighContrastField() {
  static const MapStyle s = BuildHighContrastField();
  return s;
}
const MapStyle& NightRedSafe() {
  static const MapStyle s = BuildNightRedSafe();
  return s;
}

}  // namespace styles

const MapStyle& GetBuiltinStyle(BuiltinStyle which) {
  switch (which) {
    case BuiltinStyle::kOrcSdrDark:
      return styles::OrcSdrDark();
    case BuiltinStyle::kStandardLight:
      return styles::StandardLight();
    case BuiltinStyle::kHighContrastField:
      return styles::HighContrastField();
    case BuiltinStyle::kNightRedSafe:
      return styles::NightRedSafe();
    case BuiltinStyle::kCount:
      break;
  }
  return styles::StandardLight();
}

const MapStyle* FindBuiltinStyleById(const char* id) {
  if (id == nullptr) return nullptr;
  static const BuiltinStyle kAll[] = {
      BuiltinStyle::kOrcSdrDark,
      BuiltinStyle::kStandardLight,
      BuiltinStyle::kHighContrastField,
      BuiltinStyle::kNightRedSafe,
  };
  for (BuiltinStyle which : kAll) {
    const MapStyle& style = GetBuiltinStyle(which);
    if (std::strcmp(style.id, id) == 0) return &style;
  }
  return nullptr;
}

}  // namespace orcmap
