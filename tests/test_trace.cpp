// Trace file format and command-line parsing tests.
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cachesim/matmul.hpp"
#include "cachesim/options.hpp"
#include "cachesim/trace.hpp"
#include "test_framework.hpp"

using namespace cachesim;

TEST(parses_read_and_write_lines) {
    TraceRecord record;
    CHECK(parse_trace_line("R 0x1000", record));
    CHECK(record.type == AccessType::Read);
    CHECK_EQ(record.address, 0x1000u);

    CHECK(parse_trace_line("W 0xDEADbeef", record));
    CHECK(record.type == AccessType::Write);
    CHECK_EQ(record.address, 0xDEADBEEFu);

    CHECK(parse_trace_line("  w   ff  \r", record));  // lower case, no 0x, Windows line ending
    CHECK(record.type == AccessType::Write);
    CHECK_EQ(record.address, 0xFFu);

    CHECK(parse_trace_line("R 0xffffffffffffffff", record));
    CHECK_EQ(record.address, ~std::uint64_t{0});
}

TEST(skips_blank_and_comment_lines) {
    TraceRecord record;
    CHECK(!parse_trace_line("", record));
    CHECK(!parse_trace_line("   \t\r", record));
    CHECK(!parse_trace_line("# a comment", record));
}

TEST(rejects_malformed_lines) {
    TraceRecord record;
    CHECK_THROWS(parse_trace_line("X 0x10", record));               // unknown access type
    CHECK_THROWS(parse_trace_line("R", record));                    // no address
    CHECK_THROWS(parse_trace_line("R0x10", record));                // no space
    CHECK_THROWS(parse_trace_line("R 0xZZ", record));               // not hexadecimal
    CHECK_THROWS(parse_trace_line("R 0x10 extra", record));         // trailing text
    CHECK_THROWS(parse_trace_line("R 0x10000000000000000", record)); // 65 bits
}

TEST(read_trace_reports_the_bad_line_number) {
    std::istringstream input("R 0x0\n# fine\nbogus\n");
    std::string message;
    try {
        read_trace(input, [](AccessType, std::uint64_t) {});
    } catch (const std::invalid_argument& error) {
        message = error.what();
    }
    CHECK(message.find("line 3") != std::string::npos);
}

TEST(trace_file_gives_same_result_as_direct_simulation) {
    // Write a tiled trace as text, read it back through a cache, and compare
    // with feeding the generator straight into an identical cache.
    CacheConfig config;
    config.size_bytes = 1024;
    config.block_bytes = 32;
    config.associativity = 2;
    const MatrixLayout layout(12);

    std::stringstream text;
    text << "# header comment\n";
    trace_matmul_tiled(layout, 5,
                       [&text](AccessType type, std::uint64_t address) { write_trace_record(text, type, address); });

    Cache from_file(config);
    const std::uint64_t count =
        read_trace(text, [&from_file](AccessType type, std::uint64_t address) { from_file.access(address, type); });

    Cache direct(config);
    trace_matmul_tiled(layout, 5, [&direct](AccessType type, std::uint64_t address) { direct.access(address, type); });

    CHECK_EQ(count, tiled_access_count(12, 5));
    CHECK_EQ(from_file.stats().accesses, direct.stats().accesses);
    CHECK_EQ(from_file.stats().hits, direct.stats().hits);
    CHECK_EQ(from_file.stats().misses, direct.stats().misses);
    CHECK_EQ(from_file.stats().write_backs, direct.stats().write_backs);
    CHECK(direct.stats().misses > 0);
}

TEST(parse_size_understands_suffixes) {
    CHECK_EQ(parse_size("64"), 64u);
    CHECK_EQ(parse_size("512B"), 512u);
    CHECK_EQ(parse_size("32K"), 32768u);
    CHECK_EQ(parse_size("32kb"), 32768u);
    CHECK_EQ(parse_size("1M"), 1048576u);
    CHECK_EQ(parse_size("2MiB"), 2097152u);
    CHECK_THROWS(parse_size(""));
    CHECK_THROWS(parse_size("K"));
    CHECK_THROWS(parse_size("32X"));
    CHECK_THROWS(parse_size("-4"));
}

TEST(parse_associativity_and_policy) {
    CHECK_EQ(parse_associativity("1"), 1u);
    CHECK_EQ(parse_associativity("16"), 16u);
    CHECK_EQ(parse_associativity("full"), 0u);
    CHECK_EQ(parse_associativity("FULL"), 0u);
    CHECK_THROWS(parse_associativity("0"));
    CHECK_THROWS(parse_associativity("two"));

    CHECK(parse_policy("lru") == Policy::LRU);
    CHECK(parse_policy("FIFO") == Policy::FIFO);
    CHECK_THROWS(parse_policy("random"));
}

TEST(arg_parser_accepts_both_value_styles) {
    const char* argv[] = {"prog", "--size", "32K", "--assoc=full", "--csv"};
    const ArgParser args(5, argv, "size assoc policy", "csv help");
    CHECK_EQ(args.get("size", ""), std::string("32K"));
    CHECK_EQ(args.get("assoc", ""), std::string("full"));
    CHECK_EQ(args.get("policy", "lru"), std::string("lru"));  // fallback
    CHECK(args.has("csv"));
    CHECK(!args.has("help"));
}

TEST(arg_parser_rejects_bad_input) {
    const char* unknown[] = {"prog", "--bogus", "1"};
    CHECK_THROWS(ArgParser(3, unknown, "size", "csv"));
    const char* missing_value[] = {"prog", "--size"};
    CHECK_THROWS(ArgParser(2, missing_value, "size", "csv"));
    const char* positional[] = {"prog", "trace.txt"};
    CHECK_THROWS(ArgParser(2, positional, "size", "csv"));
    const char* switch_with_value[] = {"prog", "--csv=yes"};
    CHECK_THROWS(ArgParser(2, switch_with_value, "size", "csv"));
}
