# High-Frequency Trading Limit Order Book

This project implements a basic production-style limit order book in modern C++ (C++17). The goal is to model the core mechanics of a matching engine used in electronic markets: storing buy and sell interest, matching orders at the best prices, and supporting cancellation by order ID.

## Overview

A limit order book maintains all active buy and sell orders sorted by price. When a new order arrives, the engine checks whether it can match against the opposite side. If the incoming order is a buy, it matches against the lowest ask price. If the incoming order is a sell, it matches against the highest bid price. Any remainder is stored in the book and waits for a future match.

## Architecture

The implementation is intentionally simple but production-oriented in structure:

- `Order` stores the order metadata:
  - `id`: unique order identifier
  - `price`: limit price
  - `quantity`: order size
  - `is_buy`: true for buy orders, false for sell orders

- `OrderBook` manages two price levels:
  - `bids_` uses `std::map<double, std::list<Order>, std::greater<double>>`
  - `asks_` uses `std::map<double, std::list<Order>, std::less<double>>`

This keeps price levels ordered automatically and makes the best bid/ask easy to access in O(log N) time for the map lookup.

## Data structures used

### 1. std::map for price priority

`std::map` provides a sorted associative container. The book uses:

- bids sorted descending so the best bid is at the beginning
- asks sorted ascending so the best ask is at the beginning

This makes price-based matching efficient and straightforward.

### 2. std::list for FIFO within a price level

Each price level stores a `std::list<Order>`. This preserves insertion order at a given price and makes it natural to match the oldest resting order first, which is a common market convention.

### 3. std::unordered_map for O(1) cancel lookup

An order may be canceled by ID. To support this efficiently, the book maintains:

- `order_index_`: `unordered_map<uint64_t, std::list<Order>::iterator>`
- `order_owners_`: `unordered_map<uint64_t, std::list<Order>*>`

This supports near-constant-time cancellation without scanning the entire book.

## Matching logic

The matching flow is:

1. Validate the order quantity.
2. If incoming order is a buy, compare with the best ask.
3. If incoming order is a sell, compare with the best bid.
4. If prices cross (buy price >= ask price or sell price <= bid price), execute a trade.
5. Reduce the resting order quantity and remove it when exhausted.
6. If any quantity remains, add it as a resting order in the appropriate side.

Console logs show each trade and resting order event.

## Benchmarking

The program also includes a benchmark that generates 100,000 random buy/sell orders and measures:

- total execution time
- throughput in orders/sec
- average latency per order in microseconds

This is run in `benchmark_random_orders()` using `std::chrono::high_resolution_clock`.

## Example benchmark output

Typical output may look like this:

```text
=== Benchmark ===
Generated 100000 random orders in 42 ms
Throughput: 2380952.38 orders/sec
Latency per order: 420.000 us
```

Actual performance varies by CPU, compiler, and optimization settings. This project was compiled with C++17 and optimized with `-O2` for realistic comparisons.

## Build and run

From the project directory:

```bash
g++ -std=c++17 -O2 main.cpp -o orderbook
./orderbook
```

On Windows with MinGW:

```powershell
g++ -std=c++17 -O2 main.cpp -o orderbook.exe
.\orderbook.exe
```

## Notes

This is a simplified market model intended for learning and demonstration. Real matching engines often add:

- price-time priority enforcement
- partial-fill accounting
- market data snapshots
- thread safety for multithreaded matching
- exchange-level latency measurements
- more advanced order types such as IOC, FOK, and stop orders

## Summary

This order book demonstrates the core building blocks of an HFT matching engine using a production-style design:

- sorted price levels via `std::map`
- FIFO queues per price via `std::list`
- constant-time cancel support via `std::unordered_map`
- benchmark instrumentation using `std::chrono`
