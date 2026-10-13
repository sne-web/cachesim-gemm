// A set-associative cache model: the core of CacheSim.
//
// The cache is described by three numbers (total size, block size and
// associativity) plus a replacement policy. It uses write-back with
// write-allocate: a write miss first loads the block, writes only mark the
// block dirty, and a dirty block is written back to memory when it is evicted.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cachesim {

enum class Policy { LRU, FIFO };
enum class AccessType { Read, Write };

const char* to_string(Policy policy);

struct CacheConfig {
    std::uint64_t size_bytes = 32 * 1024;
    std::uint64_t block_bytes = 64;
    // Blocks per set. 1 = direct-mapped, 0 = fully associative.
    std::uint64_t associativity = 8;
    Policy policy = Policy::LRU;

    std::uint64_t num_blocks() const { return size_bytes / block_bytes; }
    // Associativity with the "0 = fully associative" shorthand resolved.
    std::uint64_t ways() const { return associativity == 0 ? num_blocks() : associativity; }
    std::uint64_t num_sets() const { return num_blocks() / ways(); }

    // Throws std::invalid_argument describing the first problem found.
    void validate() const;
};

struct CacheStats {
    std::uint64_t accesses = 0;
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t read_misses = 0;
    std::uint64_t write_misses = 0;
    std::uint64_t evictions = 0;
    // Dirty blocks written back to memory because they were evicted.
    std::uint64_t write_backs = 0;

    double miss_rate() const {
        return accesses == 0 ? 0.0 : static_cast<double>(misses) / static_cast<double>(accesses);
    }
};

class Cache {
public:
    explicit Cache(const CacheConfig& config);

    // Simulates one memory access. Returns true on a hit.
    bool access(std::uint64_t address, AccessType type);

    // Number of blocks currently holding modified data that has not been
    // written back yet (they would be flushed when the program ends).
    std::uint64_t dirty_blocks() const;

    const CacheConfig& config() const { return config_; }
    const CacheStats& stats() const { return stats_; }

private:
    // Sets with more ways than this are searched through a hash index instead
    // of a linear scan, which keeps fully associative caches fast to simulate.
    static constexpr std::uint64_t kLinearScanWays = 16;
    static constexpr std::uint64_t kNotFound = ~std::uint64_t{0};

    std::uint64_t find_way(std::uint64_t set, std::uint64_t tag, std::uint64_t block) const;
    std::uint64_t pick_victim(std::uint64_t set) const;

    CacheConfig config_;
    CacheStats stats_;

    std::uint64_t ways_;
    std::uint64_t block_shift_;  // log2(block size)
    std::uint64_t set_shift_;    // log2(number of sets)
    std::uint64_t set_mask_;     // number of sets - 1

    // One entry per block frame, laid out as [set * ways + way].
    std::vector<std::uint64_t> tags_;
    // LRU: time of last use. FIFO: time the block was loaded.
    std::vector<std::uint64_t> stamps_;
    std::vector<std::uint8_t> dirty_;
    // Frames filled so far in each set; frames fill in order and are never
    // invalidated, so ways [0, used) are exactly the valid ones.
    std::vector<std::uint64_t> used_;

    bool use_index_;
    std::unordered_map<std::uint64_t, std::uint64_t> index_;  // block number -> way

    std::uint64_t clock_ = 0;
};

}  // namespace cachesim
