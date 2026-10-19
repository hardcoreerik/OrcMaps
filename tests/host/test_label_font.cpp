#include <cstdio>
#include "freetype_label_target.hpp"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  orcmap::host::FramebufferTarget frame(256, 100);
  orcmap::host::FreeTypeLabelTarget target(frame, argv[1], 24);
  if (!target.valid()) return 2;
  int failures = 0;
  auto check = [&failures](bool good, const char* why) {
    if (!good) { ++failures; std::printf("FAIL %s\n", why); }
  };
  orcmap::TextExtent extent;
  check(target.MeasureText("Eugene", &extent) && extent.height >= 24, "real readable metrics");
  check(target.MeasureText("S\xC3\xA3o Paulo", &extent), "real diacritics");
  check(target.MeasureText("\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1", &extent), "real Greek glyphs");
  check(target.MeasureText("\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0", &extent), "real Cyrillic glyphs");
  check(!target.MeasureText("\xD8\xA7", &extent), "complex shaping explicitly unsupported");
  check(!target.MeasureText("\xE6\x9D\xB1\xE4\xBA\xAC", &extent), "missing CJK glyph rejected by this test font");
  check(!target.MeasureText(std::string_view("\xC0\xAF", 2), &extent), "bad UTF8 rejected");
  const auto ink = orcmap::Color::Rgb(230, 240, 250);
  const auto halo = orcmap::Color::Rgb(20, 30, 40);
  check(target.MeasureText("j\xC3\x81", &extent), "overhang/diacritic metrics");
  target.DrawText(20, 20, "j\xC3\x81", ink, halo);
  int painted = 0, escaped = 0;
  const auto black = orcmap::Color::Rgb(0, 0, 0);
  for (int y = 0; y < frame.Height(); ++y)
    for (int x = 0; x < frame.Width(); ++x)
      if (frame.At(x, y) != black) {
        ++painted;
        if (x < 19 || y < 19 || x > 20 + extent.width || y > 20 + extent.height) ++escaped;
      }
  check(painted > 0 && escaped == 0, "actual glyph pixels inside measured extent plus halo");
  orcmap::host::FreeTypeLabelTarget missing(frame, "/nonexistent/orcmaps-font.ttf");
  check(!missing.valid(), "missing font is an explicit error");
  std::printf("real font tests: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
