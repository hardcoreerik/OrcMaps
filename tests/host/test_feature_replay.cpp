#include "orcmap/feature_replay.hpp"
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>

bool deny_allocation = false;
void* operator new(size_t size) {
  if (deny_allocation) throw std::bad_alloc{};
  if (void* pointer = std::malloc(size ? size : 1)) return pointer;
  throw std::bad_alloc{};
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, size_t) noexcept { std::free(pointer); }

namespace {
struct Capture { orcmap::Feature feature; unsigned calls = 0; bool accept = true; };
bool CaptureFeature(const orcmap::Feature& f, void* context) {
  auto& c = *static_cast<Capture*>(context); c.feature = f; ++c.calls; return c.accept;
}
void Equal(const orcmap::Feature& a, const orcmap::Feature& b) {
  assert(a.id == b.id && a.kind == b.kind && a.kind_assigned == b.kind_assigned);
  assert(a.extent == b.extent && a.layer == b.layer && a.geometry.type == b.geometry.type);
  assert(a.property_keys == b.property_keys && a.property_values == b.property_values);
  assert(a.geometry.paths.size() == b.geometry.paths.size());
  for (size_t i = 0; i < a.geometry.paths.size(); ++i) {
    assert(a.geometry.paths[i].size() == b.geometry.paths[i].size());
    for (size_t j = 0; j < a.geometry.paths[i].size(); ++j) {
      assert(a.geometry.paths[i][j].x == b.geometry.paths[i][j].x);
      assert(a.geometry.paths[i][j].y == b.geometry.paths[i][j].y);
    }
  }
}
}  // namespace
int main() {
  orcmap::Feature f, scratch;
  f.id = UINT64_MAX; f.kind = orcmap::FeatureKind::kLabelPrimary; f.kind_assigned = true;
  f.extent = UINT32_MAX; f.layer = "places"; f.geometry.type = orcmap::GeomType::kPoint;
  f.geometry.paths = {{{INT32_MIN, INT32_MAX}, {INT32_MAX, INT32_MIN}}, {}};
  f.property_keys = {"name:en", "name", "rank", "population", "test", "unknown"};
  f.property_values = {std::string("English"), std::string("\xc3\x89vora"), INT64_MIN,
      UINT64_MAX, true, std::monostate{}, -0.0, int64_t(0), int64_t(INT64_MAX), false};
  std::array<uint8_t, 2048> buffer{};
  orcmap::FeatureReplay replay(buffer.data(), buffer.size());
  deny_allocation = true;
  const bool appended_without_allocation = replay.Append(f);
  deny_allocation = false;
  assert(appended_without_allocation && replay.Count() == 1); const size_t bytes = replay.Size();
  Capture c; assert(replay.Replay(scratch, CaptureFeature, &c)); Equal(f, c.feature);
  assert(c.calls == 1 && std::signbit(std::get<double>(c.feature.property_values[6])));
  // The owner can catch allocation failure before any incomplete feature is published.
  orcmap::Feature cold; c.calls = 0; bool caught = false;
  deny_allocation = true;
  try { replay.Replay(cold, CaptureFeature, &c); } catch (const std::bad_alloc&) { caught = true; }
  deny_allocation = false;
  assert(caught && c.calls == 0);
  // Every too-small capacity rejects the entire record without publishing it.
  for (size_t cap = 0; cap < bytes; ++cap) {
    std::array<uint8_t, 2048> limited{}; orcmap::FeatureReplay small(limited.data(), cap);
    assert(!small.Append(f) && small.Size() == 0 && small.Count() == 0);
  }
  // A failed append leaves the preceding record replayable.
  orcmap::FeatureReplay exact(buffer.data(), bytes);
  assert(exact.Append(f) && !exact.Append(f) && exact.Count() == 1 && exact.Size() == bytes);
  c = {}; assert(exact.Replay(scratch, CaptureFeature, &c)); Equal(f, c.feature);
  replay.Clear(); assert(replay.Append(f) && replay.Append(f));
  c = {}; c.accept = false;
  assert(!replay.Replay(scratch, CaptureFeature, &c) && c.calls == 1);
  assert(!replay.Replay(scratch, nullptr, nullptr));
  replay.Clear(); c = {}; assert(replay.Replay(scratch, CaptureFeature, &c) && c.calls == 0);
  orcmap::FeatureReplay unavailable(nullptr, 100); assert(!unavailable.Append(f));
  // Corrupt a committed record; its callback must never receive partial data.
  assert(replay.Append(f)); buffer[0] = 255; // UINT64_MAX already uses ten bytes.
  buffer[9] = 255; c = {};
  assert(!replay.Replay(scratch, CaptureFeature, &c) && c.calls == 0);
  f.kind = static_cast<orcmap::FeatureKind>(255); replay.Clear(); assert(!replay.Append(f));
}
