#include "hpts/engine.hpp"
#include "hpts/files.hpp"
#include "hpts/io.hpp"
#include "hpts/queue.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/utsname.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
using Args = std::map<std::string, std::string>;
constexpr std::size_t max_input_commands = 1000000;

constexpr const char* help = R"(High-Performance Trading Simulator 0.1.0
Local terminal exchange; one simulated instrument SIM. No real trading.

Usage:
  hpts --help | --version
  hpts interactive --config FILE
  hpts run --config FILE --input FILE --output FILE --state FILE [--journal FILE]
  hpts replay --journal FILE --expect FILE --expect-state FILE
  hpts bench --config FILE --input FILE [--mode direct|queued] [--output FILE]

Commands (positive numeric IDs; integer tick prices/unit quantities):
  submit COMMAND_ID ACCOUNT ORDER_ID buy|sell PRICE QUANTITY
  cancel COMMAND_ID ACCOUNT ORDER_ID
  replace COMMAND_ID ACCOUNT ORDER_ID PRICE NEW_REMAINING_QUANTITY
  kill COMMAND_ID on|off
Interactive queries: book, accounts, state, help, quit

Config:
  account ID CASH MIN_POSITION MAX_POSITION INITIAL_POSITION
  limits MAX_ACTIVE_ORDERS MAX_COMMAND_IDS MAX_ORDER_IDS QUEUE_CAPACITY
  bounds MAX_PRICE MAX_QUANTITY MAX_ACCOUNTS MAX_ACCOUNT_ID_LENGTH
Comments start with #. See examples/ and docs/DESIGN.md for rules and limits.
Replay requires a complete self-contained version-1 journal and exact outputs.
)";

class Digest {
public:
    void update(const std::string& text) {
        for (const unsigned char byte : text) { value_ ^= byte; value_ *= 1099511628211ULL; }
    }
    std::string text() const {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::hex << std::setfill('0') << std::setw(16) << value_;
        return out.str();
    }
private:
    std::uint64_t value_ = 14695981039346656037ULL;
};

Args arguments(int argc, char* argv[], const std::vector<std::string>& allowed) {
    Args args;
    for (int i = 2; i < argc; i += 2) {
        const std::string key = argv[i];
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end() || i + 1 == argc)
            throw std::runtime_error("unknown option or missing value: " + key);
        if (!args.emplace(key, argv[i + 1]).second) throw std::runtime_error("duplicate option: " + key);
    }
    return args;
}

const std::string& require(const Args& args, const std::string& key) {
    const auto found = args.find(key);
    if (found == args.end() || found->second.empty()) throw std::runtime_error("missing " + key);
    return found->second;
}

hpts::InputFile input_file(const std::string& path) {
    return hpts::InputFile(path);
}

struct ConfigSource {
    explicit ConfigSource(const std::string& path) : input(path), config(hpts::read_config(input)) {}
    hpts::InputFile input;
    hpts::AppConfig config;
};

std::string trace_header(const hpts::AppConfig& config) {
    return "HPTS-EVENTS 1\nCONFIG " + hpts::fingerprint(hpts::config_text(config)) + "\n";
}

void compare_bytes(std::istream& expected, const std::string& actual) {
    for (const char character : actual) {
        const int value = expected.get();
        if (value == std::char_traits<char>::eof() || static_cast<char>(value) != character)
            throw std::runtime_error("replay canonical bytes differ from expected output");
    }
    if (expected.bad()) throw std::runtime_error("expected output read failed");
}

void require_eof(std::istream& stream) {
    if (stream.get() != std::char_traits<char>::eof()) throw std::runtime_error("expected file has extra bytes");
    if (stream.bad()) throw std::runtime_error("expected file read failed");
}

void check_engine(const hpts::Engine& engine) {
    const auto errors = engine.invariant_errors();
    if (!errors.empty()) throw std::runtime_error("engine invariant failed: " + errors.front());
}

int run(const Args& args) {
    const ConfigSource source(require(args, "--config"));
    const auto& config = source.config;
    auto commands = input_file(require(args, "--input"));
    hpts::Engine engine(config.engine);
    hpts::OutputFile events(require(args, "--output"));
    hpts::OutputFile state(require(args, "--state"));
    std::unique_ptr<hpts::OutputFile> journal;
    if (args.contains("--journal")) journal = std::make_unique<hpts::OutputFile>(args.at("--journal"));
    hpts::prepare_outputs({&events, &state, journal.get()}, {source.input.identity(), commands.identity()});
    if (journal) *journal << "HPTS-JOURNAL 1\n" << hpts::config_text(config) << "BEGIN\n";
    Digest digest;
    const auto header = trace_header(config);
    events << header;
    digest.update(header);
    std::size_t count = 0, line_number = 0;
    std::string line;
    while (hpts::bounded_line(commands, line)) {
        ++line_number;
        const auto parts = hpts::tokens(line);
        if (parts.empty()) continue;
        if (++count > max_input_commands) throw std::runtime_error("input exceeds 1000000 command session bound");
        hpts::Command command;
        try { command = hpts::parse_command(parts); }
        catch (const std::exception& error) { throw std::runtime_error("command line " + std::to_string(line_number) + ": " + error.what()); }
        const auto outcome = engine.process(command);
        const auto canonical = outcome.canonical();
        if (journal) *journal << hpts::command_text(command);
        events << canonical;
        digest.update(canonical);
    }
    check_engine(engine);
    const auto snapshot = engine.snapshot();
    state << snapshot.canonical();
    events.finish();
    state.finish();
    if (journal) {
        *journal << "END " << count << ' ' << digest.text() << ' ' << snapshot.digest() << '\n';
        journal->finish();
    }
    std::cout << "processed=" << count << " trace_digest=" << digest.text() << " state_digest=" << snapshot.digest() << '\n';
    return 0;
}

int replay(const Args& args) {
    auto journal = input_file(require(args, "--journal"));
    auto events = input_file(require(args, "--expect"));
    auto state = input_file(require(args, "--expect-state"));
    hpts::require_distinct_files({journal.identity(), events.identity(), state.identity()});
    const auto config = hpts::read_config(journal, true);
    hpts::Engine engine(config.engine);
    Digest digest;
    const auto header = trace_header(config);
    compare_bytes(events, header);
    digest.update(header);
    std::size_t count = 0;
    std::string line;
    std::vector<std::string> footer;
    while (hpts::bounded_line(journal, line)) {
        const auto parts = hpts::tokens(line);
        if (parts.empty()) continue;
        if (parts.front() == "END") { footer = parts; break; }
        if (++count > max_input_commands) throw std::runtime_error("journal exceeds session command bound");
        const auto canonical = engine.process(hpts::parse_command(parts)).canonical();
        compare_bytes(events, canonical);
        digest.update(canonical);
    }
    if (footer.size() != 4 || footer[1] != std::to_string(count) || footer[2] != digest.text())
        throw std::runtime_error("incomplete journal or invalid completion footer");
    while (hpts::bounded_line(journal, line))
        if (!hpts::tokens(line).empty()) throw std::runtime_error("data after journal completion footer");
    check_engine(engine);
    const auto snapshot = engine.snapshot();
    if (footer[3] != snapshot.digest()) throw std::runtime_error("journal final state digest mismatch");
    compare_bytes(state, snapshot.canonical());
    require_eof(events);
    require_eof(state);
    std::cout << "replay verified: commands=" << count << " trace_digest=" << digest.text() << " state_digest=" << snapshot.digest() << '\n';
    return 0;
}

int interactive(const Args& args) {
    const ConfigSource source(require(args, "--config"));
    const auto& config = source.config;
    hpts::Engine engine(config.engine);
    std::cout << "SIM ready. Enter help for commands.\n";
    std::string line;
    std::size_t count = 0;
    while (hpts::bounded_line(std::cin, line)) {
        const auto parts = hpts::tokens(line);
        if (parts.empty()) continue;
        if (parts.size() == 1 && parts[0] == "quit") break;
        if (parts.size() == 1 && parts[0] == "help") { std::cout << help; continue; }
        if (parts.size() == 1 && (parts[0] == "book" || parts[0] == "accounts" || parts[0] == "state")) {
            auto snapshot = engine.snapshot();
            if (parts[0] == "state") std::cout << snapshot.canonical();
            else if (parts[0] == "accounts") {
                for (const auto& account : snapshot.accounts)
                    std::cout << account.id << " cash=" << account.cash << " position=" << account.position
                              << " limits=[" << account.min_position << ',' << account.max_position << "] reserved_cash="
                              << account.reserved_cash << " open_buys=" << account.reserved_buy_quantity
                              << " open_sells=" << account.reserved_sell_quantity << '\n';
                std::cout << "kill=" << (snapshot.kill_switch ? "on" : "off") << " sequence=" << snapshot.sequence << '\n';
            } else {
                std::sort(snapshot.orders.begin(), snapshot.orders.end(), [](const auto& a, const auto& b) {
                    if (a.side != b.side) return a.side == hpts::Side::Buy;
                    if (a.price != b.price) return a.side == hpts::Side::Buy ? a.price > b.price : a.price < b.price;
                    return a.priority < b.priority;
                });
                std::cout << "SIM book (best price, then FIFO):\n";
                for (const auto& order : snapshot.orders)
                    std::cout << hpts::to_string(order.side) << ' ' << order.remaining << " @ " << order.price
                              << " order=" << order.id << " account=" << order.account_id << " priority=" << order.priority << '\n';
                if (snapshot.orders.empty()) std::cout << "empty\n";
            }
            continue;
        }
        hpts::Command command;
        try {
            command = hpts::parse_command(parts);
        } catch (const std::runtime_error& error) {
            std::cerr << "input error: " << error.what() << '\n';
            continue;
        }
        if (++count > max_input_commands) throw std::runtime_error("interactive session command bound reached");
        // Infrastructure exceptions terminate the session; only parse failures
        // above are recoverable without consulting authoritative state.
        std::cout << engine.process(command).canonical();
    }
    check_engine(engine);
    return 0;
}

std::uint64_t nanoseconds(Clock::time_point start, Clock::time_point end) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

std::string json_string(const std::string& value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '"';
    for (const unsigned char character : value) {
        if (character == '\\' || character == '"') out << '\\' << static_cast<char>(character);
        else if (character < 32) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(character) << std::dec;
        else out << static_cast<char>(character);
    }
    out << '"';
    return out.str();
}

std::string percentiles(std::vector<std::uint64_t>& samples) {
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](std::size_t p) { return samples.empty() ? std::uint64_t{0} : samples[(samples.size() * p + 99) / 100 - 1]; };
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"p50\":" << percentile(50) << ",\"p95\":" << percentile(95) << ",\"p99\":" << percentile(99) << '}';
    return out.str();
}

std::uint64_t peak_memory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) throw std::runtime_error("GetProcessMemoryInfo failed");
    return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage failed");
#ifdef __APPLE__
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}

double process_cpu_seconds() {
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) throw std::runtime_error("GetProcessTimes failed");
    ULARGE_INTEGER kernel_ticks{}, user_ticks{};
    kernel_ticks.LowPart = kernel.dwLowDateTime;
    kernel_ticks.HighPart = kernel.dwHighDateTime;
    user_ticks.LowPart = user.dwLowDateTime;
    user_ticks.HighPart = user.dwHighDateTime;
    return static_cast<double>(kernel_ticks.QuadPart + user_ticks.QuadPart) / 10000000.0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage failed");
    return static_cast<double>(usage.ru_utime.tv_sec) + static_cast<double>(usage.ru_stime.tv_sec) +
           static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
#endif
}

std::string compiler() {
#ifdef _MSC_VER
    return "MSVC " + std::to_string(_MSC_FULL_VER);
#else
    return __VERSION__;
#endif
}

struct Request { hpts::Command command; Clock::time_point start; };
struct Response { hpts::Outcome outcome; Clock::time_point start; std::uint64_t core_ns = 0; bool submit = false; };

int benchmark(const Args& args) {
    const ConfigSource source(require(args, "--config"));
    const auto& config = source.config;
    auto input = input_file(require(args, "--input"));
    const std::string mode = args.contains("--mode") ? args.at("--mode") : "direct";
    if (mode != "direct" && mode != "queued") throw std::runtime_error("mode must be direct or queued");
    std::vector<hpts::Command> commands;
    std::string line;
    while (hpts::bounded_line(input, line)) {
        const auto parts = hpts::tokens(line);
        if (parts.empty()) continue;
        if (commands.size() == max_input_commands) throw std::runtime_error("benchmark exceeds command session bound");
        commands.push_back(hpts::parse_command(parts));
    }
    if (commands.empty()) throw std::runtime_error("benchmark needs at least one command");
    hpts::Engine engine(config.engine);
    std::unique_ptr<hpts::OutputFile> report_output;
    if (args.contains("--output")) report_output = std::make_unique<hpts::OutputFile>(args.at("--output"));
    hpts::prepare_outputs({report_output.get()}, {source.input.identity(), input.identity()});
    Digest digest;
    digest.update(trace_header(config));
    std::vector<std::uint64_t> core, end_to_end;
    core.reserve(commands.size());
    end_to_end.reserve(commands.size());
    std::size_t accepted_orders = 0, accepted_commands = 0, executions = 0;
    hpts::BoundedQueue<Request> ingress(config.queue_capacity);
    hpts::BoundedQueue<Response> egress(config.queue_capacity);
    const auto consume = [&](Response& response) {
        const auto canonical = response.outcome.canonical();
        digest.update(canonical);
        core.push_back(response.core_ns);
        end_to_end.push_back(nanoseconds(response.start, Clock::now()));
        if (response.outcome.accepted) {
            ++accepted_commands;
            if (response.submit) ++accepted_orders;
        }
        for (const auto& event : response.outcome.events)
            if (event.type == hpts::EventType::Trade) ++executions;
    };
    const auto cpu_start = process_cpu_seconds();
    const auto start = Clock::now();
    if (mode == "direct") {
        for (const auto& command : commands) {
            const auto entry = Clock::now();
            auto outcome = engine.process(command);
            const auto complete = Clock::now();
            Response response{std::move(outcome), entry, nanoseconds(entry, complete), command.type == hpts::CommandType::Submit};
            consume(response);
        }
    } else {
        std::exception_ptr worker_error, sink_error;
        std::thread worker([&] {
            try {
                Request request;
                while (ingress.pop(request)) {
                    const auto entry = Clock::now();
                    auto outcome = engine.process(request.command);
                    const auto complete = Clock::now();
                    if (!egress.push({std::move(outcome), request.start, nanoseconds(entry, complete), request.command.type == hpts::CommandType::Submit})) break;
                }
            } catch (...) { worker_error = std::current_exception(); ingress.close(); }
            egress.close();
        });
        std::thread sink;
        try {
            sink = std::thread([&] {
                try { Response response; while (egress.pop(response)) consume(response); }
                catch (...) { sink_error = std::current_exception(); ingress.close(); egress.close(); }
            });
            for (const auto& command : commands)
                if (!ingress.push({command, Clock::now()})) break;
        } catch (...) {
            ingress.close(); egress.close(); worker.join(); if (sink.joinable()) sink.join(); throw;
        }
        ingress.close();
        worker.join();
        sink.join();
        if (worker_error) std::rethrow_exception(worker_error);
        if (sink_error) std::rethrow_exception(sink_error);
    }
    const auto end = Clock::now();
    const auto cpu_end = process_cpu_seconds();
    if (core.size() != commands.size()) throw std::runtime_error("benchmark did not receive every command outcome");
    check_engine(engine);
    const double elapsed = std::chrono::duration<double>(end - start).count();
    const auto snapshot = engine.snapshot();
    const auto in_stats = ingress.stats();
    const auto out_stats = egress.stats();
    std::ostringstream report;
    report.imbue(std::locale::classic());
    report << std::setprecision(12) << "{\n  \"schema_version\":1,\n  \"mode\":" << json_string(mode)
           << ",\n  \"commands\":" << commands.size() << ",\n  \"accepted_orders\":" << accepted_orders
           << ",\n  \"accepted_commands\":" << accepted_commands << ",\n  \"rejected_commands\":" << commands.size() - accepted_commands
           << ",\n  \"executions\":" << executions << ",\n  \"elapsed_seconds\":" << elapsed
           << ",\n  \"commands_per_second\":" << commands.size() / elapsed
           << ",\n  \"accepted_orders_per_second\":" << accepted_orders / elapsed
           << ",\n  \"executions_per_second\":" << executions / elapsed
           << ",\n  \"core_latency_ns\":" << percentiles(core)
           << ",\n  \"end_to_end_latency_ns\":" << percentiles(end_to_end)
           << ",\n  \"trace_digest\":" << json_string(digest.text()) << ",\n  \"state_digest\":" << json_string(snapshot.digest())
           << ",\n  \"resources\":{\"process_cpu_seconds\":" << cpu_end - cpu_start
           << ",\"peak_rss_bytes\":" << peak_memory() << ",\"allocations_per_command\":null}"
           << ",\n  \"queue\":{\"capacity\":" << config.queue_capacity << ",\"input_high_water\":" << in_stats.high_water
           << ",\"input_full_waits\":" << in_stats.full_waits << ",\"output_high_water\":" << out_stats.high_water
           << ",\"output_full_waits\":" << out_stats.full_waits << ",\"dropped_commands\":0}"
           << ",\n  \"environment\":{\"compiler\":" << json_string(compiler())
           << ",\"hardware_threads\":" << std::thread::hardware_concurrency()
#ifdef _WIN32
           << ",\"platform\":\"Windows\""
#else
           << ",\"platform\":\"POSIX\""
#endif
#ifdef NDEBUG
           << ",\"assertions\":false"
#else
           << ",\"assertions\":true"
#endif
           << ",\"warmup_commands\":0},\n  \"latency_boundaries\":{\"core\":\"process call\",\"end_to_end\":\"before queue admission through canonical serialization and trace digest; in-process\"}\n}\n";
    if (report_output) { *report_output << report.str(); report_output->finish(); }
    else std::cout << report.str();
    return 0;
}

int checked_success(int result = 0) {
    // Destruction-time flushing cannot report a failed pipe/file to main.
    // Keep the final flush inside main's exception boundary for every mode.
    std::cout.flush();
    return result;
}
}  // namespace

int main(int argc, char* argv[]) {
    std::cout.imbue(std::locale::classic());
    std::cerr.imbue(std::locale::classic());
    std::cout.exceptions(std::ios::badbit | std::ios::failbit);
    try {
#ifndef _WIN32
        // Convert EPIPE into the documented checked I/O error exit rather than
        // process termination by SIGPIPE. No network transport is installed.
        if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) throw std::runtime_error("cannot configure broken-pipe handling");
#endif
        if (argc == 1 || (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))) { std::cout << help; return checked_success(); }
        if (argc == 2 && std::string(argv[1]) == "--version") { std::cout << "hpts 0.1.0 rules=1 canonical=1\n"; return checked_success(); }
        const std::string mode = argv[1];
        Args args;
        if (mode == "run") args = arguments(argc, argv, {"--config", "--input", "--output", "--state", "--journal"});
        else if (mode == "replay") args = arguments(argc, argv, {"--journal", "--expect", "--expect-state"});
        else if (mode == "bench") args = arguments(argc, argv, {"--config", "--input", "--output", "--mode"});
        else if (mode == "interactive") args = arguments(argc, argv, {"--config"});
        else throw std::runtime_error("unknown mode: " + mode);
        // Check every required and supplied optional value before any file is
        // opened. Destination admission then uses actual retained-handle IDs.
        for (const auto& [key, value] : args) { (void)value; require(args, key); }
        if (mode == "run") {
            for (const auto* key : {"--config", "--input", "--output", "--state"}) require(args, key);
        } else if (mode == "replay") {
            for (const auto* key : {"--journal", "--expect", "--expect-state"}) require(args, key);
        } else {
            require(args, "--config");
            if (mode == "bench") require(args, "--input");
        }
        if (mode == "run") return checked_success(run(args));
        if (mode == "replay") return checked_success(replay(args));
        if (mode == "bench") return checked_success(benchmark(args));
        return checked_success(interactive(args));
    } catch (const std::exception& error) {
        // cerr is normally tied to cout. Do not flush a failed stdout again
        // while reporting its exception or during process shutdown.
        std::cout.exceptions(std::ios::goodbit);
        std::cerr.tie(nullptr);
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
