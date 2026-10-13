#include "cachesim/options.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace cachesim {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Parses leading decimal digits and returns the position just past them.
std::size_t parse_digits(const std::string& text, std::uint64_t& value) {
    std::size_t pos = 0;
    value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        const std::uint64_t digit = static_cast<std::uint64_t>(text[pos] - '0');
        if (value > (~std::uint64_t{0} - digit) / 10) throw std::invalid_argument("number too large: '" + text + "'");
        value = value * 10 + digit;
        ++pos;
    }
    return pos;
}

bool in_list(const std::string& list, const std::string& name) {
    std::istringstream stream(list);
    std::string item;
    while (stream >> item) {
        if (item == name) return true;
    }
    return false;
}

}  // namespace

std::uint64_t parse_size(const std::string& text) {
    std::uint64_t value = 0;
    const std::size_t pos = parse_digits(text, value);
    if (pos == 0) throw std::invalid_argument("invalid size: '" + text + "'");

    const std::string suffix = lower(text.substr(pos));
    std::uint64_t scale = 1;
    if (suffix == "k" || suffix == "kb" || suffix == "kib") {
        scale = std::uint64_t{1} << 10;
    } else if (suffix == "m" || suffix == "mb" || suffix == "mib") {
        scale = std::uint64_t{1} << 20;
    } else if (suffix == "g" || suffix == "gb" || suffix == "gib") {
        scale = std::uint64_t{1} << 30;
    } else if (!suffix.empty() && suffix != "b") {
        throw std::invalid_argument("invalid size suffix: '" + text + "'");
    }
    if (value > ~std::uint64_t{0} / scale) throw std::invalid_argument("size too large: '" + text + "'");
    return value * scale;
}

std::uint64_t parse_count(const std::string& text) {
    std::uint64_t value = 0;
    const std::size_t pos = parse_digits(text, value);
    if (pos == 0 || pos != text.size() || value == 0) {
        throw std::invalid_argument("expected a positive integer, got '" + text + "'");
    }
    return value;
}

std::uint64_t parse_associativity(const std::string& text) {
    if (lower(text) == "full") return 0;
    try {
        return parse_count(text);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("associativity must be a positive integer or 'full', got '" + text + "'");
    }
}

Policy parse_policy(const std::string& text) {
    const std::string name = lower(text);
    if (name == "lru") return Policy::LRU;
    if (name == "fifo") return Policy::FIFO;
    throw std::invalid_argument("policy must be 'lru' or 'fifo', got '" + text + "'");
}

ArgParser::ArgParser(int argc, const char* const* argv, const std::string& known, const std::string& switches) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0 || arg.size() == 2) {
            throw std::invalid_argument("unexpected argument '" + arg + "'");
        }
        const std::size_t equals = arg.find('=');
        const std::string name = arg.substr(2, equals == std::string::npos ? std::string::npos : equals - 2);

        if (in_list(switches, name)) {
            if (equals != std::string::npos) throw std::invalid_argument("--" + name + " does not take a value");
            values_[name] = "1";
        } else if (in_list(known, name)) {
            if (equals != std::string::npos) {
                values_[name] = arg.substr(equals + 1);
            } else if (i + 1 < argc) {
                values_[name] = argv[++i];
            } else {
                throw std::invalid_argument("--" + name + " needs a value");
            }
        } else {
            throw std::invalid_argument("unknown option '--" + name + "'");
        }
    }
}

std::string ArgParser::get(const std::string& name, const std::string& fallback) const {
    const auto it = values_.find(name);
    return it == values_.end() ? fallback : it->second;
}

}  // namespace cachesim
