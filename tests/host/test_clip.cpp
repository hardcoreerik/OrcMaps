#include "framebuffer_target.hpp"
#include "orcmap/clip.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

#include "test_util.hpp"

namespace {

bool AllPixels(const orcmap::host::FramebufferTarget& fb, orcmap::Color c) {
  for (int y = 0; y < fb.Height(); ++y) {
    for (int x = 0; x < fb.Width(); ++x) {
      if (fb.At(x, y) != c) return false;
    }
  }
  return true;
}

void TestClipFullyOutside() {
  int x0 = -1000000, y0 = -1000000, x1 = -900000, y1 = -900000;
  ORCMAP_EXPECT_TRUE(!orcmap::ClipLineToPixels(&x0, &y0, &x1, &y1, 32, 32));
}

void TestClipAcceptsOnscreenSegment() {
  int x0 = 2, y0 = 2, x1 = 10, y1 = 10;
  ORCMAP_EXPECT_TRUE(orcmap::ClipLineToPixels(&x0, &y0, &x1, &y1, 32, 32));
  ORCMAP_EXPECT_EQ(x0, 2);
  ORCMAP_EXPECT_EQ(y0, 2);
  ORCMAP_EXPECT_EQ(x1, 10);
  ORCMAP_EXPECT_EQ(y1, 10);
}

void TestClipEmptyRect() {
  int x0 = 0, y0 = 0, x1 = 1, y1 = 1;
  ORCMAP_EXPECT_TRUE(!orcmap::ClipLineToPixels(&x0, &y0, &x1, &y1, 0, 8));
  ORCMAP_EXPECT_TRUE(!orcmap::ClipLineToPixels(&x0, &y0, &x1, &y1, 8, 0));
}

void TestFullyOffscreenLineLeavesFramebufferUnchanged() {
  const orcmap::Color bg = orcmap::Color::Rgb(10, 20, 30);
  orcmap::host::FramebufferTarget fb(32, 32, bg);
  fb.DrawLine(-1000000, -1000000, -900000, -900000, orcmap::Color::Rgb(255, 0, 0));
  ORCMAP_EXPECT_TRUE(AllPixels(fb, bg));
}

void TestPartialOffscreenHorizontalLine() {
  const orcmap::Color bg = orcmap::Color::Rgb(0, 0, 0);
  const orcmap::Color fg = orcmap::Color::Rgb(0, 255, 0);
  orcmap::host::FramebufferTarget fb(32, 32, bg);
  fb.DrawLine(-1000000, 16, 1000000, 16, fg);
  ORCMAP_EXPECT_TRUE(fb.At(0, 16) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(15, 16) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(31, 16) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(16, 0) == bg);
  ORCMAP_EXPECT_TRUE(fb.At(16, 31) == bg);
}

void TestHugeDiagonalClipsToVisibleSegment() {
  const orcmap::Color bg = orcmap::Color::Rgb(0, 0, 0);
  const orcmap::Color fg = orcmap::Color::Rgb(255, 255, 0);
  orcmap::host::FramebufferTarget fb(32, 32, bg);
  fb.DrawLine(-1000000, -1000000, 1000000, 1000000, fg);
  ORCMAP_EXPECT_TRUE(fb.At(0, 0) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(16, 16) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(31, 31) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(31, 0) == bg);
  ORCMAP_EXPECT_TRUE(fb.At(0, 31) == bg);
}

void TestLargePolygonCoordinatesDoNotOverflow() {
  const orcmap::Color bg = orcmap::Color::Rgb(1, 1, 1);
  const orcmap::Color fg = orcmap::Color::Rgb(0, 0, 255);
  orcmap::host::FramebufferTarget fb(16, 16, bg);
  const int lo = std::numeric_limits<int>::min() / 2;
  const int hi = std::numeric_limits<int>::max() / 2;
  const int xy[] = {lo, lo, hi, lo, hi, hi, lo, hi};
  fb.FillPolygon(xy, 4, fg);
  ORCMAP_EXPECT_TRUE(fb.At(0, 0) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(8, 8) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(15, 15) == fg);
}

void TestIntMinMaxPolygonDoesNotOverflow() {
  const orcmap::Color bg = orcmap::Color::Rgb(2, 2, 2);
  const orcmap::Color fg = orcmap::Color::Rgb(0, 128, 0);
  orcmap::host::FramebufferTarget fb(8, 8, bg);
  const int lo = std::numeric_limits<int>::min();
  const int hi = std::numeric_limits<int>::max();
  const int xy[] = {lo, lo, hi, lo, hi, hi, lo, hi};
  fb.FillPolygon(xy, 4, fg);
  ORCMAP_EXPECT_TRUE(fb.At(0, 0) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(4, 4) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(7, 7) == fg);
}

void TestFillRectBoundAddDoesNotOverflow() {
  const orcmap::Color bg = orcmap::Color::Rgb(3, 3, 3);
  const orcmap::Color fg = orcmap::Color::Rgb(9, 9, 9);
  orcmap::host::FramebufferTarget fb(8, 8, bg);
  fb.FillRect(std::numeric_limits<int>::max() - 3, 0, 100, 2, fg);
  ORCMAP_EXPECT_TRUE(fb.At(0, 0) == bg);
  fb.FillRect(-100, 3, std::numeric_limits<int>::max(), 1, fg);
  ORCMAP_EXPECT_TRUE(fb.At(0, 3) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(7, 3) == fg);
  ORCMAP_EXPECT_TRUE(fb.At(0, 4) == bg);
}

// Independent reference: the pre-rewrite double formulation, evaluated in
// long double so the comparison is not just the implementation against
// itself.
bool ReferenceClip(int* x0, int* y0, int* x1, int* y1, int xmin, int ymin,
                   int xmax, int ymax) {
  const auto clip_t = [](long double p, long double q, long double* t0,
                         long double* t1) {
    if (p == 0.0L) return q >= 0.0L;
    const long double r = q / p;
    if (p < 0.0L) {
      if (r > *t1) return false;
      if (r > *t0) *t0 = r;
    } else {
      if (r < *t0) return false;
      if (r < *t1) *t1 = r;
    }
    return true;
  };
  const auto clamp_pixel = [](long double v, int lo, int hi) {
    if (v < static_cast<long double>(lo)) return lo;
    if (v > static_cast<long double>(hi)) return hi;
    return static_cast<int>(std::floor(v + 0.5L));
  };

  const long double ax = static_cast<long double>(*x0);
  const long double ay = static_cast<long double>(*y0);
  const long double bx = static_cast<long double>(*x1);
  const long double by = static_cast<long double>(*y1);
  const long double dx = bx - ax;
  const long double dy = by - ay;
  long double t0 = 0.0L;
  long double t1 = 1.0L;
  const long double xmin_d = static_cast<long double>(xmin);
  const long double ymin_d = static_cast<long double>(ymin);
  const long double xmax_d = static_cast<long double>(xmax);
  const long double ymax_d = static_cast<long double>(ymax);
  if (!clip_t(-dx, ax - xmin_d, &t0, &t1)) return false;
  if (!clip_t(dx, xmax_d - ax, &t0, &t1)) return false;
  if (!clip_t(-dy, ay - ymin_d, &t0, &t1)) return false;
  if (!clip_t(dy, ymax_d - ay, &t0, &t1)) return false;

  *x0 = clamp_pixel(ax + t0 * dx, xmin, xmax);
  *y0 = clamp_pixel(ay + t0 * dy, ymin, ymax);
  *x1 = clamp_pixel(ax + t1 * dx, xmin, xmax);
  *y1 = clamp_pixel(ay + t1 * dy, ymin, ymax);
  return true;
}

void TestIntegerClipMatchesReference() {
  uint32_t state = 0x12345678u;
  const auto next = [&state]() {
    state = state * 1664525u + 1013904223u;
    return static_cast<int>(state >> 8);
  };
  const int width = 64;
  const int height = 48;
  int accepted = 0;
  for (int i = 0; i < 4000; ++i) {
    // Mix scales: coordinates near the rect exercise the clip strongly,
    // large ones exercise the wide geometry the integer path must survive.
    const int spans[3] = {96, 8192, 100000};
    const int span = spans[i % 3];
    const int range = 2 * span + 1;
    const int ax = next() % range - span;
    const int ay = next() % range - span;
    const int bx = next() % range - span;
    const int by = next() % range - span;

    int rx0 = ax, ry0 = ay, rx1 = bx, ry1 = by;
    const bool want =
        ReferenceClip(&rx0, &ry0, &rx1, &ry1, 0, 0, width - 1, height - 1);
    int gx0 = ax, gy0 = ay, gx1 = bx, gy1 = by;
    const bool got =
        orcmap::ClipLineToPixels(&gx0, &gy0, &gx1, &gy1, width, height);
    ORCMAP_EXPECT_TRUE(want == got);
    if (!want) continue;
    ++accepted;
    ORCMAP_EXPECT_EQ(gx0, rx0);
    ORCMAP_EXPECT_EQ(gy0, ry0);
    ORCMAP_EXPECT_EQ(gx1, rx1);
    ORCMAP_EXPECT_EQ(gy1, ry1);
  }
  // The sample has to actually clip, or this proves nothing.
  ORCMAP_EXPECT_TRUE(accepted > 200);
}

void TestHugeCoordinatesUseBoundedFallback() {
  int x0 = -2000000, y0 = -2000000, x1 = 2000000, y1 = 2000000;
  ORCMAP_EXPECT_TRUE(orcmap::ClipLineToPixels(&x0, &y0, &x1, &y1, 32, 32));
  ORCMAP_EXPECT_EQ(x0, 0);
  ORCMAP_EXPECT_EQ(y0, 0);
  ORCMAP_EXPECT_EQ(x1, 31);
  ORCMAP_EXPECT_EQ(y1, 31);
}

void TestIntExtremeEndpointsClipSafely() {
  int x0 = std::numeric_limits<int>::min();
  int y0 = 0;
  int x1 = std::numeric_limits<int>::max();
  int y1 = 0;
  ORCMAP_EXPECT_TRUE(orcmap::ClipLineToPixels(&x0, &y0, &x1, &y1, 16, 16));
  ORCMAP_EXPECT_EQ(x0, 0);
  ORCMAP_EXPECT_EQ(y0, 0);
  ORCMAP_EXPECT_EQ(x1, 15);
  ORCMAP_EXPECT_EQ(y1, 0);
}

}  // namespace

void RunClipTests() {
  TestClipFullyOutside();
  TestClipAcceptsOnscreenSegment();
  TestClipEmptyRect();
  TestFullyOffscreenLineLeavesFramebufferUnchanged();
  TestPartialOffscreenHorizontalLine();
  TestHugeDiagonalClipsToVisibleSegment();
  TestLargePolygonCoordinatesDoNotOverflow();
  TestIntMinMaxPolygonDoesNotOverflow();
  TestFillRectBoundAddDoesNotOverflow();
  TestIntegerClipMatchesReference();
  TestHugeCoordinatesUseBoundedFallback();
  TestIntExtremeEndpointsClipSafely();
}
