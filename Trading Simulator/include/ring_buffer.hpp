#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace core {

template <typename T, size_t Capacity> class RingBuffer {
public:
  // Insert a new value and record its sequence number
  void push(uint64_t seq_num, const T &item) {
    size_t index = seq_num % Capacity;
    buffer_[index] = item;
    seq_nums_[index] = seq_num;

    if (seq_num > max_seq_) {
      max_seq_ = seq_num;
    }
  }

  // Try to retrieve a past message by its sequence number
  // Returns true if the message is still in the buffer, false if it was
  // overwritten
  bool get(uint64_t seq_num, T &out_item) const {
    if (max_seq_ >= Capacity && seq_num <= max_seq_ - Capacity) {
      return false;
    }

    size_t index = seq_num % Capacity;

    if (seq_nums_[index] == seq_num) {
      out_item = buffer_[index];
      return true;
    }

    return false;
  }

private:
  std::array<T, Capacity> buffer_;
  std::array<uint64_t, Capacity> seq_nums_;
  uint64_t max_seq_ = 0;
};

} // namespace core
