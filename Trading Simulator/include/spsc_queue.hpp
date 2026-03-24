#pragma once

#include "protocol.hpp"
#include <atomic>
#include <cstddef>
#include <vector>

// Zero-Copy Single-Producer Single-Consumer (SPSC) Ring Buffer
template <size_t Capacity> class SPSCQueue {
private:
  std::vector<protocol::TickPacket> buffer;

  // alignas isolates the CPU cache lines, preventing false sharing
  alignas(64) std::atomic<size_t> head{0};
  alignas(64) std::atomic<size_t> tail{0};

public:
  SPSCQueue() : buffer(Capacity) {}

  // Called by the Network Thread: Requests a raw pointer to an empty
  // memory slot
  protocol::TickPacket *claim_write() {
    size_t current_tail = tail.load(std::memory_order_relaxed);
    size_t next_tail = (current_tail + 1) % Capacity;
    if (next_tail == head.load(std::memory_order_acquire)) {
      return nullptr; // Queue full
    }
    return &buffer[current_tail];
  }

  // Called by the Network Thread: Officially publishes the data block to
  // the Strategy Thread
  void commit_write() {
    size_t current_tail = tail.load(std::memory_order_relaxed);
    tail.store((current_tail + 1) % Capacity, std::memory_order_release);
  }

  // Called by the Strategy Thread: Peeks at the oldest available data
  // without copying it
  const protocol::TickPacket *front() {
    size_t current_head = head.load(std::memory_order_relaxed);
    if (current_head == tail.load(std::memory_order_acquire)) {
      return nullptr; // Queue empty
    }
    return &buffer[current_head];
  }

  // Called by the Strategy Thread: Releases the memory slot back to the
  // Network Thread
  void pop() {
    size_t current_head = head.load(std::memory_order_relaxed);
    head.store((current_head + 1) % Capacity, std::memory_order_release);
  }
};
