#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace protocol {

// UDP market data tick (32 byte alignment)
struct alignas(32) TickPacket {
  uint64_t sequence_num; // 8 bytes
  uint64_t timestamp;    // 8 bytes
  double price;          // 8 bytes
  uint32_t quantity;     // 4 bytes
  char symbol[4];        // 4 bytes
};

// TCP retransmission request
struct RetransmitRequest {
  uint64_t missed_sequence_num;
};

} // namespace protocol
