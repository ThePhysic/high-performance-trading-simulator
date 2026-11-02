#include "hpts/engine.hpp"
#include "reference_model.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace hpts;

void check(bool condition, const std::string& description) {
    if (!condition) {
        throw std::runtime_error(description);
    }
}

EngineConfig defaults(std::int64_t cash = 1000000, std::int64_t limit = 100) {
    EngineConfig config;
    for (const auto* id : {"A", "B", "C", "D"}) {
        config.accounts.push_back({id, cash, -limit, limit, 0});
    }
    return config;
}

Command submit(std::uint64_t command_id, std::uint64_t order_id,
               const std::string& owner, Side side, std::int64_t price,
               std::int64_t quantity) {
    return {CommandType::Submit, command_id, owner, order_id, side, price, quantity, false};
}

Command cancel(std::uint64_t command_id, std::uint64_t order_id,
               const std::string& owner) {
    return {CommandType::Cancel, command_id, owner, order_id};
}

Command replace(std::uint64_t command_id, std::uint64_t order_id,
                const std::string& owner, std::int64_t price, std::int64_t quantity) {
    return {CommandType::Replace, command_id, owner, order_id, Side::Buy, price, quantity};
}

Command kill(std::uint64_t command_id, bool enabled) {
    Command command;
    command.type = CommandType::KillSwitch;
    command.command_id = command_id;
    command.enabled = enabled;
    return command;
}

std::vector<reference::Trade> trades(const Outcome& outcome) {
    std::vector<reference::Trade> result;
    for (const auto& event : outcome.events) {
        if (event.type == EventType::Trade) {
            result.emplace_back(event.order_id, event.maker_order_id,
                                event.price, event.quantity);
        }
    }
    return result;
}

std::string normalize(Snapshot snapshot, bool include_admission = true) {
    std::sort(snapshot.accounts.begin(), snapshot.accounts.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(snapshot.orders.begin(), snapshot.orders.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    std::ostringstream out;
    out << snapshot.kill_switch << '\n';
    if (include_admission) {
        out << snapshot.sequence << '\n';
        for (const auto id : snapshot.seen_command_ids) {
            out << id << ',';
        }
        out << '\n';
    }
    for (const auto id : snapshot.used_order_ids) {
        out << id << ',';
    }
    out << '\n';
    for (const auto& account : snapshot.accounts) {
        out << account.id << ' ' << account.cash << ' ' << account.min_position << ' '
            << account.max_position << ' ' << account.position << ' '
            << account.reserved_cash << ' ' << account.reserved_buy_quantity << ' '
            << account.reserved_sell_quantity << '\n';
    }
    for (const auto& order : snapshot.orders) {
        out << order.id << ' ' << order.account_id << ' ' << static_cast<int>(order.side)
            << ' ' << order.price << ' ' << order.remaining << ' ' << order.priority << '\n';
    }
    return out.str();
}

void invariant_check(const Engine& engine) {
    const auto errors = engine.invariant_errors();
    check(errors.empty(), errors.empty() ? "" : "invariant: " + errors.front());
}

Outcome accepted(Engine& engine, const Command& command) {
    auto result = engine.process(command);
    check(result.accepted, "expected accepted command " + std::to_string(command.command_id) +
                           ", got " + result.reason);
    check(result.reason == "OK", "accepted command reason must be OK");
    invariant_check(engine);
    return result;
}

Outcome rejected(Engine& engine, const Command& command, const std::string& reason = "") {
    const auto before = normalize(engine.snapshot(), false);
    auto result = engine.process(command);
    check(!result.accepted, "expected rejected command " + std::to_string(command.command_id));
    check(!result.reason.empty(), "reject must identify reason");
    if (!reason.empty()) {
        check(result.reason == reason, "expected " + reason + ", got " + result.reason);
    }
    check(result.events.size() == 1 && result.events.front().type == EventType::Rejected,
          "reject event contract");
    check(before == normalize(engine.snapshot(), false), "rejected command mutated business state");
    invariant_check(engine);
    return result;
}

OrderSnapshot order(const Engine& engine, std::uint64_t id) {
    const auto snapshot = engine.snapshot();
    const auto found = std::find_if(snapshot.orders.begin(), snapshot.orders.end(),
                                    [&](const auto& value) { return value.id == id; });
    check(found != snapshot.orders.end(), "missing active order " + std::to_string(id));
    return *found;
}

AccountSnapshot account(const Engine& engine, const std::string& id) {
    const auto snapshot = engine.snapshot();
    const auto found = std::find_if(snapshot.accounts.begin(), snapshot.accounts.end(),
                                    [&](const auto& value) { return value.id == id; });
    check(found != snapshot.accounts.end(), "missing account " + id);
    return *found;
}

void test_fifo_and_partial_fills() {
    Engine engine(defaults());
    accepted(engine, submit(1, 1, "A", Side::Buy, 100, 10));
    accepted(engine, submit(2, 2, "B", Side::Buy, 100, 7));
    const auto outcome = accepted(engine, submit(3, 3, "C", Side::Sell, 100, 12));
    check(trades(outcome) == std::vector<reference::Trade>{{3, 1, 100, 10}, {3, 2, 100, 2}},
          "FIFO/partial-fill trade trace");
    check(outcome.events.front().type == EventType::Accepted &&
          outcome.events.back().type == EventType::BookUpdate, "submit event order");
    check(outcome.events.back().best_bid == 100 && outcome.events.back().best_ask == 0,
          "post-trade best prices");
    check(engine.snapshot().orders.size() == 1 && order(engine, 2).remaining == 5,
          "partial-fill residual");
    check(account(engine, "A").position == 10 && account(engine, "B").position == 2 &&
          account(engine, "C").position == -12, "counterparty positions");
}

void test_best_price_and_resting_price() {
    Engine engine(defaults());
    accepted(engine, submit(1, 1, "A", Side::Sell, 103, 5));
    accepted(engine, submit(2, 2, "B", Side::Sell, 102, 4));
    accepted(engine, submit(3, 3, "C", Side::Sell, 102, 4));
    const auto outcome = accepted(engine, submit(4, 4, "D", Side::Buy, 103, 6));
    check(trades(outcome) == std::vector<reference::Trade>{{4, 2, 102, 4}, {4, 3, 102, 2}},
          "best price must outrank older worse price; execution at maker price");
    check(order(engine, 1).remaining == 5 && order(engine, 3).remaining == 2,
          "unmatched ask depths");
}

void test_replace_priority() {
    {
        Engine engine(defaults());
        accepted(engine, submit(1, 1, "A", Side::Buy, 100, 8));
        accepted(engine, submit(2, 2, "B", Side::Buy, 100, 6));
        const auto original_priority = order(engine, 1).priority;
        const auto change = accepted(engine, replace(3, 1, "A", 100, 5));
        check(change.events.front().type == EventType::Replaced, "replace acknowledgement");
        check(order(engine, 1).priority == original_priority, "reduction must retain priority");
        accepted(engine, replace(4, 1, "A", 100, 5));
        check(order(engine, 1).priority == original_priority, "no-op retains priority");
        check(trades(accepted(engine, submit(5, 3, "C", Side::Sell, 100, 6))) ==
                  std::vector<reference::Trade>{{3, 1, 100, 5}, {3, 2, 100, 1}},
              "reduction queue priority");
    }
    {
        Engine engine(defaults());
        accepted(engine, submit(1, 1, "A", Side::Buy, 100, 8));
        accepted(engine, submit(2, 2, "B", Side::Buy, 100, 6));
        accepted(engine, replace(3, 1, "A", 100, 9));
        check(order(engine, 1).priority > order(engine, 2).priority, "increase renews priority");
        check(trades(accepted(engine, submit(4, 3, "C", Side::Sell, 100, 7))) ==
                  std::vector<reference::Trade>{{3, 2, 100, 6}, {3, 1, 100, 1}},
              "increased order moves to queue back");
    }
    {
        Engine engine(defaults());
        accepted(engine, submit(1, 1, "A", Side::Buy, 99, 3));
        accepted(engine, submit(2, 2, "B", Side::Buy, 100, 3));
        accepted(engine, replace(3, 1, "A", 100, 3));
        check(trades(accepted(engine, submit(4, 3, "C", Side::Sell, 100, 4))) ==
                  std::vector<reference::Trade>{{3, 2, 100, 3}, {3, 1, 100, 1}},
              "price change joins back of destination FIFO");
    }
    {
        Engine engine(defaults());
        accepted(engine, submit(1, 1, "A", Side::Buy, 100, 3));
        accepted(engine, submit(2, 2, "B", Side::Sell, 101, 2));
        const auto outcome = accepted(engine, replace(3, 1, "A", 101, 3));
        check(trades(outcome) == std::vector<reference::Trade>{{1, 2, 101, 2}},
              "price-changing replacement can cross");
        check(order(engine, 1).price == 101 && order(engine, 1).remaining == 1,
              "replacement residual");
    }
}

void test_cancel_identity_and_validation() {
    Engine engine(defaults());
    accepted(engine, submit(1, 1, "A", Side::Buy, 100, 3));
    rejected(engine, cancel(2, 1, "B"), "NOT_OWNER");
    rejected(engine, replace(3, 1, "A", 100, 0), "INVALID_QUANTITY");
    rejected(engine, submit(4, 1, "A", Side::Buy, 100, 1), "DUPLICATE_ORDER");
    rejected(engine, submit(1, 9, "A", Side::Buy, 100, 1), "DUPLICATE_COMMAND");
    const auto cancellation = accepted(engine, cancel(5, 1, "A"));
    check(cancellation.events.size() == 2 &&
          cancellation.events.front().type == EventType::Cancelled &&
          cancellation.events.back().type == EventType::BookUpdate, "cancel output order");
    rejected(engine, cancel(6, 1, "A"), "ORDER_TERMINAL");
    rejected(engine, submit(7, 1, "A", Side::Buy, 100, 1), "DUPLICATE_ORDER");
    rejected(engine, cancel(8, 999, "A"), "UNKNOWN_ORDER");
    rejected(engine, submit(9, 9, "missing", Side::Buy, 100, 1), "UNKNOWN_ACCOUNT");
    rejected(engine, submit(10, 10, "A", Side::Buy, 0, 1), "INVALID_PRICE");
    rejected(engine, submit(11, 11, "A", Side::Buy, 100, -1), "INVALID_QUANTITY");
    rejected(engine, submit(0, 12, "A", Side::Buy, 100, 1), "INVALID_COMMAND_ID");
    rejected(engine, submit(12, 0, "A", Side::Buy, 100, 1), "INVALID_ORDER_ID");
    auto invalid_side = submit(13, 13, "A", static_cast<Side>(99), 100, 1);
    rejected(engine, invalid_side, "INVALID_SIDE");
    auto invalid_type = submit(14, 14, "A", Side::Buy, 100, 1);
    invalid_type.type = static_cast<CommandType>(99);
    rejected(engine, invalid_type, "INVALID_COMMAND_TYPE");
    accepted(engine, submit(15, 2, "B", Side::Sell, 100, 3));
    check(engine.snapshot().orders.size() == 1, "cancelled order cannot execute again");
}

void test_risk_and_accounting() {
    {
        Engine engine(defaults(1000, 10));
        accepted(engine, submit(1, 1, "A", Side::Buy, 100, 8));
        check(account(engine, "A").reserved_cash == 800, "buy notional reservation");
        rejected(engine, submit(2, 2, "A", Side::Buy, 100, 5), "POSITION_LIMIT");
        rejected(engine, replace(3, 1, "A", 100, 11), "POSITION_LIMIT");
        check(order(engine, 1).remaining == 8, "invalid replacement keeps original");
        accepted(engine, cancel(4, 1, "A"));
        check(account(engine, "A").reserved_cash == 0, "cancel releases reservation");
        accepted(engine, submit(5, 2, "A", Side::Buy, 100, 5));
        accepted(engine, submit(6, 3, "A", Side::Sell, 110, 8));
        rejected(engine, submit(7, 4, "A", Side::Sell, 110, 5), "POSITION_LIMIT");
    }
    {
        Engine engine(defaults(1000, 100));
        accepted(engine, submit(1, 1, "A", Side::Buy, 100, 6));
        rejected(engine, submit(2, 2, "A", Side::Buy, 100, 5), "BUYING_POWER");
    }
    {
        EngineConfig config;
        config.accounts = {{"A", 1000, -100, 100, 0}, {"B", 0, -100, 100, 0}};
        Engine engine(config);
        accepted(engine, submit(1, 1, "B", Side::Sell, 90, 2));
        accepted(engine, submit(2, 2, "A", Side::Buy, 100, 3));
        const auto buyer = account(engine, "A");
        const auto seller = account(engine, "B");
        check(buyer.cash == 820 && buyer.position == 2 && buyer.reserved_cash == 100 &&
              buyer.reserved_buy_quantity == 1, "buyer fill/price-improvement accounting");
        check(seller.cash == 180 && seller.position == -2 &&
              seller.reserved_sell_quantity == 0, "seller fill accounting");
        check(buyer.cash + seller.cash == 1000, "cash conserved");
    }
}

void test_kill_switch() {
    Engine engine(defaults());
    accepted(engine, submit(1, 1, "A", Side::Buy, 100, 8));
    const auto control = accepted(engine, kill(2, true));
    check(control.events.size() == 1 && control.events[0].type == EventType::KillSwitch,
          "kill-switch event contract");
    rejected(engine, submit(3, 2, "A", Side::Sell, 110, 1), "KILL_SWITCH_ACTIVE");
    accepted(engine, replace(4, 1, "A", 100, 5));
    rejected(engine, replace(5, 1, "A", 100, 6), "KILL_SWITCH_ACTIVE");
    rejected(engine, replace(6, 1, "A", 99, 4), "KILL_SWITCH_ACTIVE");
    accepted(engine, cancel(7, 1, "A"));
    accepted(engine, kill(8, false));
    accepted(engine, submit(9, 2, "A", Side::Sell, 110, 1));
    check(!engine.snapshot().kill_switch, "kill reset");
}

void test_self_trade_preflight() {
    Engine engine(defaults());
    accepted(engine, submit(1, 1, "A", Side::Sell, 100, 3));
    accepted(engine, submit(2, 2, "B", Side::Sell, 99, 2));
    rejected(engine, submit(3, 3, "A", Side::Buy, 100, 4), "SELF_TRADE");
    check(trades(accepted(engine, submit(4, 3, "A", Side::Buy, 100, 2))) ==
              std::vector<reference::Trade>{{3, 2, 99, 2}},
          "self liquidity beyond exhausted quantity does not block order");
    accepted(engine, submit(5, 4, "A", Side::Buy, 95, 2));
    rejected(engine, replace(6, 4, "A", 100, 1), "SELF_TRADE");
    check(order(engine, 4).price == 95 && order(engine, 4).remaining == 2,
          "failed replacement restores original liquidity");
}

void test_resource_bounds_and_admission() {
    {
        auto config = defaults();
        config.max_active_orders = 1;
        Engine engine(config);
        accepted(engine, submit(1, 1, "A", Side::Buy, 100, 1));
        rejected(engine, submit(2, 2, "B", Side::Buy, 99, 1), "ORDER_CAPACITY");
        accepted(engine, submit(3, 2, "C", Side::Sell, 100, 1));
        check(engine.snapshot().orders.empty(), "full book allows capacity-releasing fill");
    }
    {
        auto config = defaults();
        config.max_order_ids = 1;
        Engine engine(config);
        accepted(engine, submit(1, 1, "A", Side::Buy, 100, 1));
        accepted(engine, cancel(2, 1, "A"));
        rejected(engine, submit(3, 2, "A", Side::Buy, 100, 1), "ORDER_ID_CAPACITY");
        check(engine.snapshot().used_order_ids == std::vector<std::uint64_t>{1},
              "order ID ledger never evicts terminal identity");
    }
    {
        auto config = defaults();
        config.max_commands = 2;
        Engine engine(config);
        rejected(engine, submit(0, 1, "A", Side::Buy, 100, 1), "INVALID_COMMAND_ID");
        rejected(engine, submit(1, 1, "unknown", Side::Buy, 100, 1), "UNKNOWN_ACCOUNT");
        accepted(engine, submit(2, 1, "A", Side::Buy, 100, 1));
        rejected(engine, cancel(3, 1, "A"), "COMMAND_CAPACITY");
        rejected(engine, cancel(2, 1, "A"), "DUPLICATE_COMMAND");
        const auto snapshot = engine.snapshot();
        check(snapshot.sequence == 5 && snapshot.seen_command_ids ==
              std::vector<std::uint64_t>{1, 2}, "all calls sequence; only admitted IDs stored");
    }
    {
        auto config = defaults();
        config.max_price = 100;
        config.max_quantity = 5;
        Engine engine(config);
        rejected(engine, submit(1, 1, "A", Side::Buy, 101, 1), "INVALID_PRICE");
        rejected(engine, submit(2, 1, "A", Side::Buy, 100, 6), "INVALID_QUANTITY");
    }
}

void test_integer_extremes_and_config_validation() {
    constexpr auto high = (std::numeric_limits<std::int64_t>::max)();
    constexpr auto low = (std::numeric_limits<std::int64_t>::min)();
    EngineConfig config;
    config.accounts = {{"A", high, low, high, 0}, {"B", 0, low, high, 0}};
    Engine engine(config);
    rejected(engine, submit(1, 1, "A", Side::Buy, high, 2), "ARITHMETIC_OVERFLOW");
    accepted(engine, submit(2, 1, "A", Side::Buy, high, 1));
    accepted(engine, submit(3, 2, "B", Side::Sell, high, 1));
    check(account(engine, "A").cash == 0 && account(engine, "B").cash == high,
          "maximum representable notional transfers safely");
    {
        EngineConfig split;
        split.accounts = {{"A", high, -100, 100, 0}, {"B", 0, -100, 100, 0},
                          {"C", 0, -100, 100, 0}};
        Engine split_engine(split);
        accepted(split_engine, submit(1, 1, "B", Side::Sell, high / 2, 1));
        accepted(split_engine, submit(2, 2, "C", Side::Sell, high / 2, 1));
        const auto result = accepted(split_engine, submit(3, 3, "A", Side::Buy, high / 2, 2));
        check(trades(result).size() == 2 && account(split_engine, "A").cash == 1 &&
              account(split_engine, "B").cash == high / 2 &&
              account(split_engine, "C").cash == high / 2,
              "MAX aggregate cash split across multiple maker receipts");
    }
    {
        EngineConfig extreme_position;
        extreme_position.accounts = {{"A", 0, low, high, low}};
        Engine low_engine(extreme_position);
        rejected(low_engine, submit(1, 1, "A", Side::Sell, 1, 1), "ARITHMETIC_OVERFLOW");
        extreme_position.accounts = {{"A", 1, low, high, high}};
        Engine high_engine(extreme_position);
        rejected(high_engine, submit(1, 1, "A", Side::Buy, 1, 1), "ARITHMETIC_OVERFLOW");
    }

    const auto must_throw = [](const EngineConfig& invalid, const std::string& description) {
        bool threw = false;
        try {
            Engine ignored(invalid);
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw, "invalid config accepted: " + description);
    };
    auto invalid = defaults();
    invalid.accounts[0].cash = -1;
    must_throw(invalid, "negative cash");
    invalid = config;
    invalid.accounts[1].cash = 1;
    must_throw(invalid, "aggregate cash overflow");
    invalid = defaults();
    invalid.accounts[1].id = invalid.accounts[0].id;
    must_throw(invalid, "duplicate account identity");
    invalid = defaults();
    invalid.accounts[0].position = 101;
    must_throw(invalid, "position outside limits");
    invalid = defaults();
    invalid.max_accounts = 1;
    must_throw(invalid, "account capacity");
    invalid = defaults();
    invalid.max_commands = 0;
    must_throw(invalid, "zero command capacity");
}

void test_canonical_schema_fixtures() {
    // These expectations are hand-written schema bytes. Neither the oracle nor
    // another production serializer computes the expected representation.
    const Outcome empty;
    check(empty.canonical() == "OUTCOME 0 0 0 ~\n", "empty/zero canonical outcome fixture");

    Outcome boundary;
    boundary.sequence = (std::numeric_limits<std::uint64_t>::max)();
    boundary.command_id = (std::numeric_limits<std::uint64_t>::max)();
    boundary.accepted = true;
    boundary.reason = "ok weird%~\n";
    Event trade;
    trade.type = EventType::Trade;
    trade.order_id = (std::numeric_limits<std::uint64_t>::max)();
    trade.maker_order_id = (std::numeric_limits<std::uint64_t>::max)();
    trade.account_id = "A.z_9-";
    trade.maker_account_id = std::string(" \t\n\r%~", 6);
    trade.maker_account_id += '\0';
    trade.maker_account_id += static_cast<char>(0xFF);
    trade.side = Side::Sell;
    trade.price = (std::numeric_limits<std::int64_t>::min)();
    trade.quantity = (std::numeric_limits<std::int64_t>::max)();
    trade.best_bid = -1;
    trade.best_ask = (std::numeric_limits<std::int64_t>::max)();
    trade.enabled = true;
    boundary.events.push_back(trade);
    const std::string expected_boundary =
        "OUTCOME 18446744073709551615 18446744073709551615 1 ok%20weird%25%7E%0A\n"
        "EVENT 18446744073709551615 0 TRADE 18446744073709551615 18446744073709551615 "
        "A.z_9- %20%09%0A%0D%25%7E%00%FF SELL -9223372036854775808 "
        "9223372036854775807 -1 9223372036854775807 1 ~\n";
    check(boundary.canonical() == expected_boundary,
          "canonical integer extrema, true flags, and byte-escaping fixture");

    Outcome ordered;
    ordered.sequence = 9;
    ordered.command_id = 42;
    ordered.reason = "FAILED";
    for (const auto type : {EventType::Accepted, EventType::Rejected, EventType::Cancelled,
                            EventType::Replaced, EventType::Trade, EventType::KillSwitch,
                            EventType::BookUpdate, static_cast<EventType>(99),
                            EventType::Accepted, EventType::Rejected, EventType::BookUpdate}) {
        Event event;
        event.type = type;
        if (type == static_cast<EventType>(99)) event.side = static_cast<Side>(99);
        ordered.events.push_back(event);
    }
    const std::string expected_ordered =
        "OUTCOME 9 42 0 FAILED\n"
        "EVENT 9 0 ACCEPTED 0 0 ~ ~ BUY 0 0 0 0 0 ~\n"
        "EVENT 9 1 REJECTED 0 0 ~ ~ BUY 0 0 0 0 0 ~\n"
        "EVENT 9 2 CANCELLED 0 0 ~ ~ BUY 0 0 0 0 0 ~\n"
        "EVENT 9 3 REPLACED 0 0 ~ ~ BUY 0 0 0 0 0 ~\n"
        "EVENT 9 4 TRADE 0 0 ~ ~ BUY 0 0 0 0 0 ~\n"
        "EVENT 9 5 KILL_SWITCH 0 0 ~ ~ BUY 0 0 0 0 0 ~\n"
        "EVENT 9 6 BOOK_UPDATE 0 0 ~ ~ BUY 0 0 0 0 0 ~\n"
        "EVENT 9 7 INVALID 0 0 ~ ~ INVALID 0 0 0 0 0 ~\n"
        "EVENT 9 8 ACCEPTED 0 0 ~ ~ BUY 0 0 0 0 0 ~\n"
        "EVENT 9 9 REJECTED 0 0 ~ ~ BUY 0 0 0 0 0 ~\n"
        "EVENT 9 10 BOOK_UPDATE 0 0 ~ ~ BUY 0 0 0 0 0 ~\n";
    check(ordered.canonical() == expected_ordered,
          "canonical enum spellings, defaults, false flags, and multi-digit event ordinals fixture");
}

void test_seeded_differential_and_replay() {
    constexpr std::uint64_t seed_count = 16;
    constexpr std::uint64_t steps = 1200;
    for (std::uint64_t seed = 1; seed <= seed_count; ++seed) {
        auto config = defaults(3000, 35);
        Engine engine(config);
        Engine replay(config);
        reference::Model oracle(config);
        std::mt19937_64 random(seed);
        for (std::uint64_t step = 1; step <= steps; ++step) {
            const auto choice = random() % 100;
            const std::string owner(1, static_cast<char>('A' + random() % 4));
            const auto target = 1 + random() % (step + 7);
            const auto price = static_cast<std::int64_t>(90 + random() % 21);
            const auto quantity = static_cast<std::int64_t>(1 + random() % 12);
            Command command;
            if (choice < 50) {
                command = submit(step, step, owner, random() % 2 ? Side::Buy : Side::Sell,
                                 price, quantity);
            } else if (choice < 70) {
                command = cancel(step, target, owner);
            } else if (choice < 90) {
                command = replace(step, target, owner, price, quantity);
            } else if (choice < 95) {
                command = kill(step, random() % 2 == 0);
            } else {
                command = submit(step, step, owner, Side::Buy,
                                 random() % 2 ? 0 : price, random() % 2 ? 0 : quantity);
                if (random() % 3 == 0) {
                    command.command_id = step > 1 ? step - 1 : 0;
                }
            }
            const auto actual = engine.process(command);
            const auto expected = oracle.process(command);
            const auto repeated = replay.process(command);
            const auto context = "seed=" + std::to_string(seed) + " step=" + std::to_string(step);
            check(actual.accepted == expected.accepted, "reference acceptance differs: " + context +
                                                        " engine=" + actual.reason);
            check(trades(actual) == expected.trades, "reference trade trace differs: " + context);
            check(normalize(engine.snapshot()) == normalize(oracle.snapshot()),
                  "reference full state differs: " + context);
            check(actual.canonical() == repeated.canonical(), "replay event bytes differ: " + context);
            invariant_check(engine);
        }
        check(engine.snapshot().canonical() == replay.snapshot().canonical(),
              "replay final state differs for seed " + std::to_string(seed));
        check(engine.snapshot().digest() == replay.snapshot().digest(),
              "replay final digest differs for seed " + std::to_string(seed));
    }
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"FIFO and partial fills", test_fifo_and_partial_fills},
        {"Best and resting prices", test_best_price_and_resting_price},
        {"Replace priority and crossing", test_replace_priority},
        {"Cancel identity and validation", test_cancel_identity_and_validation},
        {"Risk reservations and accounting", test_risk_and_accounting},
        {"Kill-switch transitions", test_kill_switch},
        {"Atomic self-trade preflight", test_self_trade_preflight},
        {"Resource bounds and admission", test_resource_bounds_and_admission},
        {"Integer extremes and configuration", test_integer_extremes_and_config_validation},
        {"Independent canonical schema fixtures", test_canonical_schema_fixtures},
        {"Seeded differential model and exact replay", test_seeded_differential_and_replay},
    };
    std::size_t failed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failed << '/' << tests.size()
              << " test groups passed; differential suite covers 16 seeds x 1200 commands.\n";
    return failed == 0 ? 0 : 1;
}
