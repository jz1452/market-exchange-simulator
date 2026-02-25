#include "event_loop.hpp"
#include "networking.hpp"
#include "protocol.hpp"
#include "ring_buffer.hpp"
#include <chrono>
#include <iostream>
#include <random>
#include <string>

const std::string MULTICAST_IP = "224.0.0.1";
const int MULTICAST_PORT = 30001;
const int TCP_PORT = 40001;
const size_t RING_BUFFER_SIZE = 10000;

int main() {
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

    networking::EventLoop loop;
    core::RingBuffer<protocol::TickPacket, RING_BUFFER_SIZE> ring_buffer;

    networking::EventData tcp_listen_data{tcp_sock, false};
    networking::EventData market_tick_data{-1, true};
    networking::EventData metrics_timer_data{-2, true};

    loop.register_read(tcp_sock, &tcp_listen_data);
    loop.register_timer(1, 1, &market_tick_data); // 1ms interval (1000 msgs/s)
    loop.register_timer(2, 1000,
                        &metrics_timer_data); // metrics report every second

    uint64_t seq_num = 1;
    uint64_t msgs_sent_this_sec = 0;
    protocol::TickPacket last_sent_tick{};

    std::cout << "Entering Event Loop...\n";
    while (true) {
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
            static std::uniform_real_distribution<double> price_delta_dist(
                -0.002, 0.002);

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

            // Simulate a "Fundament Crash" 0.2% of the time
            static std::uniform_int_distribution<int> fundamental_dist(1, 500);
            if (fundamental_dist(rng) == 1) {
              // A massive price collapse that we write back to current_prices
              // Simulates company having permanent damage to its stock prices
              // Drops the stock by 4% to 7% of its current value
              static std::uniform_real_distribution<double> fundamental_depth(
                  0.04, 0.07);
              current_prices[sym_idx] -=
                  (current_prices[sym_idx] * fundamental_depth(rng));
              if (current_prices[sym_idx] < 1.0)
                current_prices[sym_idx] = 1.0;
              published_price = current_prices[sym_idx];
            } else {
              // Simulate a significant random price drop 1% of the time
              static std::uniform_int_distribution<int> anomaly_dist(1, 100);
              if (anomaly_dist(rng) == 1) {
                // Momentary crash of 1.5% to 3.0% of its current value
                static std::uniform_real_distribution<double> anomaly_depth(
                    0.015, 0.030);
                published_price -= (published_price * anomaly_depth(rng));
                // dont write this back to current_prices array
                // guarantees the price will rubber-band back to normal on
                // the very next tick.
              }
            }

            protocol::TickPacket tick{};
            tick.sequence_num = seq_num;
            std::strncpy(tick.symbol, symbols[sym_idx],
                         sizeof(tick.symbol) - 1);
            tick.price = published_price;
            tick.quantity = 100 + (seq_num % 50);

            // Record timestamp right before network send
            tick.timestamp =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now()
                        .time_since_epoch())
                    .count();

            // Push to our historical ring Buffer
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

        } else if (!data->is_timer && data->fd == tcp_sock) {
          struct sockaddr_in client_addr {};
          socklen_t client_len = sizeof(client_addr);
          int client_fd =
              accept(tcp_sock, (struct sockaddr *)&client_addr, &client_len);

          if (client_fd >= 0) {
            // Ensure the client socket is completely blocking for this quick
            // exchange
            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);

            std::cout << "[TCP] Accepted request for missing packet\n";

            protocol::RetransmitRequest req;
            ssize_t bytes_read =
                recv(client_fd, &req, sizeof(req), MSG_WAITALL);
            if (bytes_read == sizeof(protocol::RetransmitRequest)) {
              std::cout << "[TCP] Client requested seq="
                        << req.missed_sequence_num << "\n";

              protocol::TickPacket recovery_tick;
              if (ring_buffer.get(req.missed_sequence_num, recovery_tick)) {
                send(client_fd, &recovery_tick, sizeof(recovery_tick), 0);
                std::cout << "[TCP] Sent missing packet back to client\n";
              } else {
                std::cerr
                    << "[TCP] Requested packet no longer in ring buffer!\n";
              }
            } else {
              std::cerr << "[TCP] Failed to read RetransmitRequest size. Read: "
                        << bytes_read << "\n";
            }
            close(client_fd);
          }
        }
      });
    }
  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
