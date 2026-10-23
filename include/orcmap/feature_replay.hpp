#pragma once

#include <cstddef>
#include <cstdint>
#include "orcmap/mvt_stream.hpp"

namespace orcmap {

// Transient feature replay in caller-owned storage, never an on-disk format.
// The owner supplies synchronization, source identity, eviction and PSRAM policy.
class FeatureReplay {
 public:
  FeatureReplay(uint8_t* storage, size_t capacity)
      : storage_(storage), capacity_(storage ? capacity : 0) {}
  // Allocation-free. Failure leaves the preceding complete records intact.
  bool Append(const Feature& feature);
  // Reuses caller scratch; a false sink result cancels immediately. Allocation
  // exceptions are handled by the caller, as for a streaming feature sink.
  bool Replay(Feature& scratch, StreamedFeatureSink sink, void* context) const;
  void Clear() { size_ = count_ = 0; }
  size_t Size() const { return size_; }
  size_t Count() const { return count_; }

 private:
  uint8_t* storage_;
  size_t capacity_, size_ = 0, count_ = 0;
};

}  // namespace orcmap
