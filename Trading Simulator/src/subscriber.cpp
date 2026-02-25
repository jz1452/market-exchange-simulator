#include "networking.hpp"
#include "protocol.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

const std::string MULTICAST_IP = "224.0.0.1";
const int MULTICAST_PORT = 30001;
const std::string PUBLISHER_IP = "127.0.0.1";
const int TCP_PORT = 40001;

// Strategy State: SMA (Simple Moving Average)
const int SMA_PERIOD = 100;
struct SymbolState {
  std::vector<double> prices;
  int idx = 0;
  double sum = 0.0;
  int position = 0; // 0 = flat, 1 = bought
  double entry_price = 0.0;
  double pnl = 0.0;
  int trades = 0;
  int ticks_held = 0;
};

std::atomic<bool> keep_running{true};
double total_session_pnl = 0.0;
std::unordered_map<std::string, SymbolState> strategy_state;

void signal_handler(int signum) {
  std::cout
      << "\n\n[TRADING ENGINE] Shutting down... Generating Final Report\n";
  std::cout << "=========================================================\n";

  // Close any open positions at the last known price to calculate final score
  double mtm_pnl = 0.0;
  for (const auto &[sym, state] : strategy_state) {
    if (state.position == 1 && !state.prices.empty()) {
      // The last price inserted into the ring buffer is current market value
      int last_inserted_idx =
          (state.idx == 0) ? state.prices.size() - 1 : state.idx - 1;
      double current_market_price = state.prices[last_inserted_idx];

      double unrealised_profit =
          (current_market_price - state.entry_price) * 100.0;
      mtm_pnl += unrealised_profit;

      std::cout << "  Open Position: " << sym << " (Bought @ $"
                << state.entry_price << ", Current @ $" << current_market_price
                << ") -> Unrealised: $" << unrealised_profit << "\n";
    }
  }

  std::cout << "---------------------------------------------------------\n";
  std::cout << "REALISED PnL:   $" << total_session_pnl << "\n";
  std::cout << "UNREALISED PnL: $" << mtm_pnl << "\n";
  std::cout << "TOTAL NET PnL:  $" << (total_session_pnl + mtm_pnl) << "\n";
  std::cout << "=========================================================\n";
  exit(signum);
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::cout << "Starting Trading Simulation Engine...\n";

  try {
    int udp_sock =
        networking::create_udp_multicast_receiver(MULTICAST_IP, MULTICAST_PORT);
    std::cout << "[UDP] Listening on " << MULTICAST_IP << ":" << MULTICAST_PORT
              << "\n";

    uint64_t expected_seq = 0;

    // Performance Metrics
    uint64_t ticks_received_this_sec = 0;
    double min_lat = 1e9, max_lat = 0, sum_lat = 0;
    auto last_report_time = std::chrono::steady_clock::now();
    protocol::TickPacket last_recv_tick{};

    while (keep_running) {
      protocol::TickPacket tick;
      sockaddr_in sender_addr{};
      socklen_t sender_len = sizeof(sender_addr);

      ssize_t received =
          recvfrom(udp_sock, &tick, sizeof(protocol::TickPacket), 0,
                   (struct sockaddr *)&sender_addr, &sender_len);

      if (received == sizeof(protocol::TickPacket)) {

        if (expected_seq != 0 && tick.sequence_num > expected_seq) {
          std::cout << "\n[!] GAP DETECTED! Expected " << expected_seq
                    << ", got " << tick.sequence_num << "\n";

          // Recover missing packet via TCP
          for (uint64_t missed_seq = expected_seq;
               missed_seq < tick.sequence_num; ++missed_seq) {
            try {
              int tcp_sock =
                  networking::connect_tcp_client(PUBLISHER_IP, TCP_PORT);

              protocol::RetransmitRequest req{missed_seq};
              send(tcp_sock, &req, sizeof(req), 0);

              protocol::TickPacket recovered_tick;
              ssize_t bytes_recv =
                  recv(tcp_sock, &recovered_tick, sizeof(recovered_tick), 0);

              if (bytes_recv == sizeof(protocol::TickPacket)) {
                std::cout << "[TCP] Successfully RECOVERED seq="
                          << recovered_tick.sequence_num
                          << " price=" << recovered_tick.price << "\n";
              } else {
                std::cerr << "[TCP] Failed to recover seq=" << missed_seq
                          << "\n";
              }
              close(tcp_sock);

            } catch (const std::exception &e) {
              std::cerr << "[TCP] Recovery connection failed: " << e.what()
                        << "\n";
            }
          }
        }

        uint64_t now_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch())
                .count();
        double latency_us = (now_ns - tick.timestamp) / 1000.0;

        ticks_received_this_sec++;
        if (latency_us < min_lat)
          min_lat = latency_us;
        if (latency_us > max_lat)
          max_lat = latency_us;
        sum_lat += latency_us;
        last_recv_tick = tick;

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now -
                                                             last_report_time)
                .count() >= 1) {
          double avg_lat = sum_lat / ticks_received_this_sec;
          std::cout << "[METRICS] " << ticks_received_this_sec
                    << " msgs/sec | Latency (us): Min=" << min_lat
                    << " Max=" << max_lat << " Avg=" << avg_lat
                    << " | Last: " << last_recv_tick.symbol << " @ "
                    << last_recv_tick.price << "\n";

          ticks_received_this_sec = 0;
          min_lat = 1e9;
          max_lat = 0;
          sum_lat = 0;
          last_report_time = now;
        }

        // next expected seq
        expected_seq = tick.sequence_num + 1;

        // Trading strat: statistical arbitrage (mean reversion)
        // Maintain the moving average for the stock
        std::string sym(tick.symbol);
        SymbolState &state = strategy_state[sym];

        if (state.prices.size() < SMA_PERIOD) {
          state.prices.push_back(tick.price);
          state.sum += tick.price;
        } else {
          state.sum -= state.prices[state.idx];
          state.prices[state.idx] = tick.price;
          state.sum += tick.price;
          state.idx = (state.idx + 1) % SMA_PERIOD;
        }

        if (state.prices.size() == SMA_PERIOD) {
          double current_sma = state.sum / SMA_PERIOD;

          // Calculate Standard Deviation (Bollinger Bands)
          double variance = 0.0;
          for (double p : state.prices) {
            variance += (p - current_sma) * (p - current_sma);
          }
          variance /= SMA_PERIOD;
          double std_dev = std::sqrt(variance);

          // Round the standard deviation so it doesn't get ridiculously small
          // in completely silent markets
          if (std_dev < 0.10)
            std_dev = 0.10;

          // If we have no position, look for a dip below the Bollinger Band (-2
          // Standard Deviations)
          if (state.position == 0 &&
              tick.price <= current_sma - (2.0 * std_dev)) {
            state.position = 1;
            state.entry_price = tick.price;
            state.ticks_held = 0;
            std::cout << "\033[1;32m[STRATEGY] BUY 100 " << sym << " @ $"
                      << tick.price << " (SMA: $" << current_sma
                      << ", 2\u03c3: $" << (2.0 * std_dev) << ")\033[0m\n";
          }
          // If we have stock, manage the open position
          else if (state.position == 1) {
            state.ticks_held++;

            // Exit 1: take profit (mean reversion)
            if (tick.price >= current_sma) {
              double profit = (tick.price - state.entry_price) * 100.0;
              state.pnl += profit;
              total_session_pnl += profit;
              state.position = 0;
              state.trades++;
              std::cout << "\033[1;36m[STRATEGY] SELL (TAKE PROFIT) 100 " << sym
                        << " @ $" << tick.price << " (Profit: $" << profit
                        << ")\033[0m\n";
            }
            // Exit 2: hard stop loss (The price just keeps plummeting)
            else if (tick.price <= state.entry_price - (3.0 * std_dev) &&
                     state.ticks_held > 2) {
              double loss = (tick.price - state.entry_price) * 100.0;
              state.pnl += loss;
              total_session_pnl += loss;
              state.position = 0;
              state.trades++;
              std::cout << "\033[1;31m[STRATEGY] SELL (STOP LOSS) 100 " << sym
                        << " @ $" << tick.price << " (Loss: $" << loss
                        << ")\033[0m\n";
            }
            // Exit 3: time stop loss (stock is not rising back to mean)
            else if (state.ticks_held > 50) {
              double loss = (tick.price - state.entry_price) * 100.0;
              state.pnl += loss;
              total_session_pnl += loss;
              state.position = 0;
              state.trades++;
              std::cout << "\033[1;33m[STRATEGY] SELL (TIME STOP) 100 " << sym
                        << " @ $" << tick.price << " (PnL: $" << loss
                        << ")\033[0m\n";
            }
          }
        }

      } else if (received < 0) {
        std::cerr << "UDP Receive failed\n";
        break;
      }
    }

  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
