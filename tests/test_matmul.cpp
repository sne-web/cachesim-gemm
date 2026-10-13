// Trace generator tests, using matrices small enough to list by hand.
#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "cachesim/matmul.hpp"
#include "test_framework.hpp"

using namespace cachesim;

namespace {

using Access = std::pair<char, std::uint64_t>;

// For N=2 each matrix is 32 bytes, rounded up to one 4 KiB page.
const std::uint64_t A = 0x10000000;
const std::uint64_t B = 0x10001000;
const std::uint64_t C = 0x10002000;

std::vector<Access> naive_trace(std::uint64_t n) {
    std::vector<Access> trace;
    trace_matmul_naive(MatrixLayout(n), [&trace](AccessType type, std::uint64_t address) {
        trace.emplace_back(type == AccessType::Read ? 'R' : 'W', address);
    });
    return trace;
}

std::vector<Access> tiled_trace(std::uint64_t n, std::uint64_t tile) {
    std::vector<Access> trace;
    trace_matmul_tiled(MatrixLayout(n), tile, [&trace](AccessType type, std::uint64_t address) {
        trace.emplace_back(type == AccessType::Read ? 'R' : 'W', address);
    });
    return trace;
}

// The A and B reads of a trace, sorted, so two traces can be compared
// regardless of the order they visit elements in.
std::vector<std::uint64_t> sorted_input_reads(const std::vector<Access>& trace, std::uint64_t n) {
    const MatrixLayout layout(n);
    std::vector<std::uint64_t> reads;
    for (const Access& access : trace) {
        if (access.second < layout.base_c) reads.push_back(access.second);
    }
    std::sort(reads.begin(), reads.end());
    return reads;
}

}  // namespace

TEST(layout_places_matrices_on_page_boundaries) {
    const MatrixLayout small(2);
    CHECK_EQ(small.base_a, A);
    CHECK_EQ(small.base_b, B);
    CHECK_EQ(small.base_c, C);
    CHECK_EQ(small.a(0, 1), A + 8);   // next element in the row: +8 bytes
    CHECK_EQ(small.a(1, 0), A + 16);  // next row: +N*8 bytes
    CHECK_EQ(small.c(1, 1), C + 24);

    // N=100: 80,000 bytes per matrix -> 20 pages (81,920 bytes) apart.
    const MatrixLayout larger(100);
    CHECK_EQ(larger.base_b - larger.base_a, 81920u);
    CHECK_EQ(larger.base_c - larger.base_b, 81920u);
}

TEST(naive_2x2_trace_is_exact) {
    // C[i][j] = A[i][0]*B[0][j] + A[i][1]*B[1][j]
    const std::vector<Access> expected = {
        {'R', A + 0},  {'R', B + 0}, {'R', A + 8},  {'R', B + 16}, {'W', C + 0},   // C[0][0]
        {'R', A + 0},  {'R', B + 8}, {'R', A + 8},  {'R', B + 24}, {'W', C + 8},   // C[0][1]
        {'R', A + 16}, {'R', B + 0}, {'R', A + 24}, {'R', B + 16}, {'W', C + 16},  // C[1][0]
        {'R', A + 16}, {'R', B + 8}, {'R', A + 24}, {'R', B + 24}, {'W', C + 24},  // C[1][1]
    };
    CHECK(naive_trace(2) == expected);
}

TEST(tiled_2x2_with_tile_1_is_exact) {
    // Tile size 1 visits (i, j, k) one at a time, updating C[i][j] in memory
    // for every k. Shown here: everything that contributes to C[0][0].
    const std::vector<Access> trace = tiled_trace(2, 1);
    const std::vector<Access> expected_start = {
        {'R', C + 0}, {'R', A + 0}, {'R', B + 0},  {'W', C + 0},  // k = 0
        {'R', C + 0}, {'R', A + 8}, {'R', B + 16}, {'W', C + 0},  // k = 1
    };
    CHECK_EQ(trace.size(), 32u);
    CHECK(std::vector<Access>(trace.begin(), trace.begin() + 8) == expected_start);
}

TEST(tiled_with_one_big_tile_matches_naive_order) {
    // With tile >= N there is a single tile, so the loops run in naive order;
    // the only difference is the extra read of C[i][j] before each inner loop.
    const std::vector<Access> naive = naive_trace(3);
    std::vector<Access> tiled = tiled_trace(3, 8);
    CHECK_EQ(tiled.size(), naive.size() + 9);
    tiled.erase(std::remove_if(tiled.begin(), tiled.end(),
                               [](const Access& access) { return access.first == 'R' && access.second >= 0x10002000; }),
                tiled.end());
    CHECK(tiled == naive);
}

TEST(trace_lengths_match_formulas) {
    CHECK_EQ(naive_access_count(2), 20u);     // 2*8 + 4
    CHECK_EQ(tiled_access_count(2, 2), 24u);  // 2*8 + 2*4*1
    CHECK_EQ(tiled_access_count(2, 1), 32u);  // 2*8 + 2*4*2
    CHECK_EQ(tiled_access_count(5, 2), 400u); // 2*125 + 2*25*3

    for (std::uint64_t n : {1, 2, 5, 12}) {
        CHECK_EQ(naive_trace(n).size(), naive_access_count(n));
        for (std::uint64_t tile : {1, 2, 3, 4, 16}) {
            CHECK_EQ(tiled_trace(n, tile).size(), tiled_access_count(n, tile));
        }
    }
}

TEST(tiling_only_reorders_the_work) {
    // Tiled must read exactly the same A and B elements the same number of
    // times as naive, including when the tile size does not divide N.
    for (std::uint64_t tile : {1, 2, 3, 4, 5, 7}) {
        CHECK(sorted_input_reads(tiled_trace(5, tile), 5) == sorted_input_reads(naive_trace(5), 5));
    }
}

TEST(tiled_writes_each_result_once_per_k_tile) {
    // N=5, tile=2 -> k is split into 3 tiles, so each C[i][j] is written 3 times.
    const MatrixLayout layout(5);
    std::map<std::uint64_t, int> writes;
    for (const Access& access : tiled_trace(5, 2)) {
        if (access.first == 'W') ++writes[access.second];
    }
    CHECK_EQ(writes.size(), 25u);
    for (const auto& entry : writes) {
        CHECK(entry.first >= layout.base_c);
        CHECK_EQ(entry.second, 3);
    }
}

TEST(matmul_in_a_huge_cache_only_misses_once_per_block) {
    // N=8: each matrix is 512 bytes = 8 blocks of 64 bytes. A 1 MiB cache
    // holds all 24 blocks, so the only misses are the 24 first touches.
    CacheConfig config;
    config.size_bytes = 1 << 20;
    for (const bool tiled : {false, true}) {
        Cache cache(config);
        const auto sink = [&cache](AccessType type, std::uint64_t address) { cache.access(address, type); };
        if (tiled) {
            trace_matmul_tiled(MatrixLayout(8), 4, sink);
        } else {
            trace_matmul_naive(MatrixLayout(8), sink);
        }
        CHECK_EQ(cache.stats().accesses, tiled ? tiled_access_count(8, 4) : naive_access_count(8));
        CHECK_EQ(cache.stats().misses, 24u);
        CHECK_EQ(cache.stats().evictions, 0u);
        CHECK_EQ(cache.stats().write_backs, 0u);
        CHECK_EQ(cache.dirty_blocks(), 8u);  // the 8 blocks of C
    }
}
