#include "cachesim/cache.hpp"

#include <stdexcept>

namespace cachesim {

namespace {

bool is_power_of_two(std::uint64_t value) { return value != 0 && (value & (value - 1)) == 0; }

std::uint64_t log2_exact(std::uint64_t value) {
    std::uint64_t shift = 0;
    while ((std::uint64_t{1} << shift) < value) ++shift;
    return shift;
}

}  // namespace

const char* to_string(Policy policy) { return policy == Policy::LRU ? "LRU" : "FIFO"; }

void CacheConfig::validate() const {
    if (!is_power_of_two(block_bytes)) {
        throw std::invalid_argument("block size must be a power of two");
    }
    if (size_bytes == 0 || size_bytes % block_bytes != 0) {
        throw std::invalid_argument("cache size must be a non-zero multiple of the block size");
    }
    if (ways() > num_blocks()) {
        throw std::invalid_argument("associativity cannot exceed the number of blocks in the cache");
    }
    if (num_blocks() % ways() != 0) {
        throw std::invalid_argument("number of blocks must be a multiple of the associativity");
    }
    if (!is_power_of_two(num_sets())) {
        throw std::invalid_argument("number of sets (size / block / associativity) must be a power of two");
    }
}

Cache::Cache(const CacheConfig& config) : config_(config) {
    config_.validate();
    ways_ = config_.ways();
    block_shift_ = log2_exact(config_.block_bytes);
    set_shift_ = log2_exact(config_.num_sets());
    set_mask_ = config_.num_sets() - 1;

    tags_.assign(config_.num_blocks(), 0);
    stamps_.assign(config_.num_blocks(), 0);
    dirty_.assign(config_.num_blocks(), 0);
    used_.assign(config_.num_sets(), 0);

    use_index_ = ways_ > kLinearScanWays;
    if (use_index_) index_.reserve(config_.num_blocks());
}

std::uint64_t Cache::find_way(std::uint64_t set, std::uint64_t tag, std::uint64_t block) const {
    if (use_index_) {
        const auto it = index_.find(block);
        return it == index_.end() ? kNotFound : it->second;
    }
    const std::uint64_t* tags = &tags_[set * ways_];
    const std::uint64_t used = used_[set];
    for (std::uint64_t way = 0; way < used; ++way) {
        if (tags[way] == tag) return way;
    }
    return kNotFound;
}

// Both policies evict the block with the oldest stamp; they differ only in
// when the stamp is refreshed (see access()).
std::uint64_t Cache::pick_victim(std::uint64_t set) const {
    const std::uint64_t* stamps = &stamps_[set * ways_];
    std::uint64_t victim = 0;
    for (std::uint64_t way = 1; way < ways_; ++way) {
        if (stamps[way] < stamps[victim]) victim = way;
    }
    return victim;
}

bool Cache::access(std::uint64_t address, AccessType type) {
    // address = | tag | set index | block offset |
    const std::uint64_t block = address >> block_shift_;
    const std::uint64_t set = block & set_mask_;
    const std::uint64_t tag = block >> set_shift_;
    const bool is_write = type == AccessType::Write;

    ++clock_;
    ++stats_.accesses;
    ++(is_write ? stats_.writes : stats_.reads);

    std::uint64_t way = find_way(set, tag, block);
    if (way != kNotFound) {
        const std::uint64_t frame = set * ways_ + way;
        ++stats_.hits;
        if (config_.policy == Policy::LRU) stamps_[frame] = clock_;
        if (is_write) dirty_[frame] = 1;
        return true;
    }

    ++stats_.misses;
    ++(is_write ? stats_.write_misses : stats_.read_misses);

    if (used_[set] < ways_) {
        way = used_[set]++;
    } else {
        way = pick_victim(set);
        const std::uint64_t frame = set * ways_ + way;
        ++stats_.evictions;
        if (dirty_[frame]) ++stats_.write_backs;
        if (use_index_) index_.erase((tags_[frame] << set_shift_) | set);
    }

    // Write-allocate: the block is loaded on a write miss too, then modified.
    const std::uint64_t frame = set * ways_ + way;
    tags_[frame] = tag;
    stamps_[frame] = clock_;
    dirty_[frame] = is_write ? 1 : 0;
    if (use_index_) index_[block] = way;
    return false;
}

std::uint64_t Cache::dirty_blocks() const {
    std::uint64_t count = 0;
    for (std::uint64_t set = 0; set < used_.size(); ++set) {
        for (std::uint64_t way = 0; way < used_[set]; ++way) count += dirty_[set * ways_ + way];
    }
    return count;
}

}  // namespace cachesim
