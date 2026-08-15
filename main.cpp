#include <algorithm>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <string>
#include <utility>

struct Order {
    long long id;
    double price;
    long long quantity;
    bool is_buy;
};

class OrderBook {
public:
    void add_order(const Order& incoming_order) {
        if (incoming_order.quantity <= 0) {
            std::cout << "Order " << incoming_order.id << " has invalid quantity.\n";
            return;
        }

        if (incoming_order.is_buy) {
            match_buy(incoming_order);
        } else {
            match_sell(incoming_order);
        }
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

private:
    using BidBook = std::map<double, std::list<Order>, std::greater<double>>;
    using AskBook = std::map<double, std::list<Order>, std::less<double>>;

    BidBook bids_;
    AskBook asks_;

    void match_buy(const Order& incoming_order) {
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
                best_ask_list.pop_front();
            }

            if (best_ask_list.empty()) {
                asks_.erase(best_ask_price);
            }
        }

        if (remaining.quantity > 0) {
            bids_[remaining.price].push_back(remaining);
            std::cout << "ORDER REST: BUY " << remaining.id << " left resting " << remaining.quantity
                      << " @ " << std::fixed << std::setprecision(2) << remaining.price << "\n";
        }
    }

    void match_sell(const Order& incoming_order) {
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
                best_bid_list.pop_front();
            }

            if (best_bid_list.empty()) {
                bids_.erase(best_bid_price);
            }
        }

        if (remaining.quantity > 0) {
            asks_[remaining.price].push_back(remaining);
            std::cout << "ORDER REST: SELL " << remaining.id << " left resting " << remaining.quantity
                      << " @ " << std::fixed << std::setprecision(2) << remaining.price << "\n";
        }
    }
};

int main() {
    OrderBook book;

    std::cout << "Simple Limit Order Book Demo\n";

    book.add_order({1, 100.00, 10, true});
    book.add_order({2, 99.50, 8, false});
    book.add_order({3, 101.00, 5, true});
    book.add_order({4, 100.50, 12, false});
    book.add_order({5, 100.80, 7, true});
    book.add_order({6, 100.20, 18, false});
    book.add_order({7, 99.80, 4, true});

    book.print_book();
    return 0;
}
