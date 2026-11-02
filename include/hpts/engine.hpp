#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace hpts {

enum class Side { Buy, Sell };
enum class CommandType { Submit, Cancel, Replace, KillSwitch };
enum class EventType { Accepted, Rejected, Cancelled, Replaced, Trade, KillSwitch, BookUpdate };

struct Command {
    CommandType type = CommandType::Submit;
    std::uint64_t command_id = 0;
    std::string account_id;
    std::uint64_t order_id = 0;
    Side side = Side::Buy;
    std::int64_t price = 0;
    // Replacement quantity is the desired NEW REMAINING quantity, not lifetime quantity.
    std::int64_t quantity = 0;
    bool enabled = false;
};

struct AccountConfig {
    std::string id;
    std::int64_t cash = 0;
    std::int64_t min_position = 0;
    std::int64_t max_position = 0;
    std::int64_t position = 0;
};

struct EngineConfig {
    std::vector<AccountConfig> accounts;
    std::size_t max_active_orders = 100000;
    std::size_t max_order_ids = 1000000;
    std::size_t max_commands = 1000000;
    std::size_t max_accounts = 1024;
    std::size_t max_account_id_length = 64;
    std::int64_t max_price = (std::numeric_limits<std::int64_t>::max)();
    std::int64_t max_quantity = (std::numeric_limits<std::int64_t>::max)();
};

struct Event {
    EventType type = EventType::Rejected;
    // For trades, order_id/account_id are the taker; maker fields are the resting order.
    std::uint64_t order_id = 0;
    std::uint64_t maker_order_id = 0;
    std::string account_id;
    std::string maker_account_id;
    Side side = Side::Buy;
    std::int64_t price = 0;
    std::int64_t quantity = 0;
    std::int64_t best_bid = 0;
    std::int64_t best_ask = 0;
    bool enabled = false;
    std::string reason;
};

struct Outcome {
    std::uint64_t sequence = 0;
    std::uint64_t command_id = 0;
    bool accepted = false;
    std::string reason;
    std::vector<Event> events;
    [[nodiscard]] std::string canonical() const;
};

struct AccountSnapshot {
    std::string id;
    std::int64_t cash = 0;
    std::int64_t min_position = 0;
    std::int64_t max_position = 0;
    std::int64_t position = 0;
    std::int64_t reserved_cash = 0;
    std::int64_t reserved_buy_quantity = 0;
    std::int64_t reserved_sell_quantity = 0;
};

struct OrderSnapshot {
    std::uint64_t id = 0;
    std::string account_id;
    Side side = Side::Buy;
    std::int64_t price = 0;
    std::int64_t remaining = 0;
    std::uint64_t priority = 0;
};

struct Snapshot {
    std::uint64_t sequence = 0;
    bool kill_switch = false;
    // Sorted account IDs, order IDs, and ledger IDs make serialization deterministic.
    std::vector<AccountSnapshot> accounts;
    std::vector<OrderSnapshot> orders;
    std::vector<std::uint64_t> seen_command_ids;
    std::vector<std::uint64_t> used_order_ids;
    [[nodiscard]] std::string canonical() const;
    // Stable FNV-1a fingerprint, not a cryptographic integrity guarantee.
    [[nodiscard]] std::string digest() const;
};

class Engine {
public:
    // Accounts need nonnegative cash with a representable total cash sum. Cash is
    // conserved, so no later seller credit can exceed the representable total.
    explicit Engine(EngineConfig config);
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Single-writer API. Every call consumes a sequence; a new valid command ID
    // consumes a dedup entry even on business rejection. Ledgers never evict.
    [[nodiscard]] Outcome process(const Command& command);
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] std::vector<std::string> invariant_errors() const;
    [[nodiscard]] const EngineConfig& config() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string canonical_config(const EngineConfig& config);
[[nodiscard]] std::string to_string(Side side);
[[nodiscard]] std::string to_string(EventType type);

}  // namespace hpts
