// tracegen: writes the memory-address trace of an N x N matrix multiplication.
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cachesim/matmul.hpp"
#include "cachesim/options.hpp"
#include "cachesim/trace.hpp"

namespace {

using namespace cachesim;

const char* const kUsage = R"(Usage: tracegen --algo naive|tiled [options]

  --algo NAME   naive (plain i-j-k loops) or tiled (cache-blocked)
  --n N         Matrix dimension                               (default 64)
  --tile T      Tile size for --algo tiled                     (default 16)
  --out FILE    Output file; omit or use '-' for standard output
  --help        Show this message

Output has one access per line: "R 0x<address>" or "W 0x<address>".
The trace has about 2*N^3 lines, so large N produces large files.
)";

int run(int argc, char** argv) {
    const ArgParser args(argc, argv, "algo n tile out", "help");
    if (args.has("help") || argc == 1) {
        std::fputs(kUsage, stdout);
        return 0;
    }
    if (!args.has("algo")) throw std::invalid_argument("--algo is required");

    const std::string algo = args.get("algo", "");
    const std::uint64_t n = parse_count(args.get("n", "64"));
    const std::uint64_t tile = parse_count(args.get("tile", "16"));
    if (algo != "naive" && algo != "tiled") {
        throw std::invalid_argument("algo must be 'naive' or 'tiled', got '" + algo + "'");
    }
    if (algo == "naive" && args.has("tile")) throw std::invalid_argument("--tile only applies to --algo tiled");

    const std::string path = args.get("out", "-");
    std::ofstream file;
    std::vector<char> buffer(1 << 20);
    if (path != "-") {
        file.rdbuf()->pubsetbuf(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        file.open(path, std::ios::binary);
        if (!file) throw std::runtime_error("cannot open output file '" + path + "'");
    }
    std::ostream& out = path == "-" ? std::cout : file;

    out << "# CacheSim trace: " << algo << " matrix multiply, N=" << n;
    if (algo == "tiled") out << ", tile=" << tile;
    out << "\n";

    const MatrixLayout layout(n);
    const auto sink = [&out](AccessType type, std::uint64_t address) { write_trace_record(out, type, address); };
    if (algo == "naive") {
        trace_matmul_naive(layout, sink);
    } else {
        trace_matmul_tiled(layout, tile, sink);
    }

    out.flush();
    if (!out) throw std::runtime_error("failed while writing the trace");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "tracegen: %s\nTry 'tracegen --help'.\n", error.what());
        return 1;
    }
}
