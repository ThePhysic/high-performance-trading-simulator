#pragma once

#include "hpts/engine.hpp"

#include <istream>
#include <string>
#include <vector>

namespace hpts {

struct AppConfig { EngineConfig engine; std::size_t queue_capacity = 256; };

// At most 4096 bytes per logical line. Returns false only on clean EOF.
bool bounded_line(std::istream& stream, std::string& line);
std::vector<std::string> tokens(const std::string& line);
AppConfig read_config(std::istream& stream, bool journal = false);
std::string config_text(const AppConfig& config);
Command parse_command(const std::vector<std::string>& fields);
std::string command_text(const Command& command);
std::string fingerprint(const std::string& text);

}  // namespace hpts
