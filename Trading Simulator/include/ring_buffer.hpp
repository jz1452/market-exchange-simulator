#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace core {

template <typename T, size_t Capacity> class RingBuffer {
public:
  RingBuffer() {
    for (size_t i = 0; i < Capacity; i++) {
      versions_[i].store(0, std::memory_order_relaxed);
    }
  }

  // Called by the UDP Broadcast Thread
  void push(uint64_t seq_num, const T &item) {
    size_t index = seq_num % Capacity;

    // SeqLock: set version to odd (write in progress)
    uint32_t v = versions_[index].load(std::memory_order_relaxed);
    versions_[index].store(v + 1, std::memory_order_release);

    buffer_[index] = item;
    seq_nums_[index] = seq_num;

    // SeqLock: set version to even (write complete)
    versions_[index].store(v + 2, std::memory_order_release);

    if (seq_num > max_seq_.load(std::memory_order_relaxed)) {
      max_seq_.store(seq_num, std::memory_order_relaxed);
    }
  }

  // Called by the TCP Recovery Thread: SeqLock-protected read
  bool get(uint64_t seq_num, T &out_item) const {
    uint64_t current_max = max_seq_.load(std::memory_order_relaxed);
    if (current_max >= Capacity && seq_num <= current_max - Capacity) {
      return false; // Expired from buffer
    }

    size_t index = seq_num % Capacity;

    while (true) {
      uint32_t v1 = versions_[index].load(std::memory_order_acquire);
      if (v1 & 1) continue; // Writer is mid-write, retry

      if (seq_nums_[index] != seq_num) {
        return false; // Slot has been overwritten
      }

      out_item = buffer_[index];

      uint32_t v2 = versions_[index].load(std::memory_order_acquire);
      if (v1 == v2) return true; // Version unchanged = clean read
      // Version changed during read, retry
    }
  }

private:
  std::array<T, Capacity> buffer_;
  std::array<uint64_t, Capacity> seq_nums_;
  std::atomic<uint32_t> versions_[Capacity]; // SeqLock version stamps
  std::atomic<uint64_t> max_seq_{0};
};

} // namespace core
