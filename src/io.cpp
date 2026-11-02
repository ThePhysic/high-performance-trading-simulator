#include "hpts/io.hpp"

#include <charconv>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace hpts {
namespace {
template<class T> T number(const std::string& value) {
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("invalid integer: " + value);
    return result;
}
void fields(const std::vector<std::string>& parts, std::size_t size) {
    if (parts.size() != size) throw std::runtime_error("incorrect number of fields for " + parts.front());
}
void validate_caps(const AppConfig& config) {
    const auto& e = config.engine;
    if (!e.max_active_orders || e.max_active_orders > 1000000 || !e.max_commands ||
        e.max_commands > 1000000 || !e.max_order_ids || e.max_order_ids > 1000000 ||
        !e.max_accounts || e.max_accounts > 1024 || !e.max_account_id_length ||
        e.max_account_id_length > 64 || !config.queue_capacity || config.queue_capacity > 65536)
        throw std::runtime_error("CLI resource bounds exceeded (orders/commands/IDs <= 1000000, accounts <= 1024, ID length <= 64, queue <= 65536; all positive)");
}
}  // namespace

bool bounded_line(std::istream& stream, std::string& line) {
    line.clear();
    char value{};
    while (stream.get(value)) {
        if (value == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        if (line.size() == 4096) throw std::runtime_error("input line exceeds 4096 bytes");
        line.push_back(value);
    }
    if (stream.bad() || (!stream.eof() && stream.fail())) throw std::runtime_error("input read failure");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return !line.empty();
}

std::vector<std::string> tokens(const std::string& line) {
    std::istringstream input(line.substr(0, line.find('#')));
    input.imbue(std::locale::classic());
    std::vector<std::string> result;
    std::string part;
    while (input >> part) result.push_back(std::move(part));
    return result;
}

AppConfig read_config(std::istream& stream, bool journal) {
    AppConfig config;
    std::string line;
    std::size_t line_number = 0;
    bool have_limits = false, have_bounds = false, have_begin = false;
    if (journal) {
        if (!bounded_line(stream, line) || line != "HPTS-JOURNAL 1")
            throw std::runtime_error("unsupported or missing journal header");
        ++line_number;
    }
    while (bounded_line(stream, line)) {
        ++line_number;
        const auto parts = tokens(line);
        if (parts.empty()) continue;
        try {
            if (journal && parts.front() == "BEGIN") {
                fields(parts, 1);
                have_begin = true;
                break;
            }
            if (parts.front() == "account") {
                fields(parts, 6);
                if (config.engine.accounts.size() == 1024) throw std::runtime_error("too many accounts");
                config.engine.accounts.push_back({parts[1], number<std::int64_t>(parts[2]),
                    number<std::int64_t>(parts[3]), number<std::int64_t>(parts[4]), number<std::int64_t>(parts[5])});
            } else if (parts.front() == "limits") {
                fields(parts, 5);
                if (have_limits) throw std::runtime_error("duplicate limits directive");
                have_limits = true;
                config.engine.max_active_orders = number<std::size_t>(parts[1]);
                config.engine.max_commands = number<std::size_t>(parts[2]);
                config.engine.max_order_ids = number<std::size_t>(parts[3]);
                config.queue_capacity = number<std::size_t>(parts[4]);
            } else if (parts.front() == "bounds") {
                fields(parts, 5);
                if (have_bounds) throw std::runtime_error("duplicate bounds directive");
                have_bounds = true;
                config.engine.max_price = number<std::int64_t>(parts[1]);
                config.engine.max_quantity = number<std::int64_t>(parts[2]);
                config.engine.max_accounts = number<std::size_t>(parts[3]);
                config.engine.max_account_id_length = number<std::size_t>(parts[4]);
            } else throw std::runtime_error("unknown configuration directive: " + parts.front());
        } catch (const std::exception& error) {
            throw std::runtime_error("configuration line " + std::to_string(line_number) + ": " + error.what());
        }
    }
    if (journal && !have_begin) throw std::runtime_error("journal missing BEGIN marker");
    validate_caps(config);
    return config;
}

std::string config_text(const AppConfig& config) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto& e = config.engine;
    for (const auto& a : e.accounts)
        out << "account " << a.id << ' ' << a.cash << ' ' << a.min_position << ' ' << a.max_position << ' ' << a.position << '\n';
    out << "limits " << e.max_active_orders << ' ' << e.max_commands << ' ' << e.max_order_ids << ' ' << config.queue_capacity << '\n';
    out << "bounds " << e.max_price << ' ' << e.max_quantity << ' ' << e.max_accounts << ' ' << e.max_account_id_length << '\n';
    return out.str();
}

Command parse_command(const std::vector<std::string>& parts) {
    if (parts.empty()) throw std::runtime_error("empty command");
    Command command;
    if (parts.front() == "submit") {
        fields(parts, 7);
        command.type = CommandType::Submit;
        command.command_id = number<std::uint64_t>(parts[1]);
        command.account_id = parts[2];
        command.order_id = number<std::uint64_t>(parts[3]);
        if (parts[4] != "buy" && parts[4] != "sell") throw std::runtime_error("side must be buy or sell");
        command.side = parts[4] == "buy" ? Side::Buy : Side::Sell;
        command.price = number<std::int64_t>(parts[5]);
        command.quantity = number<std::int64_t>(parts[6]);
    } else if (parts.front() == "cancel" || parts.front() == "replace") {
        const bool replacement = parts.front() == "replace";
        fields(parts, replacement ? 6 : 4);
        command.type = replacement ? CommandType::Replace : CommandType::Cancel;
        command.command_id = number<std::uint64_t>(parts[1]);
        command.account_id = parts[2];
        command.order_id = number<std::uint64_t>(parts[3]);
        if (replacement) {
            command.price = number<std::int64_t>(parts[4]);
            command.quantity = number<std::int64_t>(parts[5]);
        }
    } else if (parts.front() == "kill") {
        fields(parts, 3);
        command.type = CommandType::KillSwitch;
        command.command_id = number<std::uint64_t>(parts[1]);
        if (parts[2] != "on" && parts[2] != "off") throw std::runtime_error("kill state must be on or off");
        command.enabled = parts[2] == "on";
    } else throw std::runtime_error("unknown command: " + parts.front());
    return command;
}

std::string command_text(const Command& command) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (command.type == CommandType::KillSwitch) {
        out << "kill " << command.command_id << ' ' << (command.enabled ? "on" : "off");
    } else {
        out << (command.type == CommandType::Submit ? "submit" : command.type == CommandType::Cancel ? "cancel" : "replace");
        out << ' ' << command.command_id << ' ' << command.account_id << ' ' << command.order_id;
        if (command.type == CommandType::Submit) out << ' ' << (command.side == Side::Buy ? "buy" : "sell");
        if (command.type != CommandType::Cancel) out << ' ' << command.price << ' ' << command.quantity;
    }
    return out.str() + '\n';
}

std::string fingerprint(const std::string& text) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const unsigned char byte : text) { value ^= byte; value *= 1099511628211ULL; }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

}  // namespace hpts
