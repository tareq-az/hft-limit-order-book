#include <algorithm>
#include <windows.h>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class OrderType {
    GTC,
    IOC,
    FOK,
    POST_ONLY
};

enum class OrderStatus {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    REJECTED
};

inline const char* order_status_to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::NEW: return "NEW";
        case OrderStatus::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case OrderStatus::FILLED: return "FILLED";
        case OrderStatus::CANCELED: return "CANCELED";
        case OrderStatus::REJECTED: return "REJECTED";
        default: return "UNKNOWN";
    }
}

struct Order {
    uint64_t id;
    double price;
    long long quantity;
    bool is_buy;
    OrderType type{OrderType::GTC};
    OrderStatus status{OrderStatus::NEW};
};

struct LevelSnapshot {
    double price = 0.0;
    long long total_volume = 0;
    int order_count = 0;
};

struct SnapshotData {
    std::vector<LevelSnapshot> bids;
    std::vector<LevelSnapshot> asks;
};

class OrderBook {
public:
    OrderBook() : next_order_id_(1) {}

    uint64_t add_order(double price, long long quantity, bool is_buy, OrderType type = OrderType::GTC) {
        if (quantity <= 0) {
            std::cout << "Order has invalid quantity.\n";
            return 0;
        }

        Order order{next_order_id_++, price, quantity, is_buy, type, OrderStatus::NEW};
        return add_order(order);
    }

    uint64_t add_order(const Order& incoming_order) {
        if (incoming_order.quantity <= 0) {
            std::cout << "Order " << incoming_order.id << " has invalid quantity.\n";
            return 0;
        }

        if (incoming_order.type == OrderType::POST_ONLY) {
            if (incoming_order.is_buy && !asks_.empty() && incoming_order.price >= asks_.begin()->first) {
                std::cout << "POST_ONLY: BUY " << incoming_order.id << " rejected because it would match immediately.\n";
                update_order_status(incoming_order.id, OrderStatus::REJECTED);
                export_json("orderbook_data.json");
                return incoming_order.id;
            }
            if (!incoming_order.is_buy && !bids_.empty() && incoming_order.price <= bids_.begin()->first) {
                std::cout << "POST_ONLY: SELL " << incoming_order.id << " rejected because it would match immediately.\n";
                update_order_status(incoming_order.id, OrderStatus::REJECTED);
                export_json("orderbook_data.json");
                return incoming_order.id;
            }
        }

        uint64_t result = 0;
        if (incoming_order.is_buy) {
            result = match_buy(incoming_order);
        } else {
            result = match_sell(incoming_order);
        }

        export_json("orderbook_data.json");
        return result;
    }

    bool cancel_order(uint64_t order_id) {
        auto index_it = order_index_.find(order_id);
        if (index_it == order_index_.end()) {
            return false;
        }

        std::list<Order>* owner = order_owners_[order_id];
        if (owner == nullptr) {
            order_index_.erase(index_it);
            return false;
        }

        owner->erase(index_it->second);
        order_index_.erase(index_it);
        order_owners_.erase(order_id);
        update_order_status(order_id, OrderStatus::CANCELED);
        export_json("orderbook_data.json");

        return true;
    }

    void export_json(const std::string& filename) const {
        std::ofstream out(filename);
        if (!out.is_open()) {
            std::cerr << "Failed to open JSON export file: " << filename << "\n";
            return;
        }

        LevelSnapshot best_bid = get_best_bid();
        LevelSnapshot best_ask = get_best_ask();

        out << "{\n";
        out << "  \"best_bid\": {\"price\": " << std::fixed << std::setprecision(2) << best_bid.price
            << ", \"quantity\": " << best_bid.total_volume << "},\n";
        out << "  \"best_ask\": {\"price\": " << std::fixed << std::setprecision(2) << best_ask.price
            << ", \"quantity\": " << best_ask.total_volume << "},\n";
        out << "  \"bids\": [\n";

        int bid_index = 0;
        for (BidBook::const_iterator it = bids_.begin(); it != bids_.end(); ++it) {
            long long total_qty = 0;
            for (const auto& order : it->second) {
                total_qty += order.quantity;
            }

            out << "    {\"price\": " << std::fixed << std::setprecision(2) << it->first
                << ", \"quantity\": " << total_qty << "}";
            if (++bid_index < static_cast<int>(std::distance(bids_.begin(), std::next(it, 1)))) {
                // no-op; fallback is handled by iterator count below
            }
            if (std::next(it) != bids_.end()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";

        out << "  \"asks\": [\n";
        int ask_index = 0;
        for (AskBook::const_iterator it = asks_.begin(); it != asks_.end(); ++it) {
            long long total_qty = 0;
            for (const auto& order : it->second) {
                total_qty += order.quantity;
            }

            out << "    {\"price\": " << std::fixed << std::setprecision(2) << it->first
                << ", \"quantity\": " << total_qty << "}";
            if (std::next(it) != asks_.end()) {
                out << ",";
            }
            out << "\n";
            ++ask_index;
        }
        out << "  ]\n";
        out << "}\n";

        out.close();
    }

    void print_book() const {
        std::cout << "\n=== Order Book ===\n";
        std::cout << "Bids (best first):\n";
        for (BidBook::const_iterator it = bids_.begin(); it != bids_.end(); ++it) {
            const double& price = it->first;
            const std::list<Order>& orders = it->second;
            std::cout << "  " << std::fixed << std::setprecision(2) << price
                      << " x " << std::accumulate(orders.begin(), orders.end(), 0LL,
                                                  [](long long total, const Order& o) {
                                                      return total + o.quantity;
                                                  })
                      << "\n";
        }

        std::cout << "Asks (best first):\n";
        for (AskBook::const_iterator it = asks_.begin(); it != asks_.end(); ++it) {
            const double& price = it->first;
            const std::list<Order>& orders = it->second;
            std::cout << "  " << std::fixed << std::setprecision(2) << price
                      << " x " << std::accumulate(orders.begin(), orders.end(), 0LL,
                                                  [](long long total, const Order& o) {
                                                      return total + o.quantity;
                                                  })
                      << "\n";
        }
        std::cout << "==================\n";
    }

    SnapshotData get_snapshot_data(int depth) const {
        SnapshotData snapshot;
        int bid_count = 0;

        for (BidBook::const_iterator it = bids_.begin(); it != bids_.end() && bid_count < depth; ++it, ++bid_count) {
            LevelSnapshot level;
            level.price = it->first;
            for (const auto& order : it->second) {
                level.total_volume += order.quantity;
                ++level.order_count;
            }
            snapshot.bids.push_back(level);
        }

        int ask_count = 0;
        for (AskBook::const_iterator it = asks_.begin(); it != asks_.end() && ask_count < depth; ++it, ++ask_count) {
            LevelSnapshot level;
            level.price = it->first;
            for (const auto& order : it->second) {
                level.total_volume += order.quantity;
                ++level.order_count;
            }
            snapshot.asks.push_back(level);
        }

        return snapshot;
    }

    LevelSnapshot get_best_bid() const {
        if (bids_.empty()) {
            return LevelSnapshot{0.0, 0, 0};
        }

        LevelSnapshot level;
        auto it = bids_.begin();
        level.price = it->first;
        for (const auto& order : it->second) {
            level.total_volume += order.quantity;
            ++level.order_count;
        }
        return level;
    }

    LevelSnapshot get_best_ask() const {
        if (asks_.empty()) {
            return LevelSnapshot{0.0, 0, 0};
        }

        LevelSnapshot level;
        auto it = asks_.begin();
        level.price = it->first;
        for (const auto& order : it->second) {
            level.total_volume += order.quantity;
            ++level.order_count;
        }
        return level;
    }

    void get_snapshot(int depth) const {
        SnapshotData snapshot = get_snapshot_data(depth);

        std::cout << "\n=== Market Data Snapshot (depth=" << depth << ") ===\n";
        std::cout << "Best Bids:\n";
        for (const auto& level : snapshot.bids) {
            std::cout << "  " << std::fixed << std::setprecision(2) << level.price
                      << " | Vol " << level.total_volume << " | Orders " << level.order_count << "\n";
        }

        std::cout << "Best Asks:\n";
        for (const auto& level : snapshot.asks) {
            std::cout << "  " << std::fixed << std::setprecision(2) << level.price
                      << " | Vol " << level.total_volume << " | Orders " << level.order_count << "\n";
        }
        std::cout << "============================\n";
    }

    OrderStatus get_order_status(uint64_t order_id) const {
        auto it = order_status_.find(order_id);
        if (it == order_status_.end()) {
            return OrderStatus::REJECTED;
        }
        return it->second;
    }

    const char* get_order_status_string(uint64_t order_id) const {
        return order_status_to_string(get_order_status(order_id));
    }

private:
    using BidBook = std::map<double, std::list<Order>, std::greater<double>>;
    using AskBook = std::map<double, std::list<Order>, std::less<double>>;

    BidBook bids_;
    AskBook asks_;
    std::unordered_map<uint64_t, std::list<Order>::iterator> order_index_;
    std::unordered_map<uint64_t, std::list<Order>*> order_owners_;
    std::unordered_map<uint64_t, OrderStatus> order_status_;
    uint64_t next_order_id_;

    uint64_t match_buy(const Order& incoming_order) {
        Order remaining = incoming_order;

        if (incoming_order.type == OrderType::FOK) {
            if (!can_fully_fill_buy(remaining)) {
                std::cout << "FOK: BUY " << remaining.id << " rejected, not enough liquidity to fill entire quantity.\n";
                update_order_status(remaining.id, OrderStatus::REJECTED);
                return remaining.id;
            }
        }

        while (remaining.quantity > 0 && !asks_.empty() && remaining.price >= asks_.begin()->first) {
            AskBook::iterator best_ask_it = asks_.begin();
            double best_ask_price = best_ask_it->first;
            std::list<Order>& best_ask_list = best_ask_it->second;

            if (best_ask_list.empty()) {
                asks_.erase(best_ask_price);
                continue;
            }

            Order& best_ask = best_ask_list.front();
            long long trade_qty = std::min(remaining.quantity, best_ask.quantity);

            std::cout << "TRADE: BUY " << remaining.id << " matched SELL " << best_ask.id
                      << " | Qty " << trade_qty << " @ " << std::fixed << std::setprecision(2)
                      << best_ask_price << "\n";

            remaining.quantity -= trade_qty;
            best_ask.quantity -= trade_qty;
            update_order_status(best_ask.id, best_ask.quantity == 0 ? OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED);

            if (best_ask.quantity == 0) {
                remove_from_index(best_ask.id);
                best_ask_list.pop_front();
            } else {
                update_order_index(best_ask);
            }

            if (best_ask_list.empty()) {
                asks_.erase(best_ask_price);
            }
        }

        if (remaining.quantity > 0) {
            if (incoming_order.type == OrderType::IOC) {
                std::cout << "IOC: BUY " << remaining.id << " canceled " << remaining.quantity
                          << " unfilled units.\n";
                update_order_status(remaining.id, OrderStatus::CANCELED);
            } else if (incoming_order.type == OrderType::GTC || incoming_order.type == OrderType::POST_ONLY) {
                add_resting_order(remaining, &bids_[remaining.price]);
                update_order_status(remaining.id, remaining.quantity < incoming_order.quantity ? OrderStatus::PARTIALLY_FILLED : OrderStatus::NEW);
                std::cout << "ORDER REST: BUY " << remaining.id << " left resting " << remaining.quantity
                          << " @ " << std::fixed << std::setprecision(2) << remaining.price << "\n";
            }
        } else {
            update_order_status(remaining.id, OrderStatus::FILLED);
        }

        return incoming_order.id;
    }

    uint64_t match_sell(const Order& incoming_order) {
        Order remaining = incoming_order;

        if (incoming_order.type == OrderType::FOK) {
            if (!can_fully_fill_sell(remaining)) {
                std::cout << "FOK: SELL " << remaining.id << " rejected, not enough liquidity to fill entire quantity.\n";
                update_order_status(remaining.id, OrderStatus::REJECTED);
                return remaining.id;
            }
        }

        while (remaining.quantity > 0 && !bids_.empty() && remaining.price <= bids_.begin()->first) {
            BidBook::iterator best_bid_it = bids_.begin();
            double best_bid_price = best_bid_it->first;
            std::list<Order>& best_bid_list = best_bid_it->second;

            if (best_bid_list.empty()) {
                bids_.erase(best_bid_price);
                continue;
            }

            Order& best_bid = best_bid_list.front();
            long long trade_qty = std::min(remaining.quantity, best_bid.quantity);

            std::cout << "TRADE: SELL " << remaining.id << " matched BUY " << best_bid.id
                      << " | Qty " << trade_qty << " @ " << std::fixed << std::setprecision(2)
                      << best_bid_price << "\n";

            remaining.quantity -= trade_qty;
            best_bid.quantity -= trade_qty;
            update_order_status(best_bid.id, best_bid.quantity == 0 ? OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED);

            if (best_bid.quantity == 0) {
                remove_from_index(best_bid.id);
                best_bid_list.pop_front();
            } else {
                update_order_index(best_bid);
            }

            if (best_bid_list.empty()) {
                bids_.erase(best_bid_price);
            }
        }

        if (remaining.quantity > 0) {
            if (incoming_order.type == OrderType::IOC) {
                std::cout << "IOC: SELL " << remaining.id << " canceled " << remaining.quantity
                          << " unfilled units.\n";
                update_order_status(remaining.id, OrderStatus::CANCELED);
            } else if (incoming_order.type == OrderType::GTC || incoming_order.type == OrderType::POST_ONLY) {
                add_resting_order(remaining, &asks_[remaining.price]);
                update_order_status(remaining.id, remaining.quantity < incoming_order.quantity ? OrderStatus::PARTIALLY_FILLED : OrderStatus::NEW);
                std::cout << "ORDER REST: SELL " << remaining.id << " left resting " << remaining.quantity
                          << " @ " << std::fixed << std::setprecision(2) << remaining.price << "\n";
            }
        } else {
            update_order_status(remaining.id, OrderStatus::FILLED);
        }

        return incoming_order.id;
    }

    bool can_fully_fill_buy(const Order& order) const {
        long long required = order.quantity;
        for (const auto& ask_level : asks_) {
            if (order.price < ask_level.first) {
                break;
            }
            for (const auto& resting_order : ask_level.second) {
                required -= resting_order.quantity;
                if (required <= 0) {
                    return true;
                }
            }
        }
        return false;
    }

    bool can_fully_fill_sell(const Order& order) const {
        long long required = order.quantity;
        for (const auto& bid_level : bids_) {
            if (order.price > bid_level.first) {
                break;
            }
            for (const auto& resting_order : bid_level.second) {
                required -= resting_order.quantity;
                if (required <= 0) {
                    return true;
                }
            }
        }
        return false;
    }

    void add_resting_order(const Order& resting_order, std::list<Order>* level) {
        level->push_back(resting_order);
        auto it = std::prev(level->end());
        order_index_[resting_order.id] = it;
        order_owners_[resting_order.id] = level;
    }

    void update_order_status(uint64_t order_id, OrderStatus status) {
        auto it = order_status_.find(order_id);
        if (it == order_status_.end()) {
            order_status_[order_id] = status;
            return;
        }
        it->second = status;
    }

    void update_order_index(const Order& order) {
        auto owner_it = order_owners_.find(order.id);
        if (owner_it != order_owners_.end()) {
            std::list<Order>* owner = owner_it->second;
            for (auto it = owner->begin(); it != owner->end(); ++it) {
                if (it->id == order.id) {
                    order_index_[order.id] = it;
                    return;
                }
            }
        }
    }

    void remove_from_index(uint64_t order_id) {
        auto idx_it = order_index_.find(order_id);
        if (idx_it != order_index_.end()) {
            order_index_.erase(idx_it);
        }

        auto owner_it = order_owners_.find(order_id);
        if (owner_it != order_owners_.end()) {
            order_owners_.erase(owner_it);
        }
    }
};

void benchmark_random_orders(OrderBook& book, size_t order_count) {
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> price_dist(95.0, 105.0);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> qty_dist(1, 25);
    std::uniform_int_distribution<int> type_dist(0, 2);

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < order_count; ++i) {
        const double price = price_dist(rng);
        const long long quantity = qty_dist(rng);
        const bool is_buy = side_dist(rng) == 0;
        const int type_choice = type_dist(rng);
        OrderType type = OrderType::GTC;

        if (type_choice == 1) {
            type = OrderType::IOC;
        } else if (type_choice == 2) {
            type = OrderType::FOK;
        }

        book.add_order(price, quantity, is_buy, type);
    }

    auto end = std::chrono::high_resolution_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    const double elapsed_seconds = std::max(0.001, static_cast<double>(elapsed_ms) / 1000.0);
    const double throughput = static_cast<double>(order_count) / elapsed_seconds;

    std::cout << "\n=== Benchmark ===\n";
    std::cout << "Generated " << order_count << " random orders in " << elapsed_ms << " ms\n";
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) << throughput << " orders/sec\n";
    std::cout << "Latency per order: " << std::fixed << std::setprecision(3)
              << (elapsed_seconds * 1'000'000.0 / order_count) << " us\n";
}

int main() {
    OrderBook book;

    std::cout << "Production-grade Limit Order Book Demo\n";

    std::cout << "\n--- IOC / FOK / POST_ONLY / Snapshot Demo ---\n";
    auto buy1 = book.add_order(100.00, 10, true);
    auto buy2 = book.add_order(100.00, 8, true);
    auto sell1 = book.add_order(99.50, 12, false);
    auto sell2 = book.add_order(99.50, 6, false);
    (void)buy1;
    (void)buy2;
    (void)sell1;
    (void)sell2;

    std::cout << "\nPOST_ONLY rejected because it would cross:\n";
    book.add_order(99.60, 5, true, OrderType::POST_ONLY);
    std::cout << "Best bid: " << std::fixed << std::setprecision(2) << book.get_best_bid().price
              << " | Best ask: " << std::fixed << std::setprecision(2) << book.get_best_ask().price << "\n";

    std::cout << "\nIOC buy test:\n";
    auto ioc_buy = book.add_order(100.20, 25, true, OrderType::IOC);
    std::cout << "IOC buy status: " << (book.get_order_status(ioc_buy) == OrderStatus::CANCELED ? "CANCELED" : "OTHER") << "\n";
    book.get_snapshot(5);

    std::cout << "\nFOK sell test:\n";
    auto fok_sell = book.add_order(99.80, 50, false, OrderType::FOK);
    std::cout << "FOK sell status: " << (book.get_order_status(fok_sell) == OrderStatus::REJECTED ? "REJECTED" : "OTHER") << "\n";
    book.get_snapshot(5);

    std::cout << "\n--- Partial Fill / Queue Retention Demo ---\n";
    auto buy_id = book.add_order(101.00, 15, true);
    auto sell_id = book.add_order(100.50, 20, false);
    std::cout << "Buy status: " << (book.get_order_status(buy_id) == OrderStatus::NEW ? "NEW" : "OTHER") << "\n";
    std::cout << "Sell status: " << (book.get_order_status(sell_id) == OrderStatus::PARTIALLY_FILLED ? "PARTIALLY_FILLED" : "OTHER") << "\n";
    book.get_snapshot(5);

    std::cout << "\nCancel order " << buy_id << ": " << (book.cancel_order(buy_id) ? "CANCELLED" : "NOT_FOUND") << "\n";
    std::cout << "Order " << buy_id << " status after cancel: " << (book.get_order_status(buy_id) == OrderStatus::CANCELED ? "CANCELED" : "OTHER") << "\n";

    std::cout << "\n--- Live JSON Export Simulation ---\n";
    std::mt19937_64 rng(123);
    std::uniform_real_distribution<double> live_price_dist(98.0, 104.0);
    std::uniform_int_distribution<int> live_qty_dist(1, 12);
    std::uniform_int_distribution<int> live_side_dist(0, 1);

    for (int i = 0; i < 8; ++i) {
        const bool is_buy = live_side_dist(rng) == 0;
        const double price = live_price_dist(rng);
        const long long qty = live_qty_dist(rng);
        book.add_order(price, qty, is_buy, OrderType::GTC);
        std::cout << "Live update " << i + 1 << ": exported to orderbook_data.json\n";
        Sleep(200);
    }

    benchmark_random_orders(book, 100000);
    return 0;
}
