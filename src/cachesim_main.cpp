// cachesim: runs a memory trace through a configurable cache and reports
// hits, misses and write-backs.
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
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
    std::ostream& out = std::cout;

    out << "Cache        : " << config.size_bytes << " bytes, " << config.block_bytes << "-byte blocks, ";
    if (config.ways() == 1) {
        out << "direct-mapped";
    } else if (config.num_sets() == 1) {
        out << "fully associative (" << config.ways() << " ways)";
    } else {
        out << config.ways() << "-way";
    }
    out << ", " << config.num_sets() << " sets, " << to_string(config.policy) << ", write-back + write-allocate\n";

    if (source.n == 0) {
        out << "Input        : trace file\n";
    } else if (source.name == "tiled") {
        out << "Input        : tiled matrix multiply, N=" << source.n << ", tile=" << source.tile << "\n";
    } else {
        out << "Input        : naive matrix multiply, N=" << source.n << "\n";
    }

    out << "Accesses     : " << stats.accesses << " (" << stats.reads << " reads, " << stats.writes << " writes)\n"
        << "Hits         : " << stats.hits << "\n"
        << "Misses       : " << stats.misses << " (" << stats.read_misses << " read, " << stats.write_misses
        << " write)\n"
        << "Miss rate    : " << std::fixed << std::setprecision(4) << stats.miss_rate() * 100.0 << "%\n"
        << "Evictions    : " << stats.evictions << "\n"
        << "Write-backs  : " << stats.write_backs << " (+" << cache.dirty_blocks()
        << " dirty blocks still cached at the end)\n";
}

void print_csv(const Cache& cache, const Source& source) {
    const CacheConfig& config = cache.config();
    const CacheStats& stats = cache.stats();
    std::cout << "workload,n,tile,cache_bytes,block_bytes,ways,sets,policy,accesses,reads,writes,hits,misses,"
                 "miss_rate,read_misses,write_misses,evictions,write_backs,dirty_at_end\n"
              << source.name << ',' << source.n << ',' << source.tile << ',' << config.size_bytes << ','
              << config.block_bytes << ',' << config.ways() << ',' << config.num_sets() << ','
              << to_string(config.policy) << ',' << stats.accesses << ',' << stats.reads << ',' << stats.writes << ','
              << stats.hits << ',' << stats.misses << ',' << std::fixed << std::setprecision(8) << stats.miss_rate()
              << ',' << stats.read_misses << ',' << stats.write_misses << ',' << stats.evictions << ','
              << stats.write_backs << ',' << cache.dirty_blocks() << '\n';
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
