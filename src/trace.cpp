#include "cachesim/trace.hpp"

#include <cstdio>

namespace cachesim {

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

bool parse_trace_line(const std::string& line, TraceRecord& record) {
    std::size_t pos = 0;
    const std::size_t size = line.size();
    while (pos < size && is_space(line[pos])) ++pos;
    if (pos == size || line[pos] == '#') return false;

    const char op = line[pos++];
    if (op == 'R' || op == 'r') {
        record.type = AccessType::Read;
    } else if (op == 'W' || op == 'w') {
        record.type = AccessType::Write;
    } else {
        throw std::invalid_argument("expected 'R' or 'W', got '" + std::string(1, op) + "'");
    }

    if (pos == size || !is_space(line[pos])) {
        throw std::invalid_argument("expected a space after the access type");
    }
    while (pos < size && is_space(line[pos])) ++pos;
    if (pos + 1 < size && line[pos] == '0' && (line[pos + 1] == 'x' || line[pos + 1] == 'X')) pos += 2;

    std::uint64_t address = 0;
    std::size_t digits = 0;
    while (pos < size && hex_digit(line[pos]) >= 0) {
        address = (address << 4) | static_cast<std::uint64_t>(hex_digit(line[pos]));
        ++pos;
        ++digits;
    }
    if (digits == 0) throw std::invalid_argument("missing hexadecimal address");
    if (digits > 16) throw std::invalid_argument("address does not fit in 64 bits");

    while (pos < size && is_space(line[pos])) ++pos;
    if (pos != size) throw std::invalid_argument("unexpected text after the address");

    record.address = address;
    return true;
}

void write_trace_record(std::ostream& out, AccessType type, std::uint64_t address) {
    char buffer[32];
    const int length = std::snprintf(buffer, sizeof(buffer), "%c 0x%llx\n", type == AccessType::Read ? 'R' : 'W',
                                     static_cast<unsigned long long>(address));
    out.write(buffer, length);
}

}  // namespace cachesim
