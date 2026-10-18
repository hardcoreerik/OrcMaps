// OrcMaps benchmark instrument for the "Cheap Yellow Display" 3.5"
// (ESP32-3248S035: ESP32-D0WD-V3, ST7796 320x480, microSD on SPI).
//
// This is a BENCHMARK INSTRUMENT, not a port of the Tab5 demo. The board has
// no PSRAM -- roughly 300 KB of DRAM total, while a single 320x480x16bpp
// framebuffer is 307,200 bytes -- so the Tab5's offscreen-canvas drag model
// cannot exist here and is deliberately not attempted. Rendering goes
// straight to the panel, which means render timings on this board include
// SPI transfer; that is the real cost of drawing on a CYD.
//
// Everything measured comes from the shared, board-agnostic harness
// (examples/hardware-benchmark), and packs are found by the same runtime
// discovery the Tab5 uses. This file owns only board specifics: the ST7796
// bring-up, the SPI SD mount, heap probes, and the report file.
//
// Pin mapping is the configuration already proven on this hardware in the
// NEONDRIVE firmware (include/variants/cyd35/cyd35_hw_config.h), not a
// guess: display on HSPI, SD on a separate VSPI bus, so the two never
// contend.

#include <LovyanGFX.hpp>
#include <lgfx/v1/touch/Touch_XPT2046.hpp>

#include "orcmap/compression.hpp"
#include "orcmap/esp_idf/file_byte_source.hpp"
#include "orcmap/esp_idf/pack_filesystem.hpp"
#include "orcmap/experimental/mvt_classify.hpp"
#include "orcmap/feature.hpp"
#include "orcmap/m5gfx/display_target.hpp"
#include "orcmap/pack.hpp"
#include "orcmap/pack_discovery.hpp"
#include "orcmap/pmtiles.hpp"
#include "orcmap/renderer.hpp"
#include "orcmap/style.hpp"
#include "orcmap/viewport.hpp"
#include "orcmap_bench/bench.hpp"

#include <driver/gpio.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr char kTag[] = "orcmap_cyd35";
constexpr char kBenchVersion[] = "cyd35-bench-1";

// The board owns the path; OrcMaps core never names an SD mount.
constexpr char kPackDir[] = "/sd/orcmaps";
constexpr char kReportDir[] = "/sd/orcmaps";

// Verified CYD 3.5" pin map (NEONDRIVE cyd35_hw_config.h). Display is on
// HSPI/SPI2, SD on VSPI/SPI3 -- separate buses.
constexpr int kTftMiso = 12;
constexpr int kTftMosi = 13;
constexpr int kTftSclk = 14;
constexpr int kTftCs = 15;
constexpr int kTftDc = 2;
constexpr int kTftRst = -1;  // tied high on this board
constexpr int kBacklight0 = 21;
// Rev A drives a second backlight line; Rev B leaves it unused. Driving it
// high is harmless on Rev B, and leaving it low on Rev A gives a dark panel.
constexpr int kBacklight1 = 27;

constexpr int kSdSclk = 18;
constexpr int kSdMiso = 19;
constexpr int kSdMosi = 23;
constexpr int kSdCs = 5;

// Explicit SPI DMA channel assignment; see the note in the bus config.
constexpr int kDisplayDmaChannel = 1;
constexpr int kSdDmaChannel = 2;

// LANDSCAPE. The panel is 320x480 portrait natively; rotation 1 presents it
// as 480x320, which is how this board is actually held -- text drawn in the
// native portrait orientation reads sideways on the bench.
constexpr int kDisplayRotation = 1;
constexpr int kDisplayW = 480;
constexpr int kDisplayH = 320;
// Native panel geometry, before rotation. LovyanGFX wants the panel's own
// dimensions here; rotation is applied on top.
constexpr int kPanelW = 320;
constexpr int kPanelH = 480;

// Memory policy for a board with ~300 KB of DRAM and no PSRAM.
//
// The budget is an upper bound per tile payload, not a reservation; measured
// OpenMapTiles tiles decompress to roughly 4-40 KB each. 192 KB is generous
// for that while still being refused long before it could exhaust the heap.
// Unused by the streaming path (nothing is inflated whole any more); kept
// for the fallback path and as the guard on a hostile payload.
constexpr size_t kDecompressBudget = 160u * 1024u;
// Below this much free internal memory the harness skips a tile instead of
// attempting an allocation that would abort the firmware (exceptions are
// disabled, so a failed allocation terminates rather than throws). A skipped
// tile is recorded; a crash would take the whole run with it.
constexpr size_t kHeapFloor = 24u * 1024u;

// Largest single feature the packs contain, measured: 36,259 bytes in
// Springfield z13, 19,253 in Oregon z7. The buffer is claimed at this size
// up front so a dense tile never has to find it on a fragmented heap.
// Oregon z7's largest feature is 19,253 encoded bytes; Springfield z13's is
// 36,259, but that one's DECODED geometry (~72 KiB) cannot fit on this board
// regardless, so reserving for it only starves the tiles that can. 24 KiB
// covers everything this board can actually draw.
constexpr size_t kMaxFeatureBytes = 24u * 1024u;

constexpr int kSweepScreens = 5;

// ---------------------------------------------------------------------------
// Board bring-up
// ---------------------------------------------------------------------------

class Cyd35Display : public lgfx::LGFX_Device {
 public:
  Cyd35Display() {
    {
      auto config = bus_.config();
      config.spi_host = SPI2_HOST;  // HSPI
      config.spi_mode = 0;
      config.freq_write = 24000000;
      config.freq_read = 16000000;
      config.spi_3wire = false;
      config.use_lock = true;
      // The classic ESP32 has exactly TWO SPI DMA channels, and this board
      // needs both: one for the display bus, one for the SD bus. Requesting
      // SPI_DMA_CH_AUTO here consumed the allocator's choice and left the
      // SD mount failing with ESP_ERR_NOT_FOUND ("no available dma
      // channel"), so the channels are assigned explicitly instead.
      config.dma_channel = kDisplayDmaChannel;
      config.pin_sclk = kTftSclk;
      config.pin_mosi = kTftMosi;
      config.pin_miso = kTftMiso;
      config.pin_dc = kTftDc;
      bus_.config(config);
      panel_.setBus(&bus_);
    }
    {
      auto config = panel_.config();
      config.pin_cs = kTftCs;
      config.pin_rst = kTftRst;
      config.pin_busy = -1;
      config.panel_width = kPanelW;
      config.panel_height = kPanelH;
      config.offset_x = 0;
      config.offset_y = 0;
      config.offset_rotation = 0;
      config.readable = true;
      config.invert = false;
      // BGR panel: NEONDRIVE sets TFT_RGB_ORDER to TFT_BGR, which is
      // LovyanGFX's rgb_order = false.
      config.rgb_order = false;
      config.dlen_16bit = false;
      // The XPT2046 touch controller shares this SPI bus (unused here, but
      // the panel must still release the bus between transactions).
      config.bus_shared = true;
      panel_.config(config);
    }
    {
      auto config = light_.config();
      config.pin_bl = kBacklight0;
      config.invert = false;
      config.freq = 22000;
      config.pwm_channel = 7;
      light_.config(config);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }

  // Runtime SPI write-clock change. Bus_SPI::setClock only invalidates the
  // cached divider, so the next transaction programs a new SPI_CLOCK_REG and
  // no bus re-initialisation is needed.
  void SetWriteClock(uint32_t hz) { bus_.setClock(hz); }
  uint32_t WriteClock() const { return bus_.getClock(); }

 private:
  lgfx::Bus_SPI bus_;
  lgfx::Panel_ST7796 panel_;
  lgfx::Light_PWM light_;
};

Cyd35Display g_display;
sdmmc_card_t* g_card = nullptr;

// ---------------------------------------------------------------------------
// Touch (XPT2046)
// ---------------------------------------------------------------------------
//
// The resistive controller shares the panel's SPI bus (same SCLK/MOSI/MISO)
// and has its own chip select, which is why the panel config above sets
// bus_shared. It is polled between frames, never during one: a touch read
// takes the bus, and a frame holds it for tens of milliseconds.
//
// Chip select and axis mapping are verified on the board (CYD 3.5",
// ESP32-D0WD-V3) with -DORCMAP_CYD_TOUCH_DEBUG=1: CS 33 answers and a
// corner-to-corner drag reported raw x 790..3578, raw y 502..3673. Those
// observed extremes are narrower than the kTouchRawMin/Max bounds below, so
// the outer few percent of the panel is a dead margin and the drag gain runs a
// few percent low; a drag absorbs that, and the map still reaches every edge.
// Re-run the debug build and re-derive these if a replacement board feels
// offset or mirrored.
constexpr int kTouchCs = 33;
// Polled, not interrupt-driven: the XPT2046 IRQ line has no guaranteed
// pull-up on this board family, and a floating IRQ pin reads as "always
// pressed" or "never pressed" depending on the revision. The controller
// already filters inconsistent samples, so polling costs one ~230 us SPI
// read per idle tick and needs no extra wiring assumption.
constexpr int kTouchIrq = -1;
constexpr int kTouchRawMin = 300;
constexpr int kTouchRawMax = 3900;
// Measured on the board via ORCMAP_CYD_TOUCH_DEBUG. The controller's X axis
// runs down the screen and its Y axis runs right-to-left, so the axes are
// swapped and the resulting screen X is inverted. Before this was corrected a
// top-left-to-bottom-right drag reported screen (448,290) at the end, i.e. the
// finger's horizontal motion moved the reported point leftwards.
constexpr bool kTouchSwapAxes = true;
constexpr bool kTouchInvertX = true;
constexpr bool kTouchInvertY = false;

class TouchPad {
 public:
  bool Init() {
    auto cfg = touch_.config();
    cfg.spi_host = SPI2_HOST;  // the panel's bus, shared by design
    cfg.freq = 2000000;
    cfg.bus_shared = true;
    cfg.pin_sclk = kTftSclk;
    cfg.pin_mosi = kTftMosi;
    cfg.pin_miso = kTftMiso;
    cfg.pin_cs = kTouchCs;
    cfg.pin_int = kTouchIrq;
    cfg.offset_rotation = kDisplayRotation;
    cfg.x_min = kTouchRawMin;
    cfg.x_max = kTouchRawMax;
    cfg.y_min = kTouchRawMin;
    cfg.y_max = kTouchRawMax;
    touch_.config(cfg);
    ready_ = touch_.init();
    return ready_;
  }

  // True while a finger is down; writes screen pixels into *x / *y.
  bool Read(int* x, int* y) {
    if (!ready_ || touch_.getTouchRaw(&tp_, 1) == 0) return false;
    // getTouchRaw reports the ADC, not pixels: the scaling and the axis swap
    // that LGFXBase::getTouch would do are applied here so the mapping is
    // visible and adjustable in one place.
    int ax = tp_.x;
    int ay = tp_.y;
    if (kTouchSwapAxes) {
      const int t = ax;
      ax = ay;
      ay = t;
    }
    const int sx = MapAxis(ax, kDisplayW, kTouchInvertX);
    const int sy = MapAxis(ay, kDisplayH, kTouchInvertY);
    *x = sx;
    *y = sy;
#ifdef ORCMAP_CYD_TOUCH_DEBUG
    ESP_LOGI("orcmap_touch", "raw %d,%d -> screen %d,%d", tp_.x, tp_.y, sx, sy);
#endif
    return true;
  }

 private:
  static int MapAxis(int raw, int extent, bool invert) {
    if (extent <= 1 || kTouchRawMax <= kTouchRawMin) return 0;
    int64_t scaled = static_cast<int64_t>(raw - kTouchRawMin) * (extent - 1);
    scaled /= (kTouchRawMax - kTouchRawMin);
    int v = static_cast<int>(scaled);
    if (v < 0) v = 0;
    if (v > extent - 1) v = extent - 1;
    return invert ? (extent - 1 - v) : v;
  }

  lgfx::Touch_XPT2046 touch_;
  lgfx::touch_point_t tp_{};
  bool ready_ = false;
};

// How long the finger must rest on the panel before the frame under it is
// upgraded from the drag style to the full one.
constexpr int64_t kSettleUs = 300000;


int64_t NowUs() { return esp_timer_get_time(); }

// MALLOC_CAP_INTERNAL alone is MISLEADING on this chip: it counts the 69 KiB
// instruction-RAM region, which malloc cannot hand out for data. Reporting
// it made the largest usable block look like 69,632 bytes when the real
// figure for a byte buffer is smaller. Every probe here therefore asks for
// 8-bit-addressable internal memory -- what a std::vector can actually get.
constexpr uint32_t kDataCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
size_t InternalFree() { return heap_caps_get_free_size(kDataCaps); }
size_t InternalMin() { return heap_caps_get_minimum_free_size(kDataCaps); }
size_t InternalLargest() {
  return heap_caps_get_largest_free_block(kDataCaps);
}

// Serial is authoritative; the SD copy is best-effort and never blocks.
FILE* g_report = nullptr;

void EmitLine(const char* line, void* /*ctx*/) {
  ESP_LOGI(kTag, "%s", line);
  if (g_report != nullptr) {
    std::fprintf(g_report, "%s\n", line);
    std::fflush(g_report);
  }
}

orcmap_bench::BenchHooks MakeHooks() {
  orcmap_bench::BenchHooks hooks;
  hooks.now_us = &NowUs;
  hooks.internal_free = &InternalFree;
  hooks.internal_min = &InternalMin;
  hooks.internal_largest = &InternalLargest;
  // No PSRAM on this board: these probes stay null so the schema emits JSON
  // null rather than a misleading zero.
  hooks.psram_free = nullptr;
  hooks.psram_min = nullptr;
  hooks.psram_largest = nullptr;
  hooks.emit_line = &EmitLine;
  return hooks;
}

// One streaming scratch for the whole run: ~43 KiB of inflate window,
// inflater state and input chunk, plus the per-tile tables and one feature's
// bytes. Reused for every tile, so a steady-state frame makes no large
// allocation -- and no allocation here exceeds ~36 KiB, which is what lets a
// fragmented heap with a 69 KiB largest block render a 111 KiB tile.
orcmap::MvtStreamScratch g_stream_scratch;

orcmap_bench::BenchPipelineOptions MakePipelineOptions() {
  orcmap_bench::BenchPipelineOptions options;
  options.scratch_stream = &g_stream_scratch;
  options.include_layer = &orcmap::experimental::IncludeNoTextBasemapLayer;
  options.decompress_budget = kDecompressBudget;
  options.classify_feature = &orcmap::experimental::TryClassifyFeature;
  options.min_free_internal_bytes = kHeapFloor;
  return options;
}

// ---------------------------------------------------------------------------
// Plain-language status, since this board has no room for a UI
// ---------------------------------------------------------------------------

void StatusLine(int y, uint16_t color, const char* text) {
  g_display.setTextDatum(top_left);
  g_display.setTextSize(1);
  g_display.setTextColor(color, TFT_BLACK);
  g_display.fillRect(0, y, kDisplayW, 12, TFT_BLACK);
  g_display.drawString(text, 6, y);
}

[[noreturn]] void Fail(const char* headline, const char* detail) {
  ESP_LOGE(kTag, "FAILED %s: %s", headline, detail != nullptr ? detail : "");
  g_display.fillScreen(TFT_BLACK);
  g_display.setTextDatum(top_left);
  g_display.setTextSize(2);
  g_display.setTextColor(TFT_ORANGE, TFT_BLACK);
  g_display.drawString("OrcMaps", 8, 20);
  g_display.setTextSize(1);
  g_display.setTextColor(TFT_WHITE, TFT_BLACK);
  g_display.drawString(headline, 8, 60);
  if (detail != nullptr) {
    g_display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    g_display.drawString(detail, 8, 80);
  }
  g_display.drawString("Details on USB serial.", 8, 110);
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

bool MountSd() {
  spi_bus_config_t bus{};
  bus.mosi_io_num = kSdMosi;
  bus.miso_io_num = kSdMiso;
  bus.sclk_io_num = kSdSclk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 4000;
  const esp_err_t bus_err =
      spi_bus_initialize(SPI3_HOST, &bus,
                         static_cast<spi_dma_chan_t>(kSdDmaChannel));
  if (bus_err != ESP_OK) {
    ESP_LOGE(kTag, "SD SPI bus init failed: %s", esp_err_to_name(bus_err));
    return false;
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;

  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs = static_cast<gpio_num_t>(kSdCs);
  slot.host_id = SPI3_HOST;

  esp_vfs_fat_sdmmc_mount_config_t mount{};
  mount.format_if_mount_failed = false;
  mount.max_files = 6;
  mount.allocation_unit_size = 16 * 1024;

  const esp_err_t err =
      esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &mount, &g_card);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "SD SPI mount failed: %s", esp_err_to_name(err));
    return false;
  }
  sdmmc_card_print_info(stdout, g_card);
  return true;
}

void OpenNumberedReport() {
  // Distinct prefix from the Tab5's: each board has its own card, and a
  // report name should say which device wrote it.
  for (int i = 1; i <= 9999; ++i) {
    char path[96];
    std::snprintf(path, sizeof(path), "%s/orcmaps-cyd35-%04d.jsonl", kReportDir,
                  i);
    FILE* probe = std::fopen(path, "rb");
    if (probe != nullptr) {
      std::fclose(probe);
      continue;
    }
    g_report = std::fopen(path, "wb");
    if (g_report != nullptr) ESP_LOGI(kTag, "report file %s", path);
    return;
  }
  ESP_LOGW(kTag, "no free report slot; serial only");
}

// ---------------------------------------------------------------------------
// Map state. One reader per discovered pack; no canvas, no compiled-in packs.
// ---------------------------------------------------------------------------

struct MapSource {
  orcmap::esp_idf::FileByteSource* bytes = nullptr;
  orcmap::PmTilesReader* reader = nullptr;
  const orcmap::PackManifest* manifest = nullptr;
};

std::vector<MapSource> g_sources;
MapSource g_active;
orcmap::PackCatalog g_catalog;
orcmap::Viewport g_viewport;
const orcmap::MapStyle* g_style = nullptr;
orcmap_bench::BenchFrame g_last_frame;
bool g_no_coverage = false;
uint8_t g_zoom_floor = 0;

MapSource* SourceForManifest(const orcmap::PackManifest* manifest) {
  if (manifest == nullptr) return nullptr;
  for (MapSource& source : g_sources) {
    if (source.manifest != nullptr &&
        source.manifest->pack_id == manifest->pack_id) {
      return &source;
    }
  }
  return nullptr;
}

MapSource* WidestSource() {
  MapSource* best = nullptr;
  double best_span = -1.0;
  for (MapSource& source : g_sources) {
    if (source.manifest == nullptr) continue;
    const double span = source.manifest->bounds.max_lon_deg -
                        source.manifest->bounds.min_lon_deg;
    if (span > best_span) {
      best_span = span;
      best = &source;
    }
  }
  return best;
}

MapSource* DeepestSource() {
  MapSource* best = nullptr;
  for (MapSource& source : g_sources) {
    if (source.manifest == nullptr) continue;
    if (best == nullptr ||
        source.manifest->max_zoom > best->manifest->max_zoom) {
      best = &source;
    }
  }
  return best;
}

bool BoundsOverlap(const orcmap::GeoBounds& a, const orcmap::GeoBounds& b) {
  return a.min_lon_deg <= b.max_lon_deg && a.max_lon_deg >= b.min_lon_deg &&
         a.min_lat_deg <= b.max_lat_deg && a.max_lat_deg >= b.min_lat_deg;
}

// Same two-tier rule as the Tab5: ResolvePack() decides automatic selection
// with strict full coverage, and a merely overlapping pack is still drawn
// rather than refused.
void ResolveActiveSource() {
  g_no_coverage = false;
  orcmap::GeoBounds visible{};
  if (!orcmap::GetVisibleBounds(g_viewport, &visible)) {
    g_no_coverage = true;
    return;
  }
  const uint8_t zoom = orcmap::GetZoom(g_viewport);
  if (MapSource* covered =
          SourceForManifest(orcmap::ResolvePack(g_catalog, visible, zoom))) {
    g_active = *covered;
    return;
  }
  const orcmap::PackManifest* best = nullptr;
  for (const orcmap::PackManifest& m : g_catalog.Packs()) {
    if (zoom < m.min_zoom || zoom > m.max_zoom) continue;
    if (!BoundsOverlap(m.bounds, visible)) continue;
    if (best == nullptr || m.priority > best->priority) best = &m;
  }
  MapSource* partial = SourceForManifest(best);
  if (partial == nullptr) {
    g_no_coverage = true;
    return;
  }
  g_active = *partial;
}

// The whole-world view for THIS display, derived by the engine. 320x480 at
// 256 px tiles resolves to z1 (a 512 px world covers 480 px of height),
// where the Tab5's 1280x600 resolves to z2 -- the same pack, a different
// answer, with no per-board constant.
void RecomputeZoomFloor() {
  orcmap::Viewport probe = g_viewport;
  probe.width_px = kDisplayW;
  probe.height_px = kDisplayH;
  bool have = false;
  uint8_t floor_zoom = 0;
  for (const orcmap::PackManifest& m : g_catalog.Packs()) {
    uint8_t fill = 0;
    if (!orcmap::MinFillZoom(probe, m.bounds, &fill)) continue;
    if (fill > m.max_zoom) continue;
    if (fill < m.min_zoom) fill = m.min_zoom;
    if (!have || fill < floor_zoom) {
      floor_zoom = fill;
      have = true;
    }
  }
  g_zoom_floor = have ? floor_zoom : 0;
}

bool FocusPack(const orcmap::PackManifest& manifest) {
  if (manifest.bounds.max_lon_deg - manifest.bounds.min_lon_deg >= 360.0) {
    uint8_t zoom = 0;
    orcmap::Viewport probe = g_viewport;
    probe.width_px = kDisplayW;
    probe.height_px = kDisplayH;
    if (orcmap::WorldViewZoom(probe, &zoom)) {
      if (zoom < manifest.min_zoom) zoom = manifest.min_zoom;
      if (zoom > manifest.max_zoom) zoom = manifest.max_zoom;
      if (orcmap::SetZoom(&g_viewport, zoom)) {
        return orcmap::SetCenter(&g_viewport, 0.0, 0.0);
      }
    }
  }
  if (orcmap::FillBounds(&g_viewport, manifest.bounds)) return true;
  return orcmap::FitBounds(&g_viewport, manifest.bounds, 8);
}

// One measured frame, drawn straight to the panel. There is no offscreen
// canvas on this board, so render_ms includes SPI transfer to the display --
// stated plainly rather than hidden, because it is the real cost here.
void DrawNoCoverage() {
  g_display.fillScreen(TFT_BLACK);
  g_display.setTextDatum(middle_center);
  g_display.setTextSize(1);
  g_display.setTextColor(TFT_ORANGE, TFT_BLACK);
  g_display.drawString("NO COVERAGE AT THIS VIEW", kDisplayW / 2,
                       kDisplayH / 2);
}

// One measured frame with an explicit style and layer policy. RenderFrame()
// below is this with the production style; a drag uses the same function with
// a reduced one.
void RenderFrameWith(const orcmap::MapStyle& style,
                     const orcmap_bench::BenchPipelineOptions& options) {
  ResolveActiveSource();
  if (g_no_coverage || g_active.reader == nullptr) {
    DrawNoCoverage();
    // No pipeline ran, so there is no measurement to report.
    g_last_frame = orcmap_bench::BenchFrame{};
    return;
  }
  orcmap::m5gfx_adapter::DisplayTarget target(g_display);
  orcmap::Viewport map_vp = g_viewport;
  map_vp.width_px = kDisplayW;
  map_vp.height_px = kDisplayH;

  // Drawing goes straight to the panel: there is no offscreen framebuffer on
  // this board -- 480x320 is 307,200 bytes at RGB565, 153,600 at 8bpp and
  // 76,800 even at 4bpp, against 138,628 free once the streaming scratch is
  // accounted for.
  //
  // Holding one SPI transaction open across the frame (startWrite/endWrite)
  // was tried and MEASURED SLOWER: render went 616.8 -> 735.8 ms on the
  // world view, 615.5 -> 722.5 on Springfield and 925.5 -> 981.0 on Oregon,
  // on identical work. LovyanGFX evidently batches better when each
  // primitive manages its own transaction than when one is held open across
  // thousands of small writes. Reverted, and recorded so it is not
  // "optimised" again on intuition.
  orcmap_bench::RenderMeasuredFrame(*g_active.reader, map_vp, style, &target,
                                    options, MakeHooks(),
                                    /*background=*/true, &g_last_frame);
}

void RenderFrame() { RenderFrameWith(*g_style, MakePipelineOptions()); }

// ---------------------------------------------------------------------------
// Interactive drag (ORCMAP_CYD_INTERACTIVE)
// ---------------------------------------------------------------------------

#ifdef ORCMAP_CYD_INTERACTIVE

// The style used while a finger is moving. It withholds the categories that
// cost the most per screen -- minor and secondary roads, rail, buildings,
// parks, boundaries -- and keeps the background, land, water and the
// motorway/primary network, which is what makes a moving map still readable.
// The full OrcSdrDark frame is drawn as soon as the finger stops.
const orcmap::MapStyle& DragStyle() {
  static const orcmap::MapStyle style = [] {
    orcmap::MapStyle drag = orcmap::styles::OrcSdrDark();
    using K = orcmap::FeatureKind;
    const K hidden[] = {K::kMinorRoad,     K::kSecondaryRoad, K::kRail,
                        K::kBuilding,      K::kPark,          K::kBoundary,
                        K::kLabelPrimary,  K::kLabelSecondary, K::kLabelMuted};
    for (K kind : hidden) {
      drag.rules[static_cast<size_t>(kind)].visible = false;
    }
    drag.id = "orcsdr-dark-drag";
    drag.display_name = "OrcSDR Dark (drag)";
    return drag;
  }();
  return style;
}

// Decode-time companion to DragStyle(): the production layer filter plus the
// building layer, which produces only a category the drag style hides.
bool IncludeDragLayer(const char* name, size_t name_len, void* ctx) {
  if (!orcmap::experimental::IncludeNoTextBasemapLayer(name, name_len, ctx)) {
    return false;
  }
  return !(name_len == 8 && std::memcmp(name, "building", 8) == 0);
}

orcmap_bench::BenchPipelineOptions MakeDragPipelineOptions() {
  orcmap_bench::BenchPipelineOptions options = MakePipelineOptions();
  options.include_layer = &IncludeDragLayer;
  return options;
}

void EmitInteractiveFrame(const char* phase) {
  if (g_last_frame.tiles_visible == 0) return;
  orcmap::Viewport map_vp = g_viewport;
  map_vp.width_px = kDisplayW;
  map_vp.height_px = kDisplayH;
  orcmap_bench::EmitFrameRecord(
      MakeHooks(), "interactive", phase,
      g_active.manifest != nullptr ? g_active.manifest->pack_id.c_str() : "none",
      g_style->id, map_vp, g_last_frame);
}

// One finger drags the map. There is no offscreen canvas on this board and
// every frame goes straight to the panel, so the loop is sample -> pan ->
// draw -> sample: the drag rate IS the frame rate, which is why the moving
// frame uses the reduced style and the settled frame uses the real one.
void RunInteractive(TouchPad& touch) {
  int last_x = 0;
  int last_y = 0;
  bool was_down = false;
  bool reduced_on_screen = false;
  int64_t last_move_us = 0;

  for (;;) {
    int x = 0;
    int y = 0;
    const bool down = touch.Read(&x, &y);
    const int64_t now = NowUs();

    if (down && !was_down) {
      last_x = x;
      last_y = y;
    } else if (down) {
      const int dx = x - last_x;
      const int dy = y - last_y;
      if (dx != 0 || dy != 0) {
        // Direct manipulation: the finger moves the map, so the camera moves
        // the other way (docs/MAP_CONTROLS.md).
        orcmap::PanByPixels(&g_viewport, -dx, -dy);
        last_x = x;
        last_y = y;
        last_move_us = now;
        RenderFrameWith(DragStyle(), MakeDragPipelineOptions());
        reduced_on_screen = true;
      }
    } else if (was_down && reduced_on_screen) {
      // Finger lifted: draw the frame the user actually looks at.
      RenderFrame();
      EmitInteractiveFrame("released");
      reduced_on_screen = false;
    }

    // Resting finger: upgrade in place without waiting for the lift.
    if (down && reduced_on_screen && now - last_move_us > kSettleUs) {
      RenderFrame();
      EmitInteractiveFrame("settled");
      reduced_on_screen = false;
    }

    was_down = down;
    vTaskDelay(pdMS_TO_TICKS(down ? 1 : 10));
  }
}

// No packs on the card (or no card) but the touch wiring still needs
// confirming. This is the only way to verify kTouchCs and the axis mapping
// without a map, so it runs instead of failing hard: every touch draws a dot
// where the code thinks the finger is, and ORCMAP_CYD_TOUCH_DEBUG prints the
// raw and mapped values on serial.
void RunTouchTest(TouchPad& touch) {
  g_display.fillScreen(TFT_BLACK);
  g_display.setTextDatum(top_left);
  g_display.setTextSize(1);
  g_display.setTextColor(TFT_WHITE, TFT_BLACK);
  g_display.drawString("TOUCH TEST - no map packs found", 6, 6);
  g_display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g_display.drawString("Drag a finger; dots appear under it.", 6, 20);
  g_display.drawString("Serial logs raw -> screen.", 6, 32);

  for (;;) {
    int x = 0;
    int y = 0;
    if (touch.Read(&x, &y)) {
      g_display.fillCircle(x, y, 2, TFT_GREEN);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

#endif  // ORCMAP_CYD_INTERACTIVE

// ---------------------------------------------------------------------------
// FPS probe (ORCMAP_CYD_FPS_PROBE)
// ---------------------------------------------------------------------------
//
// Answers one question with numbers: how far from 30 fps is this board, and is
// the limit the CPU or the bus?
//
// Every frame here is kDisplayW x kDisplayH RGB565 = 307,200 bytes, and
// Panel_ST7796 only speaks rgb565_2Byte or rgb888_3Byte, so bytes per frame is
// fixed by the hardware. The achievable rate is then exactly
//
//     fps = SPI write throughput / 307,200
//
// which makes 30 fps require 9.22 MB/s, or 73.7 Mbit/s, of sustained pixels
// out of an 80 MHz maximum SPI clock. That is the whole budget, so the probe
// measures the real throughput at each candidate clock, the CPU cost of a
// frame-shaped workload against a counting target that does no I/O, and
// whether the panel read back what was written at that clock.
#ifdef ORCMAP_CYD_FPS_PROBE

constexpr size_t kFrameWireBytes =
    static_cast<size_t>(kDisplayW) * static_cast<size_t>(kDisplayH) * 2;

// Counts the primitives the panel would receive without touching the bus, so
// the CPU half of a frame can be timed on its own.
class CountingTarget : public orcmap::RenderTarget {
 public:
  int Width() const override { return kDisplayW; }
  int Height() const override { return kDisplayH; }

  void FillRect(int, int, int w, int h, orcmap::Color) override {
    ++rects;
    if (w > 0 && h > 0) span_px += static_cast<uint64_t>(w) * h;
  }
  void DrawPoint(int, int, orcmap::Color) override { ++points; }
  void DrawLine(int x0, int y0, int x1, int y1, orcmap::Color,
                float) override {
    ++lines;
    const int dx = x0 > x1 ? x0 - x1 : x1 - x0;
    const int dy = y0 > y1 ? y0 - y1 : y1 - y0;
    span_px += static_cast<uint64_t>((dx > dy ? dx : dy) + 1);
  }
  void FillPolygon(const int* xy, size_t n, orcmap::Color c) override {
    FillPolygonRings(xy, &n, 1, c);
  }
  void FillPolygonRings(const int* xy, const size_t* counts, size_t rings,
                        orcmap::Color) override {
    ++polygons;
    size_t total = 0;
    for (size_t r = 0; r < rings; ++r) total += counts[r];
    if (total < 3) return;
    int minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1];
    for (size_t i = 1; i < total; ++i) {
      const int px = xy[i * 2];
      const int py = xy[i * 2 + 1];
      if (px < minx) minx = px;
      if (px > maxx) maxx = px;
      if (py < miny) miny = py;
      if (py > maxy) maxy = py;
    }
    // Bounding box, an upper bound on the filled area.
    span_px += static_cast<uint64_t>(maxx - minx + 1) *
               static_cast<uint64_t>(maxy - miny + 1);
  }

  uint32_t rects = 0;
  uint32_t points = 0;
  uint32_t lines = 0;
  uint32_t polygons = 0;
  uint64_t span_px = 0;
};

uint32_t NextRand(uint32_t* state) {
  *state = (*state * 1664525u) + 1013904223u;
  return *state >> 8;
}

// A frame-shaped workload rather than real tiles: the CPU half has to be
// measurable with no card inserted, and its cost depends on how many vertices
// and spans the renderer walks, not on which pack they came from. The counts
// default to the busy tile measured in docs/evidence/CYD35_BENCHMARK.md
// (202 features), so the number here is comparable to the device figures.
orcmap::FeatureTile BuildProbeTile(int polygon_count, int road_count) {
  // cos/sin of 30-degree steps, x1000: a 12-gon without pulling in libm.
  static const int16_t kUnit[12][2] = {
      {1000, 0},   {866, 500},   {500, 866},   {0, 1000},  {-500, 866},
      {-866, 500}, {-1000, 0},   {-866, -500}, {-500, -866}, {0, -1000},
      {500, -866}, {866, -500}};
  // Categories the default style actually fills and strokes at street zoom;
  // an unassigned Feature is skipped outright by RenderOneFeature.
  static const orcmap::FeatureKind kAreaKinds[3] = {
      orcmap::FeatureKind::kWater, orcmap::FeatureKind::kPark,
      orcmap::FeatureKind::kLand};
  static const orcmap::FeatureKind kLineKinds[4] = {
      orcmap::FeatureKind::kMotorway, orcmap::FeatureKind::kPrimaryRoad,
      orcmap::FeatureKind::kSecondaryRoad, orcmap::FeatureKind::kMinorRoad};
  uint32_t seed = 0x5EED1234u;

  orcmap::FeatureTile tile;
  tile.features.reserve(static_cast<size_t>(polygon_count + road_count));

  for (int f = 0; f < polygon_count; ++f) {
    orcmap::Feature feature;
    feature.geometry.type = orcmap::GeomType::kPolygon;
    feature.extent = 4096;
    feature.kind = kAreaKinds[f % 3];
    feature.kind_assigned = true;
    const int cx = static_cast<int>(NextRand(&seed) % 4096u);
    const int cy = static_cast<int>(NextRand(&seed) % 4096u);
    const int rx = 80 + static_cast<int>(NextRand(&seed) % 500u);
    const int ry = 80 + static_cast<int>(NextRand(&seed) % 500u);
    orcmap::Path ring;
    ring.reserve(12);
    for (int i = 0; i < 12; ++i) {
      orcmap::Point p;
      p.x = cx + (kUnit[i][0] * rx) / 1000;
      p.y = cy + (kUnit[i][1] * ry) / 1000;
      ring.push_back(p);
    }
    feature.geometry.paths.push_back(std::move(ring));
    tile.features.push_back(std::move(feature));
  }

  for (int f = 0; f < road_count; ++f) {
    orcmap::Feature feature;
    feature.geometry.type = orcmap::GeomType::kLineString;
    feature.extent = 4096;
    feature.kind = kLineKinds[f % 4];
    feature.kind_assigned = true;
    orcmap::Path line;
    line.reserve(10);
    int x = static_cast<int>(NextRand(&seed) % 4096u);
    int y = static_cast<int>(NextRand(&seed) % 4096u);
    for (int i = 0; i < 10; ++i) {
      orcmap::Point p;
      p.x = x;
      p.y = y;
      line.push_back(p);
      x += static_cast<int>(NextRand(&seed) % 401u) - 200;
      y += static_cast<int>(NextRand(&seed) % 401u) - 200;
    }
    feature.geometry.paths.push_back(std::move(line));
    tile.features.push_back(std::move(feature));
  }
  return tile;
}

// Writes into one horizontal band of RAM instead of the panel. Every primitive
// is clipped to the band, so the same pre-projected frame can be painted once
// per band and the panel then receives one bulk pushImage per band -- the
// difference between an SPI transaction per pixel and one per band.
class BandTarget : public orcmap::RenderTarget {
 public:
  BandTarget(int band_top, int band_rows, uint16_t* pixels)
      : top_(band_top), rows_(band_rows), px_(pixels) {}

  int Width() const override { return kDisplayW; }
  int Height() const override { return kDisplayH; }

  void Clear(uint16_t color) {
    // 32 bits per store: clearing is 307,200 bytes a frame, which was ~8 ms of
    // the banded frame when it was a 16-bit loop.
    const uint32_t pair = (static_cast<uint32_t>(color) << 16) | color;
    uint32_t* p = reinterpret_cast<uint32_t*>(px_);
    const size_t n = (static_cast<size_t>(kDisplayW) * rows_) / 2;
    for (size_t i = 0; i < n; ++i) p[i] = pair;
  }

  void FillRect(int x, int y, int w, int h, orcmap::Color color) override {
    if (w <= 0 || h <= 0) return;
    int64_t x0 = x < 0 ? 0 : x;
    int64_t y0 = y < top_ ? top_ : y;
    int64_t x1 = static_cast<int64_t>(x) + w;
    int64_t y1 = static_cast<int64_t>(y) + h;
    if (x1 > kDisplayW) x1 = kDisplayW;
    if (y1 > top_ + rows_) y1 = top_ + rows_;
    if (x0 >= x1 || y0 >= y1) return;
    const uint16_t v = orcmap::m5gfx_adapter::ToRgb565(color);
    for (int64_t yy = y0; yy < y1; ++yy) {
      uint16_t* row = px_ + static_cast<size_t>(yy - top_) * kDisplayW;
      for (int64_t xx = x0; xx < x1; ++xx) row[xx] = v;
    }
  }

  void DrawPoint(int x, int y, orcmap::Color color) override {
    if (x < 0 || x >= kDisplayW || y < top_ || y >= top_ + rows_) return;
    px_[static_cast<size_t>(y - top_) * kDisplayW + x] =
        orcmap::m5gfx_adapter::ToRgb565(color);
  }

  // One Bresenham walk for every width, stamping a w x w brush per centreline
  // pixel. That is exactly the pixel set the shared run-coalescing stroke
  // produces (the union of brushes on consecutive pixels is the coalesced
  // rect), but without a FillRect call per run -- measured as the largest
  // single cost in the banded frame, because three quarters of the segments in
  // a street-level view are wider than one pixel.
  void DrawLine(int x0, int y0, int x1, int y1, orcmap::Color color,
                float width_px) override {
    const int w = orcmap::RasterStrokeWidthPx(width_px);
    if (w <= 0) return;
    const int r = w / 2;
    const int xmin = -r;
    const int ymin = top_ - r;
    const int xmax = kDisplayW - 1 + r;
    const int ymax = top_ + rows_ - 1 + r;
    // Interior segments are the common case and the exact-rational clipper is
    // expensive, so it only runs when a segment leaves the band.
    const bool inside = x0 >= xmin && x1 >= xmin && x0 <= xmax && x1 <= xmax &&
                        y0 >= ymin && y1 >= ymin && y0 <= ymax && y1 <= ymax;
    if (!inside &&
        !orcmap::ClipLineToPixelRect(&x0, &y0, &x1, &y1, xmin, ymin, xmax,
                                     ymax)) {
      return;
    }
    const uint16_t v = orcmap::m5gfx_adapter::ToRgb565(color);
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int sx = x0 < x1 ? 1 : -1;
    const int ady = y1 > y0 ? y1 - y0 : y0 - y1;
    const int dy = -ady;  // negative magnitude, canonical Bresenham
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      Stamp(x0 - r, y0 - r, w, v);
      if (x0 == x1 && y0 == y1) break;
      const int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  void FillPolygon(const int* xy_pairs, size_t n_points,
                   orcmap::Color color) override {
    FillPolygonRings(xy_pairs, &n_points, 1, color);
  }

  // Even-odd scanline over every ring, clipped to the band's rows.
  void FillPolygonRings(const int* xy_pairs, const size_t* ring_point_counts,
                        size_t ring_count, orcmap::Color color) override {
    if (xy_pairs == nullptr || ring_point_counts == nullptr || ring_count == 0) {
      return;
    }
    size_t n_points = 0;
    for (size_t r = 0; r < ring_count; ++r) n_points += ring_point_counts[r];
    if (n_points < 3) return;

    int min_y = xy_pairs[1];
    int max_y = xy_pairs[1];
    int min_x = xy_pairs[0];
    int max_x = xy_pairs[0];
    for (size_t i = 0; i < n_points; ++i) {
      const int px = xy_pairs[i * 2];
      const int py = xy_pairs[i * 2 + 1];
      if (py < min_y) min_y = py;
      if (py > max_y) max_y = py;
      if (px < min_x) min_x = px;
      if (px > max_x) max_x = px;
    }
    if (min_y < top_) min_y = top_;
    if (max_y > top_ + rows_ - 1) max_y = top_ + rows_ - 1;
    if (max_y < min_y || max_x < 0 || min_x >= kDisplayW) return;

    const uint16_t v = orcmap::m5gfx_adapter::ToRgb565(color);
    // One 16.16 division per EDGE instead of one integer division per
    // (edge, row): the crossing x is linear in y, so it is advanced with a
    // fixed-point step. The per-row division was the single largest cost in
    // the banded frame.
    edges_.clear();
    {
      size_t base = 0;
      for (size_t r = 0; r < ring_count; ++r) {
        const size_t ring_n = ring_point_counts[r];
        if (ring_n >= 3) {
          for (size_t i = 0; i < ring_n; ++i) {
            const size_t j = (i + 1) % ring_n;
            const int ya = xy_pairs[(base + i) * 2 + 1];
            const int yb = xy_pairs[(base + j) * 2 + 1];
            if (ya == yb) continue;
            const int xa = xy_pairs[(base + i) * 2];
            const int xb = xy_pairs[(base + j) * 2];
            const bool up = ya < yb;
            const int lo = up ? ya : yb;
            const int hi = up ? yb : ya;
            if (hi <= top_ || lo >= top_ + rows_) continue;
            const int xlo = up ? xa : xb;
            const int xhi = up ? xb : xa;
            Edge e;
            e.lo = lo;
            e.hi = hi;
            e.x_q16 = (static_cast<int64_t>(xlo) << 16);
            e.step_q16 = (static_cast<int64_t>(xhi - xlo) << 16) / (hi - lo);
            edges_.push_back(e);
          }
        }
        base += ring_n;
      }
    }

    for (int y = min_y; y <= max_y; ++y) {
      crossings_.clear();
      for (const Edge& e : edges_) {
        if (y < e.lo || y >= e.hi) continue;
        const int64_t x = e.x_q16 + static_cast<int64_t>(y - e.lo) * e.step_q16;
        crossings_.push_back(static_cast<int>(x >> 16));
      }
      // Insertion sort: a scanline crosses a handful of edges, and std::sort's
      // call overhead per scanline showed up in the banded frame time.
      for (size_t a = 1; a < crossings_.size(); ++a) {
        const int key = crossings_[a];
        size_t b = a;
        while (b > 0 && crossings_[b - 1] > key) {
          crossings_[b] = crossings_[b - 1];
          --b;
        }
        crossings_[b] = key;
      }
      uint16_t* row = px_ + static_cast<size_t>(y - top_) * kDisplayW;
      for (size_t k = 0; k + 1 < crossings_.size(); k += 2) {
        int xa = crossings_[k];
        int xb = crossings_[k + 1];
        if (xa < 0) xa = 0;
        if (xb >= kDisplayW) xb = kDisplayW - 1;
        for (int xx = xa; xx <= xb; ++xx) row[xx] = v;
      }
    }
  }

 private:
  // One solid w x w block, clipped to the band. Deliberately not FillRect:
  // this runs per centreline pixel and must not pay a full clip per brush.
  void Stamp(int x, int y, int w, uint16_t v) {
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + w;
    if (x0 < 0) x0 = 0;
    if (y0 < top_) y0 = top_;
    if (x1 > kDisplayW) x1 = kDisplayW;
    if (y1 > top_ + rows_) y1 = top_ + rows_;
    if (x0 >= x1 || y0 >= y1) return;
    for (int yy = y0; yy < y1; ++yy) {
      uint16_t* row = px_ + static_cast<size_t>(yy - top_) * kDisplayW;
      for (int xx = x0; xx < x1; ++xx) row[xx] = v;
    }
  }

  struct Edge {
    int lo = 0;
    int hi = 0;
    int64_t x_q16 = 0;
    int64_t step_q16 = 0;
  };

  int top_;
  int rows_;
  uint16_t* px_;
  std::vector<int> crossings_;
  std::vector<Edge> edges_;
};

// The pre-projected frame, reused between frames so the screen-space cache
// allocates once.
std::vector<orcmap::ScreenFeature> g_screen_frame;
size_t g_screen_count = 0;
int64_t g_proj_us = 0;
int64_t g_raster_us = 0;
int64_t g_push_us = 0;

// Paints one whole frame as bands: project once, then per band cull by bounds,
// rasterize into RAM and hand the panel a single pushImage.
int64_t RenderFrameBanded(const orcmap::FeatureTile& tile,
                          const orcmap::TilePlacement& placement,
                          const orcmap::Viewport& vp,
                          const orcmap::MapStyle& style, uint16_t* band,
                          int band_rows) {
  orcmap::TileScreenMap map;
  if (!orcmap::MakeTilePlacementScreenMap(vp, placement, 4096, &map)) return -1;

  const int64_t t0 = NowUs();
  // Never resize down: shrinking destroys the ScreenFeature vectors and frees
  // the buffers this cache exists to reuse.
  if (g_screen_frame.size() < tile.features.size()) {
    g_screen_frame.resize(tile.features.size());
  }
  size_t keep = 0;
  for (const orcmap::Feature& f : tile.features) {
    if (orcmap::ProjectFeatureToScreen(f, map, vp, style,
                                      &g_screen_frame[keep])) {
      ++keep;
    }
  }
  g_screen_count = keep;
  const int64_t t_proj = NowUs();

  const orcmap::MapPaint bg = orcmap::ResolveFeatureStyle(
      orcmap::FeatureKind::kBackground, vp.zoom, style);
  const uint16_t bg565 = orcmap::m5gfx_adapter::ToRgb565(bg.color);

  for (int top = 0; top < kDisplayH; top += band_rows) {
    BandTarget target(top, band_rows, band);
    target.Clear(bg565);
    for (size_t i = 0; i < g_screen_count; ++i) {
      const orcmap::ScreenFeature& sf = g_screen_frame[i];
      if (sf.max_y < top || sf.min_y >= top + band_rows) continue;
      orcmap::DrawScreenFeature(sf, &target);
    }
    g_push_us -= NowUs();
    g_display.pushImage(0, top, kDisplayW, band_rows, band);
    g_push_us += NowUs();
  }
  const int64_t t_end = NowUs();
  g_proj_us = t_proj - t0;
  g_raster_us = (t_end - t_proj) - g_push_us;
  return t_end - t0;
}

// Writes eight solid bands and reads one pixel back from each. A clock the
// panel cannot keep up with shows up here as written != read before it shows
// up as a visibly torn map.
int VerifyBands(uint16_t* first_expected, uint16_t* first_got) {
  static const uint16_t kColors[8] = {0xF800, 0x07E0, 0x001F, 0xFFFF,
                                      0x0000, 0x7BEF, 0xF81F, 0x07FF};
  const int band = kDisplayH / 8;
  g_display.startWrite();
  for (int i = 0; i < 8; ++i) {
    g_display.fillRect(0, i * band, kDisplayW, band, kColors[i]);
  }
  g_display.endWrite();
  int bad = 0;
  for (int i = 0; i < 8; ++i) {
    const uint16_t got = g_display.readPixel(kDisplayW / 2, i * band + band / 2);
    if (got != kColors[i]) {
      if (bad == 0) {
        *first_expected = kColors[i];
        *first_got = got;
      }
      ++bad;
    }
  }
  return bad;
}

void RunFpsProbe() {
  g_viewport.width_px = kDisplayW;
  g_viewport.height_px = kDisplayH;
  orcmap::SetZoom(&g_viewport, 14);
  orcmap::SetCenter(&g_viewport, 44.0462, -123.0220);

  const orcmap::TileId tile =
      orcmap::LatLonToTile(44.0462, -123.0220, g_viewport.zoom);
  orcmap::TilePlacement placement;
  if (!orcmap::NearestTilePlacement(g_viewport, tile, &placement)) {
    Fail("FPS PROBE CANNOT PLACE TILE", "viewport/tile mismatch");
  }

  const orcmap::MapStyle& style = orcmap::styles::OrcSdrDark();

  // How the CPU half scales with what is on screen. The busy row matches the
  // 202-feature tile recorded in docs/evidence/CYD35_BENCHMARK.md.
  struct Density {
    const char* name;
    int polygons;
    int roads;
  };
  static const Density kDensities[] = {
      {"light", 10, 60},
      {"busy", 30, 172},
      {"dense", 80, 500},
  };
  for (const Density& d : kDensities) {
    const orcmap::FeatureTile tile = BuildProbeTile(d.polygons, d.roads);
    CountingTarget counting;
    const int64_t t0 = NowUs();
    {
      orcmap::RenderScratch scratch;
      for (const orcmap::Feature& f : tile.features) {
        orcmap::RenderFeatureAt(f, placement, g_viewport, style, &counting,
                                &scratch);
      }
    }
    const int64_t cpu_us = NowUs() - t0;
    ESP_LOGI(kTag,
             "fps probe cpu %-5s: %u features, %u polys, %u lines, "
             "%llu px span -> %.2f ms (no SPI)",
             d.name, static_cast<unsigned>(tile.features.size()),
             static_cast<unsigned>(counting.polygons),
             static_cast<unsigned>(counting.lines),
             static_cast<unsigned long long>(counting.span_px),
             cpu_us / 1000.0);
  }

  const orcmap::FeatureTile busy = BuildProbeTile(30, 172);

  // 80 MHz is the APB clock, so the reachable dividers are 80, 40, 26.67 and
  // 20 MHz; asking for 60 MHz silently lands on 40.
  const uint32_t clocks[] = {20000000u, 26666667u, 40000000u, 80000000u};
  for (uint32_t hz : clocks) {
    g_display.SetWriteClock(hz);

    int64_t wire_best = 0x7FFFFFFFFFFFFFFFLL;
    for (int i = 0; i < 3; ++i) {
      const int64_t t0 = NowUs();
      g_display.fillScreen(TFT_BLACK);
      const int64_t dt = NowUs() - t0;
      if (dt < wire_best) wire_best = dt;
    }
    const double wire_ms = wire_best / 1000.0;

    uint16_t expected = 0;
    uint16_t got = 0;
    const int bad = VerifyBands(&expected, &got);

    orcmap::m5gfx_adapter::DisplayTarget target(g_display);
    int64_t total_us = 0;
    {
      orcmap::RenderScratch scratch;
      const int64_t t0 = NowUs();
      orcmap::ClearMapBackground(g_viewport, style, &target);
      for (const orcmap::Feature& f : busy.features) {
        orcmap::RenderFeatureAt(f, placement, g_viewport, style, &target,
                                &scratch);
      }
      total_us = NowUs() - t0;
    }
    const double total_ms = total_us / 1000.0;

    ESP_LOGI(kTag,
             "fps probe clock=%uMHz wire=%.2fms (%.2fMB/s ceiling=%.1ffps) "
             "frame=%.2fms (%.1ffps) readback_bad=%d",
             static_cast<unsigned>(hz / 1000000u), wire_ms,
             (kFrameWireBytes / 1048576.0) / (wire_ms / 1000.0),
             1000.0 / wire_ms, total_ms, 1000.0 / total_ms, bad);
    if (bad > 0) {
      ESP_LOGW(kTag, "fps probe readback mismatch at %uMHz: expected 0x%04X got 0x%04X",
               static_cast<unsigned>(hz / 1000000u), expected, got);
    }
  }

  g_display.SetWriteClock(24000000u);

  // Where the non-wire time goes: areas and lines are pushed through very
  // different LovyanGFX paths (drawFastHLine runs vs per-pixel Bresenham), so
  // measuring them apart says which one to attack.
  g_display.SetWriteClock(80000000u);
  const auto panel_ms = [&](int polys, int roads) {
    const orcmap::FeatureTile t = BuildProbeTile(polys, roads);
    orcmap::m5gfx_adapter::DisplayTarget target(g_display);
    orcmap::RenderScratch scratch;
    const int64_t t0 = NowUs();
    orcmap::ClearMapBackground(g_viewport, style, &target);
    for (const orcmap::Feature& f : t.features) {
      orcmap::RenderFeatureAt(f, placement, g_viewport, style, &target,
                              &scratch);
    }
    return (NowUs() - t0) / 1000.0;
  };
  ESP_LOGI(kTag,
           "fps probe split @80MHz: areas_only=%.2f ms, lines_only=%.2f ms, "
           "both=%.2f ms",
           panel_ms(30, 0), panel_ms(0, 172), panel_ms(30, 172));

  // Cost of showing a frame that was rasterized in RAM: N bulk pushes instead
  // of one call per primitive. If this lands on the fillScreen time, banding
  // reaches the bus floor and the only remaining cost is rasterization.
  // Band height: 80 rows is 76,800 B, which leaves the streaming scratch and
  // the feature buffer room on this ~129 KiB board and halves the number of
  // push calls (each costs ~0.36 ms of bus turnaround).
  constexpr int kBandRows = 80;
  constexpr int kBands = kDisplayH / kBandRows;
  auto* band = static_cast<uint16_t*>(
      heap_caps_malloc(static_cast<size_t>(kDisplayW) * kBandRows * 2,
                       kDataCaps));
  if (band != nullptr) {
    for (size_t i = 0; i < static_cast<size_t>(kDisplayW) * kBandRows; ++i) {
      band[i] = 0x18E3;  // a land-ish fill, so the pixels are not all zero
    }
    int64_t best = 0x7FFFFFFFFFFFFFFFLL;
    for (int rep = 0; rep < 3; ++rep) {
      const int64_t t0 = NowUs();
      for (int b = 0; b < kBands; ++b) {
        g_display.pushImage(0, b * kBandRows, kDisplayW, kBandRows, band);
      }
      const int64_t dt = NowUs() - t0;
      if (dt < best) best = dt;
    }
    ESP_LOGI(kTag,
             "fps probe banding @80MHz: %d bands of %d rows, buffer %u B -> "
             "%.2f ms/frame (%.1f fps)",
             kBands, kBandRows,
             static_cast<unsigned>(kDisplayW * kBandRows * 2), best / 1000.0,
             1000000.0 / best);

    // The whole thing: project once, rasterize every band in RAM, push it.
    int64_t banded_best = 0x7FFFFFFFFFFFFFFFLL;
    for (int rep = 0; rep < 3; ++rep) {
      const orcmap::FeatureTile busy_tile = BuildProbeTile(30, 172);
      const int64_t dt = RenderFrameBanded(busy_tile, placement, g_viewport,
                                           style, band, kBandRows);
      if (dt >= 0 && dt < banded_best) banded_best = dt;
    }
    if (banded_best != 0x7FFFFFFFFFFFFFFFLL) {
      ESP_LOGI(kTag,
               "fps probe banded frame @80MHz: %.2f ms/frame (%.1f fps) "
               "[project + rasterize + push]",
               banded_best / 1000.0, 1000000.0 / banded_best);
      ESP_LOGI(kTag,
               "fps probe banded phases: project=%.2f ms rasterize=%.2f ms "
               "push=%.2f ms",
               g_proj_us / 1000.0, g_raster_us / 1000.0, g_push_us / 1000.0);
    }
    heap_caps_free(band);
  } else {
    ESP_LOGW(kTag, "fps probe banding: no %u B for the band buffer",
             static_cast<unsigned>(kDisplayW * kBandRows * 2));
  }

  g_display.SetWriteClock(24000000u);
  Fail("FPS PROBE COMPLETE", "see serial log for the clock table");
}

#endif  // ORCMAP_CYD_FPS_PROBE

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

void SweepAtZoom(const orcmap_bench::BenchHooks& hooks, const char* direction,
                 uint8_t zoom, orcmap_bench::SweepAccumulator* overall,
                 int* overall_blank) {
  if (!orcmap::SetZoom(&g_viewport, zoom)) return;
  if (MapSource* deepest = DeepestSource()) {
    const orcmap::GeoBounds& b = deepest->manifest->bounds;
    orcmap::SetCenter(&g_viewport, (b.min_lat_deg + b.max_lat_deg) * 0.5,
                      (b.min_lon_deg + b.max_lon_deg) * 0.5);
  }

  orcmap_bench::SweepAccumulator level;
  int blank = 0;
  const auto measure = [&]() {
    RenderFrame();
    // A frame with no coverage drew a message, not a map: not a pipeline
    // measurement, so it is counted separately instead of flattering the
    // rate.
    if (g_no_coverage || g_last_frame.frame_ms <= 0.0) {
      ++blank;
      if (overall_blank != nullptr) ++(*overall_blank);
      return;
    }
    level.Add(g_last_frame);
    if (overall != nullptr) overall->Add(g_last_frame);
  };

  measure();
  for (int i = 0; i < kSweepScreens; ++i) {
    orcmap::PanByPixels(&g_viewport, kDisplayW, 0);
    measure();
  }
  for (int i = 0; i < kSweepScreens; ++i) {
    orcmap::PanByPixels(&g_viewport, -kDisplayW, 0);
    measure();
  }

  orcmap::Viewport map_vp = g_viewport;
  map_vp.width_px = kDisplayW;
  map_vp.height_px = kDisplayH;
  orcmap_bench::EmitSweepZoomRecord(
      hooks, "zoom-pan-1", direction, zoom, kSweepScreens * 2, blank,
      g_active.manifest != nullptr ? g_active.manifest->pack_id.c_str() : "none",
      map_vp, level);

  char note[80];
  std::snprintf(note, sizeof(note), "z%u %s %.0fms %.2ffps %db",
                static_cast<unsigned>(zoom), direction, level.MeanMs(),
                level.Fps(), blank);
  StatusLine(0, TFT_WHITE, note);
}

void RunZoomPanSweep() {
  const orcmap_bench::BenchHooks hooks = MakeHooks();
  uint8_t deepest_zoom = g_zoom_floor;
  if (MapSource* deepest = DeepestSource()) {
    deepest_zoom = deepest->manifest->max_zoom;
  }
  if (deepest_zoom < g_zoom_floor) deepest_zoom = g_zoom_floor;

  orcmap_bench::SweepAccumulator overall;
  int blank = 0;
  const int64_t start = NowUs();
  for (int z = deepest_zoom; z >= static_cast<int>(g_zoom_floor); --z) {
    SweepAtZoom(hooks, "down", static_cast<uint8_t>(z), &overall, &blank);
  }
  for (int z = g_zoom_floor; z <= static_cast<int>(deepest_zoom); ++z) {
    SweepAtZoom(hooks, "up", static_cast<uint8_t>(z), &overall, &blank);
  }
  const double wall_ms = static_cast<double>(NowUs() - start) / 1000.0;
  orcmap_bench::EmitSweepSummaryRecord(hooks, "zoom-pan-1", g_zoom_floor,
                                       deepest_zoom, wall_ms, blank, overall);
}

void RunScenarios() {
  const orcmap_bench::BenchHooks hooks = MakeHooks();
  // Cold then warm at each installed pack's own framing, so the numbers are
  // about this board rather than about a coordinate chosen for another one.
  for (MapSource& source : g_sources) {
    if (source.manifest == nullptr || source.reader == nullptr) continue;
    if (!FocusPack(*source.manifest)) continue;
    g_active = source;

    orcmap::Viewport map_vp = g_viewport;
    map_vp.width_px = kDisplayW;
    map_vp.height_px = kDisplayH;
    orcmap::m5gfx_adapter::DisplayTarget target(g_display);

    orcmap_bench::BenchFrame cold;
    orcmap_bench::RenderMeasuredFrame(*source.reader, map_vp, *g_style, &target,
                                      MakePipelineOptions(), hooks, true, &cold);
    orcmap_bench::EmitFrameRecord(hooks, source.manifest->region_id.c_str(),
                                  "cold", source.manifest->pack_id.c_str(),
                                  g_style->id, map_vp, cold);

    orcmap_bench::BenchFrame warm;
    orcmap_bench::RenderMeasuredFrame(*source.reader, map_vp, *g_style, &target,
                                      MakePipelineOptions(), hooks, true, &warm);
    orcmap_bench::EmitFrameRecord(hooks, source.manifest->region_id.c_str(),
                                  "warm", source.manifest->pack_id.c_str(),
                                  g_style->id, map_vp, warm);
  }
}

}  // namespace

extern "C" void app_main() {
  // FIRST, before anything else allocates: claim the streaming scratch's
  // large buffers while the 176 KiB DRAM region is still whole.
  //
  // This ordering is the fix, not a precaution. Measured on this board, by
  // the time a tile is drawn the largest allocatable block is 47,104 bytes;
  // taking the 32 KiB inflate window out of it leaves exactly 14,336, which
  // is too small for the feature buffer that must follow, so the tile fails.
  // Claimed here, both come out of contiguous DRAM and the steady state
  // needs no large allocation at all.
  const bool scratch_ready =
      orcmap::ReserveMvtStreamScratch(&g_stream_scratch, kMaxFeatureBytes);

  g_display.init();
  g_display.setRotation(kDisplayRotation);
  g_display.setBrightness(200);
  g_display.fillScreen(TFT_BLACK);
  // Rev A wires a second backlight enable; drive it high so the panel is lit
  // on both board revisions.
  if (kBacklight1 >= 0) {
    gpio_config_t bl{};
    bl.pin_bit_mask = 1ULL << kBacklight1;
    bl.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bl);
    gpio_set_level(static_cast<gpio_num_t>(kBacklight1), 1);
  }

  g_display.setTextDatum(top_left);
  g_display.setTextSize(2);
  g_display.setTextColor(TFT_WHITE, TFT_BLACK);
  g_display.drawString("OrcMaps", 8, 10);
  g_display.setTextSize(1);
  g_display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g_display.drawString("CYD 3.5\" benchmark", 8, 36);

  char line[96];
  const int w = g_display.width();
  const int h = g_display.height();
  std::snprintf(line, sizeof(line), "Display  %dx%d", w, h);
  StatusLine(56, TFT_GREEN, line);
  if (w != kDisplayW || h != kDisplayH) {
    std::snprintf(line, sizeof(line), "expected %dx%d", kDisplayW, kDisplayH);
    Fail("UNEXPECTED DISPLAY SIZE", line);
  }

  std::snprintf(line, sizeof(line), "Heap     %u KiB free, %u KiB largest",
                static_cast<unsigned>(InternalFree() / 1024),
                static_cast<unsigned>(InternalLargest() / 1024));
  StatusLine(70, TFT_GREEN, line);
  if (!scratch_ready) {
    Fail("NOT ENOUGH MEMORY FOR MAP BUFFERS",
         "streaming scratch could not be reserved");
  }
  std::snprintf(line, sizeof(line), "Buffers  32+%u KiB reserved",
                static_cast<unsigned>(kMaxFeatureBytes / 1024));
  StatusLine(84, TFT_GREEN, line);

#ifdef ORCMAP_CYD_FPS_PROBE
  ESP_LOGI(kTag, "display %dx%d, configured write clock %uMHz", w, h,
           static_cast<unsigned>(g_display.WriteClock() / 1000000u));
  RunFpsProbe();
#endif

  if (!MountSd()) {
#ifdef ORCMAP_CYD_INTERACTIVE
    // Interactive builds stay useful on a bare board: the touch pins and the
    // axis mapping are the one thing in this file that is not hardware
    // verified yet, and they can be checked without any map data.
    ESP_LOGW(kTag, "no SD card: starting touch test instead of the map");
    static TouchPad test_pad;
    if (!test_pad.Init()) {
      Fail("TOUCH CONTROLLER FAILED TO INIT",
           "check kTouchCs in main.cpp");
    }
    RunTouchTest(test_pad);
#else
    Fail("SD CARD NOT FOUND", "insert a card with /orcmaps");
#endif
  }
  StatusLine(98, TFT_GREEN, "SD card  mounted");

  OpenNumberedReport();

  static orcmap::esp_idf::PackFileSystem pack_fs;
  orcmap::DiscoveryReport discovery;
  orcmap::DiscoverPacks(pack_fs, kPackDir, &g_catalog, &discovery);
  if (!discovery.directory_listed) {
    Fail("NO /orcmaps ON CARD", "create /orcmaps and copy a pack triplet");
  }
  ESP_LOGI(kTag, "discovery: %u manifest(s), %u installed, %u rejected",
           static_cast<unsigned>(discovery.manifests_seen),
           static_cast<unsigned>(discovery.packs_added),
           static_cast<unsigned>(discovery.rejected.size()));
  for (const orcmap::RejectedPack& bad : discovery.rejected) {
    ESP_LOGW(kTag, "rejected %s: %s", bad.manifest_name.c_str(),
             orcmap::PackRejectionName(bad.rejection));
    orcmap_bench::EmitRejectedPackRecord(MakeHooks(), bad);
  }
  std::snprintf(line, sizeof(line), "Packs    %u installed, %u rejected",
                static_cast<unsigned>(discovery.packs_added),
                static_cast<unsigned>(discovery.rejected.size()));
  StatusLine(112, discovery.packs_added > 0 ? TFT_GREEN : TFT_ORANGE, line);
  if (g_catalog.Packs().empty()) {
    Fail("NO USABLE PACKS IN /orcmaps", "run tools/pack-verify on the card");
  }

  for (const orcmap::PackManifest& manifest : g_catalog.Packs()) {
    auto* bytes =
        new orcmap::esp_idf::FileByteSource(manifest.archive_path.c_str());
    // One leaf-directory slot: ~21 KiB of cached directory bytes is memory a
    // dense tile needs more than the cache does on this board.
    auto* reader = new orcmap::PmTilesReader(bytes, 1);
    if (!bytes->Valid() || !reader->Open()) {
      ESP_LOGW(kTag, "pack %s: archive not a readable PMTiles",
               manifest.pack_id.c_str());
      continue;
    }
    MapSource source;
    source.bytes = bytes;
    source.reader = reader;
    source.manifest = &manifest;
    g_sources.push_back(source);
    ESP_LOGI(kTag, "installed %s z%u-%u priority %d",
             manifest.region_id.c_str(),
             static_cast<unsigned>(manifest.min_zoom),
             static_cast<unsigned>(manifest.max_zoom), manifest.priority);
  }
  if (g_sources.empty()) Fail("MAP ARCHIVES UNREADABLE", "re-copy the packs");

  orcmap_bench::BenchIdentity identity;
  identity.bench_version = kBenchVersion;
  identity.board = "CYD 3.5in (ESP32-3248S035)";
  identity.mcu = "ESP32-D0WD-V3";
  identity.idf_version = esp_get_idf_version();
  identity.graphics_adapter = "M5GFX DisplayTarget (ST7796, direct to panel)";
  identity.display_width = w;
  identity.display_height = h;
  identity.psram_total = orcmap_bench::Optional64::None();  // none fitted
  identity.flash_total = orcmap_bench::Optional64::None();
  orcmap_bench::EmitIdentityRecord(MakeHooks(), identity);

  g_style = &orcmap::styles::OrcSdrDark();
  g_viewport = orcmap::Viewport{};
  g_viewport.width_px = kDisplayW;
  g_viewport.height_px = kDisplayH;
  g_viewport.tile_size_px = 256;
  RecomputeZoomFloor();

  if (MapSource* initial = WidestSource()) {
    FocusPack(*initial->manifest);
  }
  vTaskDelay(pdMS_TO_TICKS(800));
  RenderFrame();

#ifdef ORCMAP_CYD_INTERACTIVE
  EmitInteractiveFrame("boot");
  // Interactive mode owns the loop: one finger drags the map, and the frame
  // under the finger is upgraded to full quality when it stops moving.
  ESP_LOGI(kTag, "interactive drag mode");
  static TouchPad touch_pad;
  if (!touch_pad.Init()) {
    Fail("TOUCH CONTROLLER FAILED TO INIT",
         "check kTouchCs / kTouchIrq in main.cpp");
  }
  RunInteractive(touch_pad);
#endif

  // MEASUREMENT BUILDS ONLY run the scenario sequence. In a plain build it
  // made the device look broken: six frames (three packs, cold and warm)
  // drawn back-to-back with no pause, each one appearing progressively
  // because there is no offscreen canvas on this board, so the screen
  // flickered through half-drawn maps and settled on whichever pack came
  // last. A bench instrument may do that; something you look at may not.
#if defined(ORCMAP_CYD_SCENARIOS) || defined(ORCMAP_CYD_AUTOSWEEP)
  ESP_LOGI(kTag, "running fixed scenarios");
  RunScenarios();
#endif

#ifdef ORCMAP_CYD_AUTOSWEEP
  ESP_LOGW(kTag, "AUTOSWEEP build: running zoom/pan sweep");
  RunZoomPanSweep();
  ESP_LOGW(kTag, "AUTOSWEEP complete");
#endif

  // Always END on one finished view rather than wherever a benchmark left
  // the camera: re-frame the widest installed pack and draw it once. On this
  // card that is the world overview, which renders every tile.
  if (MapSource* widest = WidestSource()) {
    FocusPack(*widest->manifest);
  }
  RenderFrame();
  if (g_last_frame.tiles_visible > 0) {
    ESP_LOGI(kTag, "final view: %u/%u tiles, %u features",
             static_cast<unsigned>(g_last_frame.tiles_present),
             static_cast<unsigned>(g_last_frame.tiles_visible),
             static_cast<unsigned>(g_last_frame.features_total));
    // Serial stays the authoritative record even for a plain build: the one
    // frame a user actually looks at is measured like any other.
    orcmap::Viewport map_vp = g_viewport;
    map_vp.width_px = kDisplayW;
    map_vp.height_px = kDisplayH;
    orcmap_bench::EmitFrameRecord(
        MakeHooks(), "final-view", "live",
        g_active.manifest != nullptr ? g_active.manifest->pack_id.c_str()
                                     : "none",
        g_style->id, map_vp, g_last_frame);
  }

  ESP_LOGI(kTag, "holding final view");
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
