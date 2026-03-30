#include "event_loop.hpp"
#include "networking.hpp"
#include "protocol.hpp"
#include "ring_buffer.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <random>
#include <string>
#include <thread>

const std::string MULTICAST_IP = "224.0.0.1";
const int MULTICAST_PORT = 30001;
const int TCP_PORT = 40001;
const size_t RING_BUFFER_SIZE = 50000;

std::atomic<bool> keep_running{true};

void signal_handler(int signum) {
  std::cout << "\n[PUBLISHER] Shutting down...\n";
  keep_running = false;
  exit(signum);
}

// Blocking TCP Recovery Thread: handles subscriber recovery requests
void tcp_recovery_thread_func(
    int tcp_sock, const core::RingBuffer<protocol::TickPacket, RING_BUFFER_SIZE>
                      &ring_buffer) {
  std::cout << "[THREAD] TCP Recovery thread initialised.\n";

  while (keep_running) {
    struct sockaddr_in client_addr {};
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(tcp_sock, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd >= 0) {
      std::cout << "[TCP] Accepted TCP connection for missing packet "
                   "recovery in batch\n";

      protocol::RetransmitRequest req;
      int packets_recovered = 0;

      // Loop reading requests until the client closes the connection
      while (true) {
        ssize_t bytes_read = recv(client_fd, &req, sizeof(req), MSG_WAITALL);
        if (bytes_read == sizeof(protocol::RetransmitRequest)) {
          protocol::TickPacket recovery_tick;
          if (ring_buffer.get(req.missed_sequence_num, recovery_tick)) {
            send(client_fd, &recovery_tick, sizeof(recovery_tick), 0);
            packets_recovered++;
          } else {
            std::cerr << "[TCP] Requested packet seq="
                      << req.missed_sequence_num
                      << " no longer in ring buffer!\n";
            // Send an empty 'dead' tick back to signal failure
            protocol::TickPacket dead_tick{};
            dead_tick.sequence_num = req.missed_sequence_num;
            send(client_fd, &dead_tick, sizeof(dead_tick), 0);
          }
        } else {
          break; // Client finished or error
        }
      }

      std::cout << "[TCP] Finished recovery batch. Retransmitted "
                << packets_recovered << " packets.\n";
      close(client_fd);
    } else if (!keep_running) {
      break;
    }
  }
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::cout << "Starting simple market data publisher...\n";

  try {
    sockaddr_in udp_addr{};
    int udp_sock = networking::create_udp_multicast_sender(
        MULTICAST_IP, MULTICAST_PORT, udp_addr);
    std::cout << "[UDP] Ready to broadcast on " << MULTICAST_IP << ":"
              << MULTICAST_PORT << "\n";

    int tcp_sock = networking::create_tcp_listener(TCP_PORT);
    std::cout << "[TCP] Listening for recovery requests on port " << TCP_PORT
              << "\n";

    // kqueue handles timers
    networking::EventLoop loop;
    core::RingBuffer<protocol::TickPacket, RING_BUFFER_SIZE> ring_buffer;

    networking::EventData market_tick_data{-1, true};
    networking::EventData metrics_timer_data{-2, true};

    loop.register_timer(1, 1, &market_tick_data); // 1ms interval (1000 msgs/s)
    loop.register_timer(2, 1000,
                        &metrics_timer_data); // metrics report every second

    // Spawn dedicated TCP recovery thread
    std::thread tcp_thread(tcp_recovery_thread_func, tcp_sock,
                           std::ref(ring_buffer));

    uint64_t seq_num = 1;
    uint64_t msgs_sent_this_sec = 0;
    protocol::TickPacket last_sent_tick{};

    std::cout << "Entering Event Loop...\n";
    while (keep_running) {
      loop.poll([&](networking::EventData *data, bool is_eof) {
        (void)is_eof;

        if (data == &metrics_timer_data) {
          std::cout << "[METRICS] " << msgs_sent_this_sec
                    << " msgs/sec | Last Tick: " << last_sent_tick.symbol
                    << " @ " << last_sent_tick.price << "\n";
          msgs_sent_this_sec = 0;
        } else if (data == &market_tick_data) {
          // 10,000 msgs/sec
          for (int batch = 0; batch < 10; ++batch) {
            // Generate new TickPacket
            static std::mt19937 rng{std::random_device{}()};
            static std::uniform_int_distribution<uint32_t> sym_dist(0, 49);
            static std::uniform_int_distribution<int> drop_dist(1, 20000);

            // Random walk delta: prices move by up to 0.2% per tick
            static std::normal_distribution<double> price_delta_dist(0.0, 0.01);

            // Stock prices
            static std::vector<double> current_prices(50, 0.0);
            static bool prices_initialised = false;
            if (!prices_initialised) {
              for (int i = 0; i < 50; i++) {
                current_prices[i] =
                    100.0 + (i * 7); // Base prices: 100.00, 107.00, 114.00...
              }
              prices_initialised = true;
            }

            const char *symbols[] = {
                "AAPL", "MSFT", "GOOG", "AMZN", "META", "TSLA", "NVDA", "JPM",
                "JNJ",  "V",    "UNH",  "PG",   "HD",   "DIS",  "MA",   "BAC",
                "VZ",   "CRM",  "XOM",  "PFE",  "NKE",  "INTC", "T",    "KO",
                "MRK",  "PEP",  "ABT",  "WMT",  "CVX",  "CSCO", "MCD",  "ABBV",
                "MDT",  "BMY",  "ACN",  "AVGO", "TXN",  "COST", "NEE",  "QCOM",
                "DHR",  "LIN",  "PM",   "UNP",  "LOW",  "HON",  "UPS",  "IBM",
                "SBUX", "CAT"};

            uint32_t sym_idx = sym_dist(rng);

            // Geometric Brownian Motion style "Random Walk"
            // (Log-Normal percentage shift)
            current_prices[sym_idx] +=
                (current_prices[sym_idx] * price_delta_dist(rng));

            // Prevent negative prices
            if (current_prices[sym_idx] < 1.0)
              current_prices[sym_idx] = 1.0;

            double published_price = current_prices[sym_idx];

            // Simulate a "Drop" (Bad earnings, scandal)
            static std::uniform_int_distribution<int> fund_drop_dist(1, 500);
            // Simulate a "Spike" (Acquisition, breakthrough)
            static std::uniform_int_distribution<int> fund_spike_dist(1, 1000);

            if (fund_drop_dist(rng) == 1) {
              // Permanent structural damage (6 to 9 Sigma)
              static std::uniform_real_distribution<double> drop_depth(0.06,
                                                                       0.09);
              current_prices[sym_idx] -=
                  (current_prices[sym_idx] * drop_depth(rng));
              if (current_prices[sym_idx] < 1.0)
                current_prices[sym_idx] = 1.0;
              published_price = current_prices[sym_idx];
            } else if (fund_spike_dist(rng) == 1) {
              // Permanent structural growth (6 to 9 Sigma)
              static std::uniform_real_distribution<double> spike_depth(0.06,
                                                                        0.09);
              current_prices[sym_idx] +=
                  (current_prices[sym_idx] * spike_depth(rng));
              published_price = current_prices[sym_idx];
            } else {
              // Simulate a "Flash Crash" - 1.0% chance
              static std::uniform_int_distribution<int> anomaly_drop_dist(1,
                                                                          100);
              // Simulate a "Flash Spike" - 0.5% chance
              static std::uniform_int_distribution<int> anomaly_spike_dist(1,
                                                                           200);

              if (anomaly_drop_dist(rng) == 1) {
                // Momentary Flash Crash (3 to 5 Sigma)
                static std::uniform_real_distribution<double> a_drop_depth(
                    0.025, 0.05);
                published_price -= (published_price * a_drop_depth(rng));
              } else if (anomaly_spike_dist(rng) == 1) {
                // Momentary Flash Spike (3 to 5 Sigma)
                static std::uniform_real_distribution<double> a_spike_depth(
                    0.025, 0.05);
                published_price += (published_price * a_spike_depth(rng));
              }
            }

            protocol::TickPacket tick{};
            tick.sequence_num = seq_num;
            std::strncpy(tick.symbol, symbols[sym_idx],
                         sizeof(tick.symbol) - 1);
            tick.price = published_price;
            tick.quantity = 100 + (seq_num % 50);

            // Record timestamp to compare with subscriber --> calculate latency
            tick.timestamp =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now()
                        .time_since_epoch())
                    .count();

            // Push to ring Buffer (SeqLock-protected)
            ring_buffer.push(seq_num, tick);

            // Send over UDP (artificially drop 1 in 2000 packets)
            bool drop_simulation = (drop_dist(rng) == 1);
            if (!drop_simulation) {
              ssize_t sent =
                  sendto(udp_sock, &tick, sizeof(protocol::TickPacket), 0,
                         (struct sockaddr *)&udp_addr, sizeof(udp_addr));
              if (sent > 0) {
                msgs_sent_this_sec++;
              }
            } else {
              std::cout << "[SIMULATION] Dropped UDP Broadcast for TICK seq="
                        << seq_num << "\n";
            }
            last_sent_tick = tick;
            seq_num++;
          }
        }
      });
    }

    tcp_thread.join();

  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
