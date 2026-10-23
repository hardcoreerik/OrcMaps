#include "orcmap/feature_replay.hpp"

#include <cstring>
#include <limits>

namespace orcmap {
namespace {
uint64_t Zig(int64_t n) {
  return n < 0 ? uint64_t(-(n + 1)) * 2 + 1 : uint64_t(n) * 2;
}
int64_t Unzig(uint64_t n) {
  return n & 1 ? -int64_t(n >> 1) - 1 : int64_t(n >> 1);
}
struct Writer {
  uint8_t* data;
  size_t capacity, pos;
  bool Byte(uint8_t n) {
    if (pos == capacity) return false;
    data[pos++] = n;
    return true;
  }
  bool Number(uint64_t n) {
    while (n >= 128) { if (!Byte(uint8_t(n) | 128)) return false; n >>= 7; }
    return Byte(uint8_t(n));
  }
  bool String(const std::string& s) {
    if (!Number(s.size()) || s.size() > capacity - pos) return false;
    if (!s.empty()) std::memcpy(data + pos, s.data(), s.size());
    pos += s.size();
    return true;
  }
};
struct Reader {
  const uint8_t* data;
  size_t size, pos = 0;
  bool Number(uint64_t& n) {
    n = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
      if (pos == size) return false;
      const uint8_t b = data[pos++];
      if (shift == 63 && (b & 254)) return false;
      n |= uint64_t(b & 127) << shift;
      if (!(b & 128)) return true;
    }
    return false;
  }
  bool String(std::string& s) {
    uint64_t n;
    if (!Number(n) || n > size - pos) return false;
    s.assign(reinterpret_cast<const char*>(data + pos), size_t(n));
    pos += size_t(n);
    return true;
  }
  bool PointCoordinate(int64_t& coordinate) {
    uint64_t n;
    if (!Number(n)) return false;
    const int64_t delta = Unzig(n);
    if (delta < int64_t(INT32_MIN) - coordinate ||
        delta > int64_t(INT32_MAX) - coordinate) return false;
    coordinate += delta;
    return true;
  }
};
bool WriteFeature(Writer& w, const Feature& f) {
  if (!w.Number(f.id) || !w.Number(static_cast<unsigned>(f.kind)) ||
      !w.Number(f.kind_assigned) || !w.Number(f.extent) || !w.String(f.layer) ||
      !w.Number(static_cast<unsigned>(f.geometry.type)) ||
      !w.Number(f.geometry.paths.size())) return false;
  for (const auto& path : f.geometry.paths) {
    if (!w.Number(path.size())) return false;
    int64_t x = 0, y = 0;
    for (const auto& point : path) {
      if (!w.Number(Zig(int64_t(point.x) - x)) ||
          !w.Number(Zig(int64_t(point.y) - y))) return false;
      x = point.x; y = point.y;
    }
  }
  if (!w.Number(f.property_keys.size())) return false;
  for (const auto& key : f.property_keys) if (!w.String(key)) return false;
  if (!w.Number(f.property_values.size())) return false;
  for (const auto& value : f.property_values) {
    if (!w.Number(value.index())) return false;
    switch (value.index()) {
      case 0: break;
      case 1: if (!w.String(std::get<std::string>(value))) return false; break;
      case 2: {
        uint64_t bits; const double number = std::get<double>(value);
        static_assert(sizeof(bits) == sizeof(number));
        std::memcpy(&bits, &number, sizeof(bits));
        if (!w.Number(bits)) return false;
        break;
      }
      case 3: if (!w.Number(Zig(std::get<int64_t>(value)))) return false; break;
      case 4: if (!w.Number(std::get<uint64_t>(value))) return false; break;
      case 5: if (!w.Number(std::get<bool>(value))) return false; break;
      default: return false;
    }
  }
  return true;
}
bool ReadFeature(Reader& r, Feature& f) {
  uint64_t kind, assigned, extent, type, paths;
  if (!r.Number(f.id) || !r.Number(kind) || kind >= unsigned(FeatureKind::kCount) ||
      !r.Number(assigned) || assigned > 1 || !r.Number(extent) || extent > UINT32_MAX ||
      !r.String(f.layer) || !r.Number(type) || type > unsigned(GeomType::kPolygon) ||
      !r.Number(paths) || paths > r.size - r.pos) return false;
  f.kind = static_cast<FeatureKind>(kind); f.kind_assigned = assigned != 0;
  f.extent = uint32_t(extent); f.geometry.type = static_cast<GeomType>(type);
  f.geometry.paths.resize(size_t(paths));
  for (auto& path : f.geometry.paths) {
    uint64_t points;
    if (!r.Number(points) || points > (r.size - r.pos) / 2) return false;
    path.resize(size_t(points)); int64_t x = 0, y = 0;
    for (auto& point : path) {
      if (!r.PointCoordinate(x) || !r.PointCoordinate(y)) return false;
      point = {int32_t(x), int32_t(y)};
    }
  }
  uint64_t keys, values;
  if (!r.Number(keys) || keys > r.size - r.pos) return false;
  f.property_keys.resize(size_t(keys));
  for (auto& key : f.property_keys) if (!r.String(key)) return false;
  if (!r.Number(values) || values > r.size - r.pos) return false;
  f.property_values.resize(size_t(values));
  for (auto& value : f.property_values) {
    uint64_t tag, n;
    if (!r.Number(tag)) return false;
    if (tag == 1) {
      if (!std::holds_alternative<std::string>(value)) value = std::string{};
      if (!r.String(std::get<std::string>(value))) return false;
      continue;
    }
    if (tag > 5 || (tag && !r.Number(n))) return false;
    switch (tag) {
      case 0: value = std::monostate{}; break;
      case 2: { double number; std::memcpy(&number, &n, sizeof(number)); value = number; break; }
      case 3: value = Unzig(n); break;
      case 4: value = n; break;
      case 5: if (n > 1) return false; value = n != 0; break;
    }
  }
  return true;
}
}  // namespace

bool FeatureReplay::Append(const Feature& f) {
  if (unsigned(f.kind) >= unsigned(FeatureKind::kCount) ||
      unsigned(f.geometry.type) > unsigned(GeomType::kPolygon)) return false;
  Writer writer{storage_, capacity_, size_};
  if (!WriteFeature(writer, f)) return false;
  size_ = writer.pos; ++count_;
  return true;
}
bool FeatureReplay::Replay(Feature& scratch, StreamedFeatureSink sink, void* context) const {
  if (!sink) return false;
  Reader reader{storage_, size_};
  for (size_t i = 0; i < count_; ++i)
    if (!ReadFeature(reader, scratch) || !sink(scratch, context)) return false;
  return reader.pos == size_;
}
bool FeatureReplay::Relocate(uint8_t* storage, size_t capacity) {
  if (!storage || capacity < size_) return false;
  if (size_) std::memmove(storage, storage_, size_);
  storage_ = storage; capacity_ = capacity;
  return true;
}
}  // namespace orcmap
