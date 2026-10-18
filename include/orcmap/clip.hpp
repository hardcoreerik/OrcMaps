#pragma once

namespace orcmap {

// Liang-Barsky clip of a line to the inclusive pixel rectangle
// [0, width-1] x [0, height-1]. Returns false if the segment is fully
// outside or the rectangle is empty. On true, endpoints lie in-range.
//
// Implemented here from the parametric line / edge-constraint formulation
// (not copied from another codebase). The accepted parameter interval is
// kept as exact rationals and the intersection is evaluated in integer
// arithmetic, because this runs once per drawn line segment on hardware
// without a double-precision FPU. Endpoints beyond +-2^20 take an equivalent
// double path, so a saturated (huge) coordinate still clips correctly.

bool ClipLineToPixels(int* x0, int* y0, int* x1, int* y1, int width,
                      int height);

// Inclusive pixel rectangle [xmin, xmax] x [ymin, ymax]. Used for thick
// strokes whose centerline may sit just outside the framebuffer while the
// brush still overlaps visible pixels.
bool ClipLineToPixelRect(int* x0, int* y0, int* x1, int* y1, int xmin,
                         int ymin, int xmax, int ymax);

}  // namespace orcmap
