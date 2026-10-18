#include "orcmap/clip.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace orcmap {

namespace {

// Liang-Barsky keeps the accepted parameter interval [t0, t1]. The fast path
// holds those bounds as exact rationals instead of doubles: this runs once
// per drawn line segment, and on ESP32 a double operation is a soft-float ROM
// call -- the previous formulation cost ~50 of them per segment (measured on
// the CYD firmware).
struct Ratio {
  int64_t num = 0;
  int64_t den = 1;  // always > 0
};

// Endpoints above this magnitude take the double path. Below it every
// product in ClipEdge and RoundedPoint stays inside int64: |coord| <= 2^21
// once the rect bound is added, and four such factors multiply out to 2^44.
constexpr int64_t kMaxFastCoordinate = 1 << 20;

bool WithinFastRange(int v) {
  return v >= -kMaxFastCoordinate && v <= kMaxFastCoordinate;
}

// Compares num/den (den > 0) against an exact rational bound.
int CompareRatio(int64_t num, int64_t den, const Ratio& bound) {
  const int64_t lhs = num * bound.den;
  const int64_t rhs = bound.num * den;
  if (lhs < rhs) return -1;
  if (lhs > rhs) return 1;
  return 0;
}

// One edge constraint. `entering` is the sign of the original p: a negative
// p constrains the lower bound, a positive p the upper bound.
bool ClipEdge(int64_t p, int64_t q, Ratio* t0, Ratio* t1) {
  if (p == 0) return q >= 0;
  const bool entering = p < 0;
  const int64_t pn = entering ? -p : p;
  const int64_t qn = entering ? -q : q;
  if (entering) {
    if (CompareRatio(qn, pn, *t1) > 0) return false;
    if (CompareRatio(qn, pn, *t0) > 0) {
      t0->num = qn;
      t0->den = pn;
    }
  } else {
    if (CompareRatio(qn, pn, *t0) < 0) return false;
    if (CompareRatio(qn, pn, *t1) < 0) {
      t1->num = qn;
      t1->den = pn;
    }
  }
  return true;
}

int64_t FloorDiv(int64_t value, int64_t divisor) {
  int64_t quotient = value / divisor;
  if ((value % divisor) != 0 && value < 0) --quotient;
  return quotient;
}

// floor(origin + t * delta + 0.5), clamped to [lo, hi]. Evaluated without
// leaving integer arithmetic; this is the same rounding ClampPixel used.
int RoundedPoint(int64_t origin, int64_t delta, const Ratio& t, int lo,
                 int hi) {
  const int64_t num = 2 * (origin * t.den + t.num * delta) + t.den;
  const int64_t den = 2 * t.den;
  const int64_t rounded = FloorDiv(num, den);
  if (rounded < static_cast<int64_t>(lo)) return lo;
  if (rounded > static_cast<int64_t>(hi)) return hi;
  return static_cast<int>(rounded);
}

// --- bounded-range fallback ------------------------------------------------
//
// Only reached when an endpoint is outside kMaxFastCoordinate. Kept as the
// exact double formulation so gigantic offscreen segments (a saturated
// projection, a caller passing INT_MIN) still clip correctly.

bool ClipT(double p, double q, double* t0, double* t1) {
  if (p == 0.0) return q >= 0.0;
  const double r = q / p;
  if (p < 0.0) {
    if (r > *t1) return false;
    if (r > *t0) *t0 = r;
  } else {
    if (r < *t0) return false;
    if (r < *t1) *t1 = r;
  }
  return true;
}

int ClampPixel(double v, int lo, int hi) {
  if (!std::isfinite(v)) return lo;
  if (v < static_cast<double>(lo)) return lo;
  if (v > static_cast<double>(hi)) return hi;
  return static_cast<int>(std::floor(v + 0.5));
}

bool ClipLineWide(int* x0, int* y0, int* x1, int* y1, int xmin, int ymin,
                  int xmax, int ymax) {
  const double xmin_d = static_cast<double>(xmin);
  const double ymin_d = static_cast<double>(ymin);
  const double xmax_d = static_cast<double>(xmax);
  const double ymax_d = static_cast<double>(ymax);
  const double ax = static_cast<double>(*x0);
  const double ay = static_cast<double>(*y0);
  const double bx = static_cast<double>(*x1);
  const double by = static_cast<double>(*y1);
  const double dx = bx - ax;
  const double dy = by - ay;
  double t0 = 0.0;
  double t1 = 1.0;
  if (!ClipT(-dx, ax - xmin_d, &t0, &t1)) return false;
  if (!ClipT(dx, xmax_d - ax, &t0, &t1)) return false;
  if (!ClipT(-dy, ay - ymin_d, &t0, &t1)) return false;
  if (!ClipT(dy, ymax_d - ay, &t0, &t1)) return false;

  *x0 = ClampPixel(ax + t0 * dx, xmin, xmax);
  *y0 = ClampPixel(ay + t0 * dy, ymin, ymax);
  *x1 = ClampPixel(ax + t1 * dx, xmin, xmax);
  *y1 = ClampPixel(ay + t1 * dy, ymin, ymax);
  return true;
}

}  // namespace

bool ClipLineToPixelRect(int* x0, int* y0, int* x1, int* y1, int xmin,
                         int ymin, int xmax, int ymax) {
  if (x0 == nullptr || y0 == nullptr || x1 == nullptr || y1 == nullptr) {
    return false;
  }
  if (xmin > xmax || ymin > ymax) return false;

  if (!WithinFastRange(*x0) || !WithinFastRange(*y0) ||
      !WithinFastRange(*x1) || !WithinFastRange(*y1)) {
    return ClipLineWide(x0, y0, x1, y1, xmin, ymin, xmax, ymax);
  }

  const int64_t ax = *x0;
  const int64_t ay = *y0;
  const int64_t dx = static_cast<int64_t>(*x1) - ax;
  const int64_t dy = static_cast<int64_t>(*y1) - ay;
  Ratio t0{0, 1};
  Ratio t1{1, 1};
  if (!ClipEdge(-dx, ax - xmin, &t0, &t1)) return false;
  if (!ClipEdge(dx, static_cast<int64_t>(xmax) - ax, &t0, &t1)) return false;
  if (!ClipEdge(-dy, ay - ymin, &t0, &t1)) return false;
  if (!ClipEdge(dy, static_cast<int64_t>(ymax) - ay, &t0, &t1)) return false;

  *x0 = RoundedPoint(ax, dx, t0, xmin, xmax);
  *y0 = RoundedPoint(ay, dy, t0, ymin, ymax);
  *x1 = RoundedPoint(ax, dx, t1, xmin, xmax);
  *y1 = RoundedPoint(ay, dy, t1, ymin, ymax);
  return true;
}

bool ClipLineToPixels(int* x0, int* y0, int* x1, int* y1, int width,
                      int height) {
  if (width <= 0 || height <= 0) return false;
  return ClipLineToPixelRect(x0, y0, x1, y1, 0, 0, width - 1, height - 1);
}

}  // namespace orcmap
