#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>

struct Order {
    uint64_t id;
    double price;
    long long quantity;
    bool is_buy;
};

class OrderBook {
public:
    OrderBook() : next_order_id_(1) {}

    uint64_t add_order(double price, long long quantity, bool is_buy) {
        if (quantity <= 0) {
            std::cout << "Order has invalid quantity.\n";
            return 0;
        }

        Order order{next_order_id_++, price, quantity, is_buy};
        return add_order(order);
    }

    uint64_t add_order(const Order& incoming_order) {
        if (incoming_order.quantity <= 0) {
            std::cout << "Order " << incoming_order.id << " has invalid quantity.\n";
            return 0;
        }

        if (incoming_order.is_buy) {
            return match_buy(incoming_order);
        }

        return match_sell(incoming_order);
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

        if (owner->empty()) {
            if (owner == &bids_[0.0]) {
                // no-op guard; not used because price-level maps are only constructed with actual prices
            }
        }

        return true;
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

    void get_snapshot(int depth) const {
        std::cout << "\n=== Market Data Snapshot (depth=" << depth << ") ===\n";

        std::cout << "Best Bids:\n";
        int bid_count = 0;
        for (BidBook::const_iterator it = bids_.begin(); it != bids_.end() && bid_count < depth; ++it, ++bid_count) {
            const double& price = it->first;
            const std::list<Order>& orders = it->second;
            long long total_volume = 0;
            int order_count = 0;
            for (const auto& order : orders) {
                total_volume += order.quantity;
                ++order_count;
            }
            std::cout << "  " << std::fixed << std::setprecision(2) << price
                      << " | Vol " << total_volume << " | Orders " << order_count << "\n";
        }

        std::cout << "Best Asks:\n";
        int ask_count = 0;
        for (AskBook::const_iterator it = asks_.begin(); it != asks_.end() && ask_count < depth; ++it, ++ask_count) {
            const double& price = it->first;
            const std::list<Order>& orders = it->second;
            long long total_volume = 0;
            int order_count = 0;
            for (const auto& order : orders) {
                total_volume += order.quantity;
                ++order_count;
            }
            std::cout << "  " << std::fixed << std::setprecision(2) << price
                      << " | Vol " << total_volume << " | Orders " << order_count << "\n";
        }

        std::cout << "============================\n";
    }

private:
    using BidBook = std::map<double, std::list<Order>, std::greater<double>>;
    using AskBook = std::map<double, std::list<Order>, std::less<double>>;

    BidBook bids_;
    AskBook asks_;
    std::unordered_map<uint64_t, std::list<Order>::iterator> order_index_;
    std::unordered_map<uint64_t, std::list<Order>*> order_owners_;
    uint64_t next_order_id_;

    uint64_t match_buy(const Order& incoming_order) {
        Order remaining = incoming_order;

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
            add_resting_order(remaining, &bids_[remaining.price]);
            std::cout << "ORDER REST: BUY " << remaining.id << " left resting " << remaining.quantity
                      << " @ " << std::fixed << std::setprecision(2) << remaining.price << "\n";
        }

        return incoming_order.id;
    }

    uint64_t match_sell(const Order& incoming_order) {
        Order remaining = incoming_order;

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
            add_resting_order(remaining, &asks_[remaining.price]);
            std::cout << "ORDER REST: SELL " << remaining.id << " left resting " << remaining.quantity
                      << " @ " << std::fixed << std::setprecision(2) << remaining.price << "\n";
        }

        return incoming_order.id;
    }

    void add_resting_order(const Order& resting_order, std::list<Order>* level) {
        level->push_back(resting_order);
        auto it = std::prev(level->end());
        order_index_[resting_order.id] = it;
        order_owners_[resting_order.id] = level;
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

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < order_count; ++i) {
        const double price = price_dist(rng);
        const long long quantity = qty_dist(rng);
        const bool is_buy = side_dist(rng) == 0;
        book.add_order(price, quantity, is_buy);
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

    std::cout << "\n--- Price-Time Priority / Partial Fill Demo ---\n";
    uint64_t buy_1 = book.add_order(100.00, 10, true);
    uint64_t buy_2 = book.add_order(100.00, 8, true);
    uint64_t sell_1 = book.add_order(99.50, 12, false);
    uint64_t sell_2 = book.add_order(99.50, 6, false);

    std::cout << "\nCancel order " << buy_2 << ": " << (book.cancel_order(buy_2) ? "CANCELLED" : "NOT_FOUND") << "\n";
    book.get_snapshot(5);

    std::cout << "\n--- Partial Fill / Queue Retention Demo ---\n";
    uint64_t aggressor_buy = book.add_order(100.20, 5, true);
    (void)aggressor_buy;
    std::cout << "\nAfter partial fill demo:\n";
    book.get_snapshot(5);

    std::cout << "\nCancel order " << sell_1 << ": " << (book.cancel_order(sell_1) ? "CANCELLED" : "NOT_FOUND") << "\n";

    benchmark_random_orders(book, 100000);
    return 0;
}
