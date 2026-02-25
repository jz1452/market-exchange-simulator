# Financial Exchange & Trading Simulator

A low-latency, distributed market data simulator and algorithmic trading engine built in C++20. 

This project simulates a high-frequency financial exchange that broadcasts market equity ticks via UDP Multicast, paired with a client-side trading engine that ingests the feed, guarantees packet recovery via TCP, and executes a systematic mean reversion strategy.

## How It Works

The project is divided into two continuous concurrent components: an **Exchange (Publisher)** generating data, and a **Trading Client (Subscriber)** systematically acting on it.

### 1. Market Generation (The Publisher)
The Publisher acts as a live financial exchange, continuously broadcasting real-time equity ticks over UDP Multicast.
- **Market Behavior:** It replicates a volatile, non-stationary market using a Log-Normal random walk. 
  - **Standard Ticks:** Prices fluctuate by a randomly chosen float between **-0.2% and +0.2%** per tick, chosen via a uniform distribution.
  - **Momentary Anomalies (Temporary):** 
    - **Flash Crashes (1.0% chance):** Sudden panic selling drops the price by 1.5% to 3.0%, rubber-banding back to normal the next tick.
    - **Flash Spikes (0.5% chance):** Brief liquidity vacuums spike the price upward by 0.5% to 1.5%, reverting immediately.
  - **Fundamental Shifts (Permanent):** 
    - **Structural Collapse (0.2% chance):** Simulates bad earnings or scandals, permanently dropping the stock's baseline value by 4.0% to 7.0%.
    - **Breakout Surge (0.1% chance):** Simulates acquisition speculation or breakthroughs, permanently raising the stock's baseline value by 2.0% to 4.0%.
- **Resilience:** Alongside the UDP broadcast, the Publisher runs a secondary TCP server to fulfill retransmission requests if the client drops any UDP packets.

### 2. Data Ingestion (The Subscriber)
The Subscriber listens to the UDP Multicast feed to ingest market data.
- **Live Terminal Output:** As data streams in, the Subscriber logs live performance metrics to the terminal every second, showing precise throughput and 60µs wire-to-wire processing latency.
- **Packet Recovery:** gapless data reception is guaranteed using a `RingBuffer` and TCP connection to recover any dropped sequence numbers.

### 3. The Trading Strategy
The Subscriber executes a mean reversion strategy directly against the real-time feed:
- **Initialisation (100-Tick Warmup):** Before any trades are executed, the Subscriber collects the first 100 ticks for a given stock. This builds a statistically significant baseline, allowing it to calculate the initial simple moving average and standard deviation.
- **Active Execution:** Once initialised, the trading engine dynamically calculates $2\sigma$ Bollinger Bands for every incoming tick.
  - **BUY (Oversold):** If a stock's price drops below the $-2\sigma$ lower band, a BUY order is executed.
  - **SELL (Take Profit):** The position is closed for a profit the moment the stock price reverts back up to the **Simple Moving Average (SMA)**.
  - **SELL (Stop Loss):** If the price completely collapses below Entry Price $- 3\sigma$, the position is aggressively liquidated.
  - **SELL (Time Stop):** If the position is held for 50 market ticks without reverting to the mean, it is liquidated to free up capital.
- **Strategy Visualisation:** Every trade execution, price, and Bollinger Band calculation is printed live to the terminal alongside the performance metrics, allowing you to watch the strategy adapt and trade against the non-stationary market in real time.

## Technology Stack
- **Language:** C++20
- **Networking:** POSIX Sockets (UDP Multicast, TCP)
- **Concurrency:** `kqueue` Asynchronous Event Loop

## Build and Run Instructions

### Prerequisites
- CMake (3.10+)
- A C++20 compatible compiler (Clang/GCC)
- macOS (Due to `kqueue`)

### Building the Project
```bash
mkdir build
cd build
cmake ..
make
```

### Running the Simulator
You must run the Publisher and Subscriber in separate terminal windows.

**Terminal 1 (Start the Exchange):**
```bash
./build/publisher
```

**Terminal 2 (Start the Trading Client):**
```bash
./build/subscriber
```

