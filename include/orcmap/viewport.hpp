#pragma once

#include <vector>

#include "orcmap/geo.hpp"

namespace orcmap {

// Map viewport: center, zoom, and output size. Geographic / tile / screen
// transform math lives here, not in a graphics adapter.
//
// Overzoom (source tile z != viewport zoom) is rejected, not guessed.
// X wraps the world (antimeridian). Y is clamped, never wrapped.
//
// Two enumeration modes, deliberately distinct:
//   - EnumerateVisibleTiles returns unique TileIds only; a tile that would
//     appear twice under wrapping is returned once. Use it to decide what to
//     fetch and decode.
//   - EnumerateVisibleTilePlacements returns one entry per drawn instance,
//     including repeats across world copies, so a world narrower than the
//     viewport extends around itself instead of leaving empty margin. Use it
//     to decide what to draw and where.
// Neither is an infinite scene graph: both are bounded by the screen.
//
// tile_size_px is screen pixels per map tile at `zoom` (Web Mercator
// convention defaults to 256). Tests may set it equal to the framebuffer
// so one z0 tile fills the target.

struct Viewport {
  double center_lat_deg = 0.0;
  double center_lon_deg = 0.0;
  uint8_t zoom = 0;
  int width_px = 256;
  int height_px = 256;
  int tile_size_px = 256;
};

bool SetCenter(Viewport* viewport, double lat_deg, double lon_deg);
LatLon GetCenter(const Viewport& viewport);
bool SetZoom(Viewport* viewport, int zoom);
uint8_t GetZoom(const Viewport& viewport);
bool ZoomIn(Viewport* viewport);
bool ZoomOut(Viewport* viewport);
bool SetViewportSize(Viewport* viewport, int width_px, int height_px);

// Positive dx/dy moves the camera east/south. Touch-drag bindings normally
// pass the negative gesture delta.
bool PanByPixels(Viewport* viewport, double dx, double dy);
bool ProjectLatLon(const Viewport& viewport, LatLon point, double* screen_x,
                   double* screen_y);
bool ScreenToLatLon(const Viewport& viewport, double screen_x,
                    double screen_y, LatLon* point);
bool GetVisibleBounds(const Viewport& viewport, GeoBounds* bounds);
// Centres on `bounds` and picks the LARGEST zoom at which all of `bounds`
// is visible inside the viewport (minus padding). Use this to show a pack's
// whole coverage area; the result usually leaves empty margin, because a
// pack's aspect ratio rarely matches the screen's.
bool FitBounds(Viewport* viewport, const GeoBounds& bounds, int padding_px);

// Centres on `bounds` and picks the SMALLEST zoom at which `bounds` fully
// covers the viewport -- the complement of FitBounds. Use this to fill the
// screen with map data instead of framing the coverage area, which is what
// a map application normally wants: correct centre, no empty margin, and a
// zoom derived from the caller's own width/height rather than a per-board
// constant.
//
// Returns false when no zoom up to the maximum can cover the viewport
// (bounds too small relative to the screen); callers should then fall back
// to FitBounds, which always succeeds for valid bounds.
bool FillBounds(Viewport* viewport, const GeoBounds& bounds);

// Query form of FillBounds: the smallest zoom at which `bounds` fully covers
// this viewport's width/height, WITHOUT moving the camera. Use it to derive a
// zoom-out floor for a UI control -- below this zoom the screen must show
// empty area, because no amount of panning can cover it from these bounds.
//
// Ignores any pack min_zoom/max_zoom; that clamp belongs to the caller, which
// knows what is actually installed. Returns false when no zoom up to the
// maximum can cover the viewport.
bool MinFillZoom(const Viewport& viewport, const GeoBounds& bounds,
                 uint8_t* out_zoom);

// Smallest zoom at which the world is at least as tall as the viewport.
//
// This is the natural "whole world" view for a given display. Horizontal
// extent needs no test: with world-copy placement (see TilePlacement) the map
// repeats around itself, so width is always covered. Height cannot repeat --
// Mercator Y is clamped, never wrapped -- so height alone decides.
//
// A caller with a global pack should clamp the result to that pack's zoom
// range. Returns false on an invalid viewport or when no zoom is tall enough.
bool WorldViewZoom(const Viewport& viewport, uint8_t* out_zoom);

// One drawn instance of a tile. `tile` is wrapped into [0, 2^z) and is what
// you look up in a pack; `unwrapped_x` is where that instance sits in world
// space and is what places it on screen, so the same tile can be drawn at
// several longitudes when the viewport is wider than the world.
//
// This is what makes the map extend around itself instead of leaving empty
// margin beside a world narrower than the screen.
struct TilePlacement {
  TileId tile;
  int64_t unwrapped_x = 0;
};

// Every drawn instance of every tile intersecting the viewport, including
// repeats across world copies. Order matches EnumerateVisibleTiles
// (north-to-south, then west-to-east in screen space).
//
// Unlike EnumerateVisibleTiles, the same TileId MAY appear more than once --
// once per world copy on screen. Decode each distinct TileId once and draw it
// at each of its placements; re-decoding per placement is wasted work.
//
// Returns false and clears `out` if `out` is null, zoom is invalid, or
// width/height/tile_size_px are <= 0.
bool EnumerateVisibleTilePlacements(const Viewport& viewport,
                                    std::vector<TilePlacement>* out);

// Placement of `tile` at the world copy nearest the viewport centre -- the
// single-copy rule MakeTileScreenMap uses. Returns false on invalid zoom,
// zoom mismatch, or out-of-range tile indices.
bool NearestTilePlacement(const Viewport& viewport, TileId tile,
                          TilePlacement* out);
bool ZoomAtScreenPoint(Viewport* viewport, double screen_x, double screen_y,
                       int zoom_delta);

// Prepared per-source-tile map: Mercator center math runs once, then each
// vertex is origin + local * scale. Invalid when zoom is out of 0..31,
// source tile z != viewport zoom, or `out` is null.
//
// The *_q16 fields are the same coefficients in Q16.16 and are what
// ProjectLocal uses: ESP32 has a single-precision FPU only, so a double
// multiply in a per-vertex path is a soft-float ROM call (~9 of them per
// coordinate). They are derived, so build a map with MakeTileScreenMap /
// MakeTilePlacementScreenMap rather than filling in the doubles yourself.
struct TileScreenMap {
  bool valid = false;
  uint8_t zoom = 0;
  double origin_sx = 0.0;  // screen x of tile-local (0, 0)
  double origin_sy = 0.0;
  double scale = 0.0;      // screen pixels per tile-local unit
  int64_t origin_sx_q16 = 0;
  int64_t origin_sy_q16 = 0;
  int64_t scale_q16 = 0;
};

bool MakeTileScreenMap(const Viewport& viewport, TileId tile, uint32_t extent,
                       TileScreenMap* out);

// As MakeTileScreenMap, but places the tile at its placement's world copy
// rather than at the copy nearest the centre. Use this whenever you drew
// placements, or repeats will all land on top of each other.
bool MakeTilePlacementScreenMap(const Viewport& viewport,
                                const TilePlacement& placement,
                                uint32_t extent, TileScreenMap* out);

// Unique Web Mercator source tiles that intersect the viewport at
// viewport.zoom (no overzoom). Order: north-to-south, then west-to-east
// in screen space (wrapped X increases to the right).
//
// X indices wrap in [0, 2^zoom). Y indices outside [0, 2^zoom) are
// dropped, not wrapped. Returns false and clears `out` if `out` is null,
// zoom is invalid, or width/height/tile_size_px are <= 0.
//
// Does not consult map storage: a returned tile may be absent from a
// pack. Duplicate TileIds are never emitted (z0 + a wide viewport yields
// one 0/0/0, not five), which is what makes this the right input to
// fetch/decode. It is NOT the right input to drawing when the world is
// narrower than the viewport, because each tile carries only one position:
// use EnumerateVisibleTilePlacements for that.
bool EnumerateVisibleTiles(const Viewport& viewport, std::vector<TileId>* out);

// Cheap path after MakeTileScreenMap. Writes saturated int screen coords.
// Integer-only: one 64-bit multiply and one shift per axis, no floating
// point. `map.valid` must be true.
void ProjectLocal(const TileScreenMap& map, int32_t local_x, int32_t local_y,
                  int* screen_x, int* screen_y);

// Convenience wrapper: prepare + project. No-op if pointers are null.
// Invalid zoom / zoom mismatch leaves outputs unchanged.
void TileLocalToScreen(const Viewport& viewport, TileId tile, uint32_t extent,
                       int32_t local_x, int32_t local_y, int* screen_x,
                       int* screen_y);

}  // namespace orcmap
