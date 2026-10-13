// cachesim: runs a memory trace through a configurable cache and reports
// hits, misses and write-backs.
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "cachesim/cache.hpp"
#include "cachesim/matmul.hpp"
#include "cachesim/options.hpp"
#include "cachesim/trace.hpp"

namespace {

using namespace cachesim;

const char* const kUsage = R"(Usage: cachesim [options] (--trace FILE | --workload naive|tiled)

Cache configuration:
  --size BYTES      Total cache size, e.g. 32K, 1M            (default 32K)
  --block BYTES     Block (line) size, a power of two         (default 64)
  --assoc N|full    Blocks per set: 1 = direct-mapped,
                    'full' = fully associative                (default 8)
  --policy NAME     Replacement policy: lru or fifo           (default lru)

Input (choose one):
  --trace FILE      Read a text trace ("R 0x1000" / "W 0x1000" per line).
                    Use '-' to read from standard input.
  --workload NAME   Generate a matrix-multiply trace in memory: naive or tiled
  --n N             Matrix dimension for --workload           (default 64)
  --tile T          Tile size for --workload tiled            (default 16)

Output:
  --csv             Print one CSV header line and one data line
  --help            Show this message

Writes use write-back with write-allocate.
)";

struct Source {
    std::string name;      // "trace", "naive" or "tiled"
    std::uint64_t n = 0;   // 0 when not a generated workload
    std::uint64_t tile = 0;
};

void print_report(const Cache& cache, const Source& source) {
    const CacheConfig& config = cache.config();
    const CacheStats& stats = cache.stats();

    std::printf("Cache        : %llu bytes, %llu-byte blocks, ", static_cast<unsigned long long>(config.size_bytes),
                static_cast<unsigned long long>(config.block_bytes));
    if (config.ways() == 1) {
        std::printf("direct-mapped");
    } else if (config.num_sets() == 1) {
        std::printf("fully associative (%llu ways)", static_cast<unsigned long long>(config.ways()));
    } else {
        std::printf("%llu-way", static_cast<unsigned long long>(config.ways()));
    }
    std::printf(", %llu sets, %s, write-back + write-allocate\n", static_cast<unsigned long long>(config.num_sets()),
                to_string(config.policy));

    if (source.n == 0) {
        std::printf("Input        : trace file\n");
    } else if (source.name == "tiled") {
        std::printf("Input        : tiled matrix multiply, N=%llu, tile=%llu\n", static_cast<unsigned long long>(source.n),
                    static_cast<unsigned long long>(source.tile));
    } else {
        std::printf("Input        : naive matrix multiply, N=%llu\n", static_cast<unsigned long long>(source.n));
    }

    std::printf("Accesses     : %llu (%llu reads, %llu writes)\n", static_cast<unsigned long long>(stats.accesses),
                static_cast<unsigned long long>(stats.reads), static_cast<unsigned long long>(stats.writes));
    std::printf("Hits         : %llu\n", static_cast<unsigned long long>(stats.hits));
    std::printf("Misses       : %llu (%llu read, %llu write)\n", static_cast<unsigned long long>(stats.misses),
                static_cast<unsigned long long>(stats.read_misses), static_cast<unsigned long long>(stats.write_misses));
    std::printf("Miss rate    : %.4f%%\n", stats.miss_rate() * 100.0);
    std::printf("Evictions    : %llu\n", static_cast<unsigned long long>(stats.evictions));
    std::printf("Write-backs  : %llu (+%llu dirty blocks still cached at the end)\n",
                static_cast<unsigned long long>(stats.write_backs), static_cast<unsigned long long>(cache.dirty_blocks()));
}

void print_csv(const Cache& cache, const Source& source) {
    const CacheConfig& config = cache.config();
    const CacheStats& stats = cache.stats();
    std::printf("workload,n,tile,cache_bytes,block_bytes,ways,sets,policy,accesses,reads,writes,hits,misses,"
                "miss_rate,read_misses,write_misses,evictions,write_backs,dirty_at_end\n");
    std::printf("%s,%llu,%llu,%llu,%llu,%llu,%llu,%s,%llu,%llu,%llu,%llu,%llu,%.8f,%llu,%llu,%llu,%llu,%llu\n",
                source.name.c_str(), static_cast<unsigned long long>(source.n),
                static_cast<unsigned long long>(source.tile), static_cast<unsigned long long>(config.size_bytes),
                static_cast<unsigned long long>(config.block_bytes), static_cast<unsigned long long>(config.ways()),
                static_cast<unsigned long long>(config.num_sets()), to_string(config.policy),
                static_cast<unsigned long long>(stats.accesses), static_cast<unsigned long long>(stats.reads),
                static_cast<unsigned long long>(stats.writes), static_cast<unsigned long long>(stats.hits),
                static_cast<unsigned long long>(stats.misses), stats.miss_rate(),
                static_cast<unsigned long long>(stats.read_misses), static_cast<unsigned long long>(stats.write_misses),
                static_cast<unsigned long long>(stats.evictions), static_cast<unsigned long long>(stats.write_backs),
                static_cast<unsigned long long>(cache.dirty_blocks()));
}

int run(int argc, char** argv) {
    const ArgParser args(argc, argv, "size block assoc policy trace workload n tile", "csv help");
    if (args.has("help") || argc == 1) {
        std::fputs(kUsage, stdout);
        return 0;
    }
    if (args.has("trace") == args.has("workload")) {
        throw std::invalid_argument("give exactly one of --trace or --workload");
    }

    CacheConfig config;
    config.size_bytes = parse_size(args.get("size", "32K"));
    config.block_bytes = parse_size(args.get("block", "64"));
    config.associativity = parse_associativity(args.get("assoc", "8"));
    config.policy = parse_policy(args.get("policy", "lru"));

    Cache cache(config);
    const auto sink = [&cache](AccessType type, std::uint64_t address) { cache.access(address, type); };

    Source source;
    if (args.has("trace")) {
        if (args.has("n") || args.has("tile")) throw std::invalid_argument("--n and --tile only apply to --workload");
        source.name = "trace";
        const std::string path = args.get("trace", "");
        if (path == "-") {
            read_trace(std::cin, sink);
        } else {
            std::ifstream file(path);
            if (!file) throw std::runtime_error("cannot open trace file '" + path + "'");
            read_trace(file, sink);
        }
    } else {
        source.name = args.get("workload", "");
        source.n = parse_count(args.get("n", "64"));
        const MatrixLayout layout(source.n);
        if (source.name == "naive") {
            if (args.has("tile")) throw std::invalid_argument("--tile only applies to --workload tiled");
            trace_matmul_naive(layout, sink);
        } else if (source.name == "tiled") {
            source.tile = parse_count(args.get("tile", "16"));
            trace_matmul_tiled(layout, source.tile, sink);
        } else {
            throw std::invalid_argument("workload must be 'naive' or 'tiled', got '" + source.name + "'");
        }
    }

    if (args.has("csv")) {
        print_csv(cache, source);
    } else {
        print_report(cache, source);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "cachesim: %s\nTry 'cachesim --help'.\n", error.what());
        return 1;
    }
}
