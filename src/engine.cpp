#include "hpts/engine.hpp"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <iterator>
#include <list>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace hpts {
namespace {

using Integer = std::int64_t;
using Id = std::uint64_t;
constexpr Integer maximum = (std::numeric_limits<Integer>::max)();
constexpr Integer minimum = (std::numeric_limits<Integer>::min)();

bool add(Integer a, Integer b, Integer& result) {
    if ((b > 0 && a > maximum - b) || (b < 0 && a < minimum - b)) return false;
    result = a + b;
    return true;
}

bool subtract(Integer a, Integer b, Integer& result) {
    if ((b > 0 && a < minimum + b) || (b < 0 && a > maximum + b)) return false;
    result = a - b;
    return true;
}

bool multiply(Integer a, Integer b, Integer& result) {
    if (a < 0 || b < 0 || (b != 0 && a > maximum / b)) return false;
    result = a * b;
    return true;
}

bool identifier_character(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

bool valid_identifier(const std::string& value, std::size_t limit) {
    return !value.empty() && value.size() <= limit &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return identifier_character(c);
           });
}

std::string encode(const std::string& value) {
    if (value.empty()) return "~";
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char c : value) {
        if (identifier_character(c)) result += static_cast<char>(c);
        else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}

std::string fingerprint(const std::string& value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

Event order_event(EventType type, const OrderSnapshot& order) {
    Event event;
    event.type = type;
    event.order_id = order.id;
    event.account_id = order.account_id;
    event.side = order.side;
    event.price = order.price;
    event.quantity = order.remaining;
    return event;
}

Outcome rejected(const Command& command, Id sequence, const std::string& reason) {
    Outcome output;
    output.sequence = sequence;
    output.command_id = command.command_id;
    output.reason = reason;
    Event event;
    event.type = EventType::Rejected;
    event.order_id = command.order_id;
    event.reason = reason;
    output.events.push_back(std::move(event));
    return output;
}

bool reserve(AccountSnapshot& account, Side side, Integer price, Integer quantity) {
    Integer notional = 0;
    if (!multiply(price, quantity, notional)) return false;
    if (side == Side::Buy) {
        return add(account.reserved_cash, notional, account.reserved_cash) &&
               add(account.reserved_buy_quantity, quantity, account.reserved_buy_quantity);
    }
    return add(account.reserved_sell_quantity, quantity, account.reserved_sell_quantity);
}

void unreserve(AccountSnapshot& account, Side side, Integer price, Integer quantity) {
    if (side == Side::Buy) {
        Integer notional = 0;
        if (!multiply(price, quantity, notional) || notional > account.reserved_cash ||
            quantity > account.reserved_buy_quantity) throw std::logic_error("invalid buy reservation");
        account.reserved_cash -= notional;
        account.reserved_buy_quantity -= quantity;
    } else {
        if (quantity > account.reserved_sell_quantity) throw std::logic_error("invalid sell reservation");
        account.reserved_sell_quantity -= quantity;
    }
}

std::string risk_error(const AccountSnapshot& account) {
    Integer potential_long = 0;
    Integer potential_short = 0;
    if (!add(account.position, account.reserved_buy_quantity, potential_long) ||
        !subtract(account.position, account.reserved_sell_quantity, potential_short)) {
        return "ARITHMETIC_OVERFLOW";
    }
    if (potential_long > account.max_position || potential_short < account.min_position) return "POSITION_LIMIT";
    if (account.reserved_cash > account.cash) return "BUYING_POWER";
    return {};
}

void assign_account_numbers(AccountSnapshot& destination, const AccountSnapshot& source) noexcept {
    destination.cash = source.cash;
    destination.position = source.position;
    destination.reserved_cash = source.reserved_cash;
    destination.reserved_buy_quantity = source.reserved_buy_quantity;
    destination.reserved_sell_quantity = source.reserved_sell_quantity;
}

}  // namespace

std::string to_string(Side side) {
    switch (side) {
        case Side::Buy: return "BUY";
        case Side::Sell: return "SELL";
    }
    return "INVALID";
}

std::string to_string(EventType type) {
    switch (type) {
        case EventType::Accepted: return "ACCEPTED";
        case EventType::Rejected: return "REJECTED";
        case EventType::Cancelled: return "CANCELLED";
        case EventType::Replaced: return "REPLACED";
        case EventType::Trade: return "TRADE";
        case EventType::KillSwitch: return "KILL_SWITCH";
        case EventType::BookUpdate: return "BOOK_UPDATE";
    }
    return "INVALID";
}

std::string Outcome::canonical() const {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "OUTCOME " << sequence << ' ' << command_id << ' ' << accepted << ' ' << encode(reason) << '\n';
    std::size_t ordinal = 0;
    for (const auto& event : events) {
        output << "EVENT " << sequence << ' ' << ordinal++ << ' ' << to_string(event.type) << ' '
               << event.order_id << ' ' << event.maker_order_id << ' ' << encode(event.account_id) << ' '
               << encode(event.maker_account_id) << ' ' << to_string(event.side) << ' ' << event.price << ' '
               << event.quantity << ' ' << event.best_bid << ' ' << event.best_ask << ' ' << event.enabled << ' '
               << encode(event.reason) << '\n';
    }
    return output.str();
}

std::string Snapshot::canonical() const {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "STATE 1 " << sequence << ' ' << kill_switch << '\n';
    for (const auto& account : accounts) {
        output << "ACCOUNT " << encode(account.id) << ' ' << account.cash << ' ' << account.min_position << ' '
               << account.max_position << ' ' << account.position << ' ' << account.reserved_cash << ' '
               << account.reserved_buy_quantity << ' ' << account.reserved_sell_quantity << '\n';
    }
    for (const auto& order : orders) {
        output << "ORDER " << order.id << ' ' << encode(order.account_id) << ' ' << to_string(order.side) << ' '
               << order.price << ' ' << order.remaining << ' ' << order.priority << '\n';
    }
    for (const auto id : seen_command_ids) output << "COMMAND_ID " << id << '\n';
    for (const auto id : used_order_ids) output << "ORDER_ID " << id << '\n';
    return output.str();
}

std::string Snapshot::digest() const { return fingerprint(canonical()); }

std::string canonical_config(const EngineConfig& config) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "CONFIG 1 SIM " << config.max_active_orders << ' ' << config.max_order_ids << ' '
           << config.max_commands << ' ' << config.max_accounts << ' ' << config.max_account_id_length << ' '
           << config.max_price << ' ' << config.max_quantity << '\n';
    auto accounts = config.accounts;
    std::sort(accounts.begin(), accounts.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (const auto& account : accounts) {
        output << "INITIAL_ACCOUNT " << encode(account.id) << ' ' << account.cash << ' ' << account.min_position << ' '
               << account.max_position << ' ' << account.position << '\n';
    }
    return output.str();
}

struct Engine::Impl {
    using Queue = std::list<Id>;
    using BidBook = std::map<Integer, Queue, std::greater<Integer>>;
    using AskBook = std::map<Integer, Queue>;
    struct Order {
        OrderSnapshot state;
        Queue::iterator location;
    };
    struct Fill { Id maker; Integer price; Integer quantity; };

    EngineConfig configuration;
    Id sequence = 0;
    bool kill_switch = false;
    Integer total_cash = 0;
    std::map<std::string, AccountSnapshot> accounts;
    std::map<Id, Order> orders;
    BidBook bids;
    AskBook asks;
    std::set<Id> commands;
    std::set<Id> used_orders;

    explicit Impl(EngineConfig config) : configuration(std::move(config)) {
        const auto& c = configuration;
        if (c.accounts.empty() || c.accounts.size() > c.max_accounts || c.max_active_orders == 0 ||
            c.max_order_ids == 0 || c.max_commands == 0 || c.max_account_id_length == 0 ||
            c.max_price <= 0 || c.max_quantity <= 0) throw std::invalid_argument("invalid engine resource limits");
        for (const auto& initial : c.accounts) {
            if (!valid_identifier(initial.id, c.max_account_id_length) || initial.cash < 0 ||
                initial.min_position > initial.max_position || initial.position < initial.min_position ||
                initial.position > initial.max_position) throw std::invalid_argument("invalid initial account");
            if (!add(total_cash, initial.cash, total_cash)) throw std::invalid_argument("total initial cash overflows");
            AccountSnapshot account;
            account.id = initial.id;
            account.cash = initial.cash;
            account.min_position = initial.min_position;
            account.max_position = initial.max_position;
            account.position = initial.position;
            if (!accounts.emplace(initial.id, std::move(account)).second) throw std::invalid_argument("duplicate account");
        }
    }

    void erase_order(Id id) noexcept {
        const auto found = orders.find(id);
        const auto& order = found->second;
        if (order.state.side == Side::Buy) {
            const auto level = bids.find(order.state.price);
            level->second.erase(order.location);
            if (level->second.empty()) bids.erase(level);
        } else {
            const auto level = asks.find(order.state.price);
            level->second.erase(order.location);
            if (level->second.empty()) asks.erase(level);
        }
        orders.erase(found);
    }

    Event book_event() const {
        Event event;
        event.type = EventType::BookUpdate;
        event.best_bid = bids.empty() ? 0 : bids.begin()->first;
        event.best_ask = asks.empty() ? 0 : asks.begin()->first;
        return event;
    }

    template <typename Book>
    bool plan_fills(const Book& book, const OrderSnapshot& incoming, std::vector<Fill>& plan,
                    Integer& remaining, std::size_t& terminal_makers) const {
        for (const auto& [price, queue] : book) {
            if ((incoming.side == Side::Buy && price > incoming.price) ||
                (incoming.side == Side::Sell && price < incoming.price)) break;
            for (const Id maker_id : queue) {
                if (remaining == 0) return true;
                const auto& maker = orders.at(maker_id).state;
                if (maker.account_id == incoming.account_id) return false;
                const auto quantity = (std::min)(remaining, maker.remaining);
                plan.push_back({maker.id, maker.price, quantity});
                remaining -= quantity;
                if (quantity == maker.remaining) ++terminal_makers;
            }
        }
        return true;
    }

    Outcome process(const Command& command) {
        if (sequence == (std::numeric_limits<Id>::max)()) throw std::overflow_error("authoritative sequence exhausted");
        ++sequence;
        const auto fail = [&](const std::string& reason) { return rejected(command, sequence, reason); };
        if (command.command_id == 0) return fail("INVALID_COMMAND_ID");
        if (commands.contains(command.command_id)) return fail("DUPLICATE_COMMAND");
        if (commands.size() >= configuration.max_commands) return fail("COMMAND_CAPACITY");
        commands.insert(command.command_id);

        if (command.type == CommandType::KillSwitch) {
            Outcome output{sequence, command.command_id, true, "OK", {}};
            Event event;
            event.type = EventType::KillSwitch;
            event.enabled = command.enabled;
            output.events.push_back(std::move(event));
            kill_switch = command.enabled;
            return output;
        }
        if (command.type != CommandType::Submit && command.type != CommandType::Cancel &&
            command.type != CommandType::Replace) return fail("INVALID_COMMAND_TYPE");
        if (!valid_identifier(command.account_id, configuration.max_account_id_length)) return fail("INVALID_ACCOUNT_ID");
        const auto account_it = accounts.find(command.account_id);
        if (account_it == accounts.end()) return fail("UNKNOWN_ACCOUNT");
        if (command.order_id == 0) return fail("INVALID_ORDER_ID");
        const bool replacing = command.type == CommandType::Replace;
        const auto old = orders.find(command.order_id);
        if (command.type != CommandType::Submit) {
            if (old == orders.end()) return fail(used_orders.contains(command.order_id) ? "ORDER_TERMINAL" : "UNKNOWN_ORDER");
            if (old->second.state.account_id != command.account_id) return fail("NOT_OWNER");
        }
        if (command.type == CommandType::Cancel) {
            auto updated = account_it->second;
            const auto original = old->second.state;
            unreserve(updated, original.side, original.price, original.remaining);
            Outcome output{sequence, command.command_id, true, "OK", {}};
            output.events.reserve(2);
            output.events.push_back(order_event(EventType::Cancelled, original));
            output.events.emplace_back();
            assign_account_numbers(account_it->second, updated);
            erase_order(command.order_id);
            output.events.back() = book_event();
            return output;
        }

        const Side side = replacing ? old->second.state.side : command.side;
        if (side != Side::Buy && side != Side::Sell) return fail("INVALID_SIDE");
        if (command.price <= 0 || command.price > configuration.max_price) return fail("INVALID_PRICE");
        if (command.quantity <= 0 || command.quantity > configuration.max_quantity) return fail("INVALID_QUANTITY");
        if (!replacing && used_orders.contains(command.order_id)) return fail("DUPLICATE_ORDER");
        if (!replacing && used_orders.size() >= configuration.max_order_ids) return fail("ORDER_ID_CAPACITY");
        const bool keeps_priority = replacing && command.price == old->second.state.price &&
                                    command.quantity <= old->second.state.remaining;
        if (kill_switch && !keeps_priority) return fail("KILL_SWITCH_ACTIVE");

        OrderSnapshot incoming{command.order_id, command.account_id, side, command.price,
                               command.quantity, keeps_priority ? old->second.state.priority : sequence};
        std::map<std::string, AccountSnapshot> account_updates;
        account_updates.emplace(command.account_id, account_it->second);
        auto& incoming_account = account_updates.at(command.account_id);
        if (replacing) {
            const auto& original = old->second.state;
            unreserve(incoming_account, original.side, original.price, original.remaining);
        }
        if (!reserve(incoming_account, side, incoming.price, incoming.remaining)) return fail("ARITHMETIC_OVERFLOW");
        if (const auto error = risk_error(incoming_account); !error.empty()) return fail(error);

        std::vector<Fill> plan;
        Integer remaining = incoming.remaining;
        std::size_t terminal_makers = 0;
        const bool safe = side == Side::Buy ? plan_fills(asks, incoming, plan, remaining, terminal_makers)
                                           : plan_fills(bids, incoming, plan, remaining, terminal_makers);
        if (!safe) return fail("SELF_TRADE");
        const std::size_t final_count = orders.size() - (replacing ? 1U : 0U) - terminal_makers + (remaining > 0 ? 1U : 0U);
        if (final_count > configuration.max_active_orders) return fail("ORDER_CAPACITY");

        // Stage all account effects and event allocations before authoritative mutation.
        Outcome output{sequence, command.command_id, true, "OK", {}};
        output.events.reserve(plan.size() + 2);
        output.events.push_back(order_event(replacing ? EventType::Replaced : EventType::Accepted, incoming));
        for (const auto& fill : plan) {
            const auto& maker = orders.at(fill.maker).state;
            account_updates.try_emplace(maker.account_id, accounts.at(maker.account_id));
            auto& maker_account = account_updates.at(maker.account_id);
            auto& buyer = side == Side::Buy ? incoming_account : maker_account;
            auto& seller = side == Side::Sell ? incoming_account : maker_account;
            Integer notional = 0;
            if (!multiply(fill.price, fill.quantity, notional) || !subtract(buyer.cash, notional, buyer.cash) ||
                !add(seller.cash, notional, seller.cash) || !add(buyer.position, fill.quantity, buyer.position) ||
                !subtract(seller.position, fill.quantity, seller.position)) return fail("ARITHMETIC_OVERFLOW");
            unreserve(incoming_account, side, incoming.price, fill.quantity);
            unreserve(maker_account, maker.side, maker.price, fill.quantity);
            Event trade = order_event(EventType::Trade, incoming);
            trade.maker_order_id = maker.id;
            trade.maker_account_id = maker.account_id;
            trade.price = fill.price;
            trade.quantity = fill.quantity;
            output.events.push_back(std::move(trade));
        }
        output.events.emplace_back();
        for (const auto& [id, account] : account_updates) {
            (void)id;
            if (account.cash < 0 || account.reserved_cash < 0 || account.reserved_buy_quantity < 0 ||
                account.reserved_sell_quantity < 0 || !risk_error(account).empty()) throw std::logic_error("invalid staged account");
        }

        // Preallocate transferable nodes. Commit below uses only no-allocation
        // map-node transfers, list splices, numeric assignment, and erasure.
        std::map<Id, Order> staged_order;
        Queue staged_queue;
        BidBook staged_bid_level;
        AskBook staged_ask_level;
        std::set<Id> staged_used_id;
        if (!replacing) staged_used_id.insert(incoming.id);
        if (remaining > 0 && !keeps_priority) {
            incoming.remaining = remaining;
            staged_order.emplace(incoming.id, Order{incoming, {}});
            staged_queue.push_back(incoming.id);
            if (side == Side::Buy) staged_bid_level.try_emplace(incoming.price);
            else staged_ask_level.try_emplace(incoming.price);
        }

        for (const auto& [id, account] : account_updates) assign_account_numbers(accounts.at(id), account);
        if (keeps_priority) {
            old->second.state.remaining = remaining;
        } else {
            if (replacing) erase_order(command.order_id);
            for (const auto& fill : plan) {
                auto& maker = orders.at(fill.maker).state;
                maker.remaining -= fill.quantity;
                if (maker.remaining == 0) erase_order(fill.maker);
            }
            if (remaining > 0) {
                auto inserted = orders.insert(staged_order.extract(incoming.id));
                Queue* destination = nullptr;
                if (side == Side::Buy) {
                    auto level = bids.insert(staged_bid_level.extract(incoming.price));
                    destination = &level.position->second;
                } else {
                    auto level = asks.insert(staged_ask_level.extract(incoming.price));
                    destination = &level.position->second;
                }
                destination->splice(destination->end(), staged_queue);
                inserted.position->second.location = std::prev(destination->end());
            }
        }
        if (!replacing) used_orders.insert(staged_used_id.extract(incoming.id));
        output.events.back() = book_event();
        return output;
    }
};

Engine::Engine(EngineConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;
Outcome Engine::process(const Command& command) { return impl_->process(command); }
const EngineConfig& Engine::config() const noexcept { return impl_->configuration; }

Snapshot Engine::snapshot() const {
    Snapshot output;
    output.sequence = impl_->sequence;
    output.kill_switch = impl_->kill_switch;
    output.accounts.reserve(impl_->accounts.size());
    for (const auto& [id, account] : impl_->accounts) { (void)id; output.accounts.push_back(account); }
    output.orders.reserve(impl_->orders.size());
    for (const auto& [id, order] : impl_->orders) { (void)id; output.orders.push_back(order.state); }
    output.seen_command_ids.assign(impl_->commands.begin(), impl_->commands.end());
    output.used_order_ids.assign(impl_->used_orders.begin(), impl_->used_orders.end());
    return output;
}

std::vector<std::string> Engine::invariant_errors() const {
    std::vector<std::string> errors;
    const auto report = [&](const std::string& message) { errors.push_back(message); };
    if (impl_->orders.size() > impl_->configuration.max_active_orders) report("active order capacity");
    if (impl_->commands.size() > impl_->configuration.max_commands) report("command ledger capacity");
    if (impl_->used_orders.size() > impl_->configuration.max_order_ids) report("order ledger capacity");
    if (!impl_->bids.empty() && !impl_->asks.empty() && impl_->bids.begin()->first >= impl_->asks.begin()->first) report("crossed book");
    std::map<Id, std::size_t> memberships;
    const auto inspect_book = [&](const auto& book, Side side) {
        for (const auto& [price, queue] : book) {
            if (queue.empty()) report("empty price level");
            Id previous_priority = 0;
            for (const Id id : queue) {
                ++memberships[id];
                const auto found = impl_->orders.find(id);
                if (found == impl_->orders.end()) { report("unknown FIFO member"); continue; }
                const auto& order = found->second.state;
                if (order.side != side || order.price != price) report("incorrect FIFO membership");
                if (order.priority <= previous_priority) report("FIFO priority disorder");
                previous_priority = order.priority;
            }
        }
    };
    inspect_book(impl_->bids, Side::Buy);
    inspect_book(impl_->asks, Side::Sell);
    std::map<std::string, AccountSnapshot> computed;
    for (const auto& [id, account] : impl_->accounts) {
        auto expected = account;
        expected.reserved_cash = expected.reserved_buy_quantity = expected.reserved_sell_quantity = 0;
        computed.emplace(id, std::move(expected));
    }
    for (const auto& [id, internal] : impl_->orders) {
        const auto& order = internal.state;
        if (id == 0 || order.id != id || order.remaining <= 0 || order.price <= 0 ||
            order.price > impl_->configuration.max_price || order.remaining > impl_->configuration.max_quantity ||
            order.priority == 0 || order.priority > impl_->sequence || !impl_->used_orders.contains(id)) report("invalid active order");
        if (memberships[id] != 1) report("active order membership count");
        const auto account = computed.find(order.account_id);
        if (account == computed.end()) report("unknown active owner");
        else if (!reserve(account->second, order.side, order.price, order.remaining)) report("reservation recomputation overflow");
    }
    Integer cash = 0;
    for (const auto& [id, account] : impl_->accounts) {
        const auto& expected = computed.at(id);
        if (account.reserved_cash != expected.reserved_cash ||
            account.reserved_buy_quantity != expected.reserved_buy_quantity ||
            account.reserved_sell_quantity != expected.reserved_sell_quantity) report("reservation mismatch");
        if (account.cash < 0 || account.reserved_cash < 0 || account.reserved_buy_quantity < 0 ||
            account.reserved_sell_quantity < 0 || !risk_error(account).empty()) report("account risk invariant");
        if (!add(cash, account.cash, cash)) report("cash sum overflow");
    }
    if (cash != impl_->total_cash) report("cash conservation");
    return errors;
}

}  // namespace hpts
