#pragma once

#include "hpts/engine.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace reference {

using Trade = std::tuple<std::uint64_t, std::uint64_t, std::int64_t, std::int64_t>;

struct Result {
    bool accepted = false;
    std::vector<Trade> trades;
};

// Deliberately slow oracle: unordered vector orders, full scans for lookup,
// sorting for every match, and reservations recomputed from active orders.
// It shares public value types with the engine, but none of its algorithms.
class Model {
public:
    explicit Model(hpts::EngineConfig config) : config_(std::move(config)) {
        for (const auto& account : config_.accounts) {
            accounts_.push_back({account.id, account.cash, account.min_position,
                                 account.max_position, account.position, 0, 0, 0});
        }
    }

    Result process(const hpts::Command& command) {
        ++sequence_;
        if (command.command_id == 0 || seen_.contains(command.command_id) ||
            seen_.size() == config_.max_commands) {
            return {};
        }
        seen_.insert(command.command_id);
        if (command.type == hpts::CommandType::KillSwitch) {
            killed_ = command.enabled;
            return {true, {}};
        }
        auto account = find_account(command.account_id);
        if (account == accounts_.end()) {
            return {};
        }
        if (command.type == hpts::CommandType::Submit) {
            if (command.order_id == 0 || used_.contains(command.order_id) ||
                used_.size() == config_.max_order_ids || killed_) {
                return {};
            }
            auto result = submit(command.order_id, command.account_id, command.side,
                                 command.price, command.quantity, nullptr);
            if (result.accepted) {
                used_.insert(command.order_id);
            }
            return result;
        }
        auto order = find_order(command.order_id);
        if (order == orders_.end() || order->account_id != command.account_id) {
            return {};
        }
        if (command.type == hpts::CommandType::Cancel) {
            orders_.erase(order);
            return {true, {}};
        }
        if (command.type != hpts::CommandType::Replace) {
            return {};
        }
        const auto original = *order;
        if (killed_ && (command.price != original.price ||
                        command.quantity > original.remaining)) {
            return {};
        }
        return submit(original.id, original.account_id, original.side,
                      command.price, command.quantity, &original);
    }

    hpts::Snapshot snapshot() const {
        hpts::Snapshot result;
        result.sequence = sequence_;
        result.kill_switch = killed_;
        result.accounts = accounts_;
        result.orders = orders_;
        result.seen_command_ids.assign(seen_.begin(), seen_.end());
        result.used_order_ids.assign(used_.begin(), used_.end());
        for (auto& account : result.accounts) {
            for (const auto& order : orders_) {
                if (order.account_id != account.id) {
                    continue;
                }
                if (order.side == hpts::Side::Buy) {
                    account.reserved_cash += order.price * order.remaining;
                    account.reserved_buy_quantity += order.remaining;
                } else {
                    account.reserved_sell_quantity += order.remaining;
                }
            }
        }
        return result;
    }

private:
    using Accounts = std::vector<hpts::AccountSnapshot>;
    using Orders = std::vector<hpts::OrderSnapshot>;

    Accounts::iterator find_account(const std::string& id) {
        return std::find_if(accounts_.begin(), accounts_.end(),
                            [&](const auto& account) { return account.id == id; });
    }

    Orders::iterator find_order(std::uint64_t id) {
        return std::find_if(orders_.begin(), orders_.end(),
                            [&](const auto& order) { return order.id == id; });
    }

    static bool add(std::int64_t a, std::int64_t b, std::int64_t& result) {
        constexpr auto high = (std::numeric_limits<std::int64_t>::max)();
        constexpr auto low = (std::numeric_limits<std::int64_t>::min)();
        if ((b > 0 && a > high - b) || (b < 0 && a < low - b)) {
            return false;
        }
        result = a + b;
        return true;
    }

    bool risk_ok(const std::string& owner, hpts::Side side, std::int64_t price,
                 std::int64_t quantity, std::uint64_t excluded_id) {
        constexpr auto high = (std::numeric_limits<std::int64_t>::max)();
        auto account = find_account(owner);
        std::int64_t buys = 0;
        std::int64_t sells = 0;
        std::int64_t reserved = 0;
        for (const auto& order : orders_) {
            if (order.id == excluded_id || order.account_id != owner) {
                continue;
            }
            if (order.side == hpts::Side::Buy) {
                if (!add(buys, order.remaining, buys) ||
                    !add(reserved, order.remaining * order.price, reserved)) {
                    return false;
                }
            } else if (!add(sells, order.remaining, sells)) {
                return false;
            }
        }
        std::int64_t exposure = 0;
        if (side == hpts::Side::Buy) {
            if (quantity > high / price || !add(buys, quantity, buys) ||
                !add(reserved, quantity * price, reserved) ||
                !add(account->position, buys, exposure)) {
                return false;
            }
            return exposure <= account->max_position && reserved <= account->cash;
        }
        if (!add(sells, quantity, sells) ||
            !add(account->position, -sells, exposure)) {
            return false;
        }
        return exposure >= account->min_position;
    }

    Result submit(std::uint64_t id, const std::string& owner, hpts::Side side,
                  std::int64_t price, std::int64_t quantity,
                  const hpts::OrderSnapshot* original) {
        constexpr auto high = (std::numeric_limits<std::int64_t>::max)();
        if (price <= 0 || quantity <= 0 || price > config_.max_price ||
            quantity > config_.max_quantity || quantity > high / price ||
            !risk_ok(owner, side, price, quantity, original ? id : 0)) {
            return {};
        }
        std::vector<hpts::OrderSnapshot> candidates;
        for (const auto& order : orders_) {
            if (order.id == (original ? id : 0) || order.side == side) {
                continue;
            }
            if ((side == hpts::Side::Buy && order.price <= price) ||
                (side == hpts::Side::Sell && order.price >= price)) {
                candidates.push_back(order);
            }
        }
        std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
            if (a.price != b.price) {
                return side == hpts::Side::Buy ? a.price < b.price : a.price > b.price;
            }
            return a.priority < b.priority;
        });
        auto remaining = quantity;
        std::size_t completed = 0;
        for (const auto& maker : candidates) {
            if (remaining == 0) {
                break;
            }
            if (maker.account_id == owner) {
                return {};
            }
            const auto filled = (std::min)(remaining, maker.remaining);
            remaining -= filled;
            completed += filled == maker.remaining ? 1 : 0;
        }
        const auto final_count = orders_.size() - (original ? 1 : 0) - completed +
                                 (remaining > 0 ? 1 : 0);
        if (final_count > config_.max_active_orders) {
            return {};
        }

        const bool retain_priority = original && price == original->price &&
                                     quantity <= original->remaining;
        const auto priority = retain_priority ? original->priority : sequence_;
        if (original) {
            orders_.erase(find_order(id));
        }
        Result result{true, {}};
        remaining = quantity;
        for (const auto& candidate : candidates) {
            if (remaining == 0) {
                break;
            }
            auto maker = find_order(candidate.id);
            const auto filled = (std::min)(remaining, maker->remaining);
            const auto notional = filled * maker->price;
            auto taker_account = find_account(owner);
            auto maker_account = find_account(maker->account_id);
            if (side == hpts::Side::Buy) {
                taker_account->cash -= notional;
                taker_account->position += filled;
                maker_account->cash += notional;
                maker_account->position -= filled;
            } else {
                taker_account->cash += notional;
                taker_account->position -= filled;
                maker_account->cash -= notional;
                maker_account->position += filled;
            }
            result.trades.emplace_back(id, maker->id, maker->price, filled);
            maker->remaining -= filled;
            remaining -= filled;
            if (maker->remaining == 0) {
                orders_.erase(maker);
            }
        }
        if (remaining > 0) {
            orders_.push_back({id, owner, side, price, remaining, priority});
        }
        return result;
    }

    hpts::EngineConfig config_;
    Accounts accounts_;
    Orders orders_;
    std::set<std::uint64_t> seen_;
    std::set<std::uint64_t> used_;
    std::uint64_t sequence_ = 0;
    bool killed_ = false;
};

}  // namespace reference
