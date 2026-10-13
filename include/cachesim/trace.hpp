// Text trace format: one access per line, "R <hex address>" or "W <hex address>".
// Blank lines and lines starting with '#' are ignored.
#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>

#include "cachesim/cache.hpp"

namespace cachesim {

struct TraceRecord {
    AccessType type = AccessType::Read;
    std::uint64_t address = 0;
};

// Parses one line. Returns false for blank and comment lines; throws
// std::invalid_argument for anything malformed.
bool parse_trace_line(const std::string& line, TraceRecord& record);

void write_trace_record(std::ostream& out, AccessType type, std::uint64_t address);

// Reads a whole trace, calling sink(type, address) per access. Returns the
// number of accesses read. Errors are reported with the offending line number.
template <class Sink>
std::uint64_t read_trace(std::istream& in, Sink&& sink) {
    std::string line;
    TraceRecord record;
    std::uint64_t line_number = 0;
    std::uint64_t count = 0;
    while (std::getline(in, line)) {
        ++line_number;
        try {
            if (!parse_trace_line(line, record)) continue;
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("trace line " + std::to_string(line_number) + ": " + error.what());
        }
        sink(record.type, record.address);
        ++count;
    }
    return count;
}

}  // namespace cachesim
