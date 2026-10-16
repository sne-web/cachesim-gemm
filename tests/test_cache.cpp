// Cache behaviour tests. Every case is small enough to trace by hand; the
// comments show the expected outcome of each access.
#include "cachesim/cache.hpp"
#include "test_framework.hpp"

using namespace cachesim;

namespace {

const AccessType R = AccessType::Read;
const AccessType W = AccessType::Write;

// Most tests use 16-byte blocks, so address 0x00 is block 0, 0x10 is block 1,
// 0x40 is block 4, and so on.
CacheConfig make_config(std::uint64_t size, std::uint64_t assoc, Policy policy = Policy::LRU) {
    CacheConfig config;
    config.size_bytes = size;
    config.block_bytes = 16;
    config.associativity = assoc;
    config.policy = policy;
    return config;
}

}  // namespace

TEST(config_derives_blocks_ways_and_sets) {
    CacheConfig config;  // defaults: 32 KiB, 64-byte blocks, 8-way
    CHECK_EQ(config.num_blocks(), 512u);
    CHECK_EQ(config.ways(), 8u);
    CHECK_EQ(config.num_sets(), 64u);

    config.associativity = 0;  // fully associative: one set holding everything
    CHECK_EQ(config.ways(), 512u);
    CHECK_EQ(config.num_sets(), 1u);
}

TEST(config_rejects_invalid_geometry) {
    CacheConfig config = make_config(64, 1);
    config.block_bytes = 48;  // not a power of two
    CHECK_THROWS(Cache{config});

    CHECK_THROWS(Cache{make_config(0, 1)});
    CHECK_THROWS(Cache{make_config(100, 1)});  // not a multiple of the block size
    CHECK_THROWS(Cache{make_config(64, 3)});   // 4 blocks cannot be split into 3-way sets
    CHECK_THROWS(Cache{make_config(64, 8)});   // more ways than blocks
    CHECK_THROWS(Cache{make_config(96, 2)});   // 3 sets: not a power of two
}

TEST(same_block_hits_after_first_miss) {
    Cache cache(make_config(64, 1));
    CHECK(!cache.access(0x00, R));  // miss: cache starts empty
    CHECK(cache.access(0x04, R));   // hit: same 16-byte block
    CHECK(cache.access(0x0F, R));   // hit: last byte of the block
    CHECK(!cache.access(0x10, R));  // miss: next block
    CHECK_EQ(cache.stats().accesses, 4u);
    CHECK_EQ(cache.stats().hits, 2u);
    CHECK_EQ(cache.stats().misses, 2u);
    CHECK_EQ(cache.stats().miss_rate(), 0.5);
}

TEST(direct_mapped_conflict_evicts) {
    // 4 sets. Blocks 0 and 4 both map to set 0 and keep evicting each other.
    Cache cache(make_config(64, 1));
    CHECK(!cache.access(0x00, R));  // miss, set 0 <- block 0
    CHECK(cache.access(0x04, R));   // hit
    CHECK(!cache.access(0x10, R));  // miss, set 1 <- block 1
    CHECK(!cache.access(0x40, R));  // miss, set 0: block 4 evicts block 0
    CHECK(!cache.access(0x00, R));  // miss, set 0: block 0 evicts block 4
    CHECK_EQ(cache.stats().hits, 1u);
    CHECK_EQ(cache.stats().misses, 4u);
    CHECK_EQ(cache.stats().evictions, 2u);
    CHECK_EQ(cache.stats().miss_rate(), 0.8);
}

TEST(two_way_set_removes_the_conflict) {
    // Same size, 2 ways: 2 sets. Blocks 0 and 4 now share set 0 peacefully.
    Cache cache(make_config(64, 2));
    CHECK(!cache.access(0x00, R));  // miss
    CHECK(!cache.access(0x40, R));  // miss, goes in the second way
    CHECK(cache.access(0x00, R));   // hit
    CHECK(cache.access(0x40, R));   // hit
    CHECK_EQ(cache.stats().evictions, 0u);
}

TEST(fully_associative_uses_every_frame) {
    // Blocks 0, 4, 8, 12 would all collide in a direct-mapped cache.
    Cache cache(make_config(64, 0));
    for (std::uint64_t address : {0x00, 0x40, 0x80, 0xC0}) CHECK(!cache.access(address, R));
    for (std::uint64_t address : {0x00, 0x40, 0x80, 0xC0}) CHECK(cache.access(address, R));
    CHECK_EQ(cache.stats().evictions, 0u);

    CHECK(!cache.access(0x100, R));  // cache full: evicts block 0, the least recently used
    CHECK(!cache.access(0x00, R));   // so block 0 misses (and evicts block 4)
    CHECK(cache.access(0x80, R));    // block 8 is still there
    CHECK_EQ(cache.stats().evictions, 2u);
}

TEST(lru_evicts_least_recently_used) {
    // One set, two ways. A=0x00, B=0x10, C=0x20.
    Cache cache(make_config(32, 0, Policy::LRU));
    CHECK(!cache.access(0x00, R));  // A miss
    CHECK(!cache.access(0x10, R));  // B miss
    CHECK(cache.access(0x00, R));   // A hit: A is now more recent than B
    CHECK(!cache.access(0x20, R));  // C miss: evicts B
    CHECK(cache.access(0x00, R));   // A hit: it survived
    CHECK(!cache.access(0x10, R));  // B miss: it was the victim
    CHECK_EQ(cache.stats().hits, 2u);
    CHECK_EQ(cache.stats().misses, 4u);
}

TEST(fifo_evicts_oldest_arrival_even_if_just_used) {
    // Identical accesses to the LRU test, different outcome.
    Cache cache(make_config(32, 0, Policy::FIFO));
    CHECK(!cache.access(0x00, R));  // A miss
    CHECK(!cache.access(0x10, R));  // B miss
    CHECK(cache.access(0x00, R));   // A hit: does not change A's place in the queue
    CHECK(!cache.access(0x20, R));  // C miss: evicts A, the first one in
    CHECK(!cache.access(0x00, R));  // A miss: evicts B
    CHECK(!cache.access(0x10, R));  // B miss: evicts C
    CHECK_EQ(cache.stats().hits, 1u);
    CHECK_EQ(cache.stats().misses, 5u);
    CHECK_EQ(cache.stats().evictions, 3u);
}

TEST(write_miss_allocates_the_block) {
    Cache cache(make_config(64, 1));
    CHECK(!cache.access(0x00, W));  // write miss: block is loaded (write-allocate)
    CHECK(cache.access(0x08, R));   // so a later read of the block hits
    CHECK_EQ(cache.stats().writes, 1u);
    CHECK_EQ(cache.stats().reads, 1u);
    CHECK_EQ(cache.stats().write_misses, 1u);
    CHECK_EQ(cache.stats().read_misses, 0u);
    CHECK_EQ(cache.dirty_blocks(), 1u);
    CHECK_EQ(cache.stats().write_backs, 0u);  // nothing written back until eviction
}

TEST(evicting_dirty_block_writes_back) {
    Cache cache(make_config(64, 1));
    cache.access(0x00, W);  // block 0 loaded and dirty
    cache.access(0x40, R);  // evicts dirty block 0  -> write-back #1
    cache.access(0x00, R);  // evicts clean block 4  -> no write-back
    cache.access(0x40, R);  // evicts block 0, reloaded clean -> no write-back
    CHECK_EQ(cache.stats().evictions, 3u);
    CHECK_EQ(cache.stats().write_backs, 1u);
    CHECK_EQ(cache.dirty_blocks(), 0u);
}

TEST(evicting_clean_blocks_never_writes_back) {
    Cache cache(make_config(64, 1));
    for (int round = 0; round < 3; ++round) {
        cache.access(0x00, R);
        cache.access(0x40, R);
    }
    CHECK_EQ(cache.stats().evictions, 5u);
    CHECK_EQ(cache.stats().write_backs, 0u);
}

TEST(write_hit_marks_block_dirty) {
    Cache cache(make_config(64, 1));
    cache.access(0x00, R);         // loaded clean
    CHECK(cache.access(0x08, W));  // write hit: now dirty
    CHECK(cache.access(0x0C, W));  // many writes to one block still mean one write-back
    cache.access(0x40, R);         // evicts it
    CHECK_EQ(cache.stats().write_backs, 1u);
}

TEST(large_associativity_matches_expected_lru_behaviour) {
    // 32 ways in one set: exercises the hash-indexed lookup path.
    Cache cache(make_config(32 * 16, 0, Policy::LRU));
    for (std::uint64_t block = 0; block < 32; ++block) CHECK(!cache.access(block * 16, R));
    for (std::uint64_t block = 0; block < 32; ++block) CHECK(cache.access(block * 16, R));
    CHECK(!cache.access(32 * 16, R));  // evicts block 0 (least recently used)
    CHECK(cache.access(2 * 16, R));    // block 2 untouched
    CHECK(!cache.access(0 * 16, R));   // block 0 gone; reloading it evicts block 1
    CHECK(!cache.access(1 * 16, R));   // block 1 gone; reloading it evicts block 3
    CHECK(!cache.access(3 * 16, R));
    CHECK_EQ(cache.stats().hits, 33u);
    CHECK_EQ(cache.stats().misses, 36u);
    CHECK_EQ(cache.stats().evictions, 4u);
}

TEST(counters_always_add_up) {
    Cache cache(make_config(64, 2, Policy::FIFO));
    std::uint64_t address = 12345;
    for (int i = 0; i < 10000; ++i) {
        address = address * 6364136223846793005ULL + 1442695040888963407ULL;
        cache.access((address >> 40) % 1024, (address >> 20) % 3 == 0 ? W : R);
    }
    const CacheStats& stats = cache.stats();
    CHECK_EQ(stats.accesses, 10000u);
    CHECK_EQ(stats.hits + stats.misses, stats.accesses);
    CHECK_EQ(stats.reads + stats.writes, stats.accesses);
    CHECK_EQ(stats.read_misses + stats.write_misses, stats.misses);
    CHECK_EQ(stats.misses - stats.evictions, 4u);  // every miss after the first 4 fills evicts
    CHECK(stats.write_backs <= stats.evictions);
}
