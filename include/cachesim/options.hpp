// Small helpers for turning command-line text into configuration values.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "cachesim/cache.hpp"

namespace cachesim {

// "4096", "32K", "32KB", "1M", "1MiB" -> bytes. Suffixes are powers of 1024.
std::uint64_t parse_size(const std::string& text);

// A plain positive integer such as a matrix or tile size.
std::uint64_t parse_count(const std::string& text);

// "1", "8", ... or "full" (returned as 0, CacheConfig's fully associative marker).
std::uint64_t parse_associativity(const std::string& text);

// "lru" or "fifo", case-insensitive.
Policy parse_policy(const std::string& text);

// Collects "--name value", "--name=value" and bare "--flag" arguments.
// Flags listed in `switches` take no value. Throws std::invalid_argument on
// anything unexpected, including names not in `known`.
class ArgParser {
public:
    ArgParser(int argc, const char* const* argv, const std::string& known, const std::string& switches);

    bool has(const std::string& name) const { return values_.count(name) != 0; }
    std::string get(const std::string& name, const std::string& fallback) const;

private:
    std::map<std::string, std::string> values_;
};

}  // namespace cachesim
