// Memory-address trace generators for N x N matrix multiplication, C = A * B.
//
// Nothing is actually multiplied. The generators walk the same loops a real
// program would and report every memory access to a "sink" callback, so the
// trace can be written to a file or fed straight into a Cache.
//
// The access pattern models what an optimising compiler emits: the running sum
// for C[i][j] lives in a CPU register, so the inner loop only loads A and B.
#pragma once

#include <algorithm>
#include <cstdint>

#include "cachesim/cache.hpp"

namespace cachesim {

// Where the three matrices live in memory. Each is a row-major array of
// 8-byte doubles; the arrays sit one after another, each starting on a 4 KiB
// page boundary, the way a large allocation would.
struct MatrixLayout {
    static constexpr std::uint64_t kElementBytes = 8;
    static constexpr std::uint64_t kPageBytes = 4096;
    static constexpr std::uint64_t kBaseAddress = 0x10000000;

    std::uint64_t n = 0;
    std::uint64_t base_a = 0;
    std::uint64_t base_b = 0;
    std::uint64_t base_c = 0;

    explicit MatrixLayout(std::uint64_t size) : n(size) {
        const std::uint64_t bytes = n * n * kElementBytes;
        const std::uint64_t span = (bytes + kPageBytes - 1) / kPageBytes * kPageBytes;
        base_a = kBaseAddress;
        base_b = base_a + span;
        base_c = base_b + span;
    }

    std::uint64_t a(std::uint64_t i, std::uint64_t j) const { return base_a + (i * n + j) * kElementBytes; }
    std::uint64_t b(std::uint64_t i, std::uint64_t j) const { return base_b + (i * n + j) * kElementBytes; }
    std::uint64_t c(std::uint64_t i, std::uint64_t j) const { return base_c + (i * n + j) * kElementBytes; }
};

// Naive i-j-k triple loop:
//   for i, for j: sum = 0; for k: sum += A[i][k] * B[k][j];  C[i][j] = sum;
template <class Sink>
void trace_matmul_naive(const MatrixLayout& m, Sink&& sink) {
    for (std::uint64_t i = 0; i < m.n; ++i) {
        for (std::uint64_t j = 0; j < m.n; ++j) {
            for (std::uint64_t k = 0; k < m.n; ++k) {
                sink(AccessType::Read, m.a(i, k));
                sink(AccessType::Read, m.b(k, j));
            }
            sink(AccessType::Write, m.c(i, j));
        }
    }
}

// Tiled (cache-blocked) version. The same i-j-k loops run inside tile x tile
// sub-matrices, so the three tiles being worked on stay in the cache:
//   for ii, jj, kk (tile corners): for i, for j:
//     sum = C[i][j]; for k: sum += A[i][k] * B[k][j];  C[i][j] = sum;
// The tile size does not have to divide N.
template <class Sink>
void trace_matmul_tiled(const MatrixLayout& m, std::uint64_t tile, Sink&& sink) {
    for (std::uint64_t ii = 0; ii < m.n; ii += tile) {
        const std::uint64_t i_end = std::min(ii + tile, m.n);
        for (std::uint64_t jj = 0; jj < m.n; jj += tile) {
            const std::uint64_t j_end = std::min(jj + tile, m.n);
            for (std::uint64_t kk = 0; kk < m.n; kk += tile) {
                const std::uint64_t k_end = std::min(kk + tile, m.n);
                for (std::uint64_t i = ii; i < i_end; ++i) {
                    for (std::uint64_t j = jj; j < j_end; ++j) {
                        sink(AccessType::Read, m.c(i, j));
                        for (std::uint64_t k = kk; k < k_end; ++k) {
                            sink(AccessType::Read, m.a(i, k));
                            sink(AccessType::Read, m.b(k, j));
                        }
                        sink(AccessType::Write, m.c(i, j));
                    }
                }
            }
        }
    }
}

// Closed-form trace lengths, used to sanity-check the generators.
inline std::uint64_t naive_access_count(std::uint64_t n) { return 2 * n * n * n + n * n; }

inline std::uint64_t tiled_access_count(std::uint64_t n, std::uint64_t tile) {
    const std::uint64_t tiles_per_dim = (n + tile - 1) / tile;
    return 2 * n * n * n + 2 * n * n * tiles_per_dim;
}

}  // namespace cachesim
