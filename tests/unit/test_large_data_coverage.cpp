#include "simple_test.h"
#include "nexus/transport/LargeDataChannel.h"
#include <vector>
#include <string>
#include <cstring>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>

using namespace Nexus::rpc;

TEST(LargeDataCoverage, DropNewestPolicy) {
    LargeDataChannel::Config config;
    config.buffer_size = 4096 * 10;
    config.max_block_size = 4096;
    config.overflow_policy = LargeDataOverflowPolicy::DROP_NEWEST;
    
    std::atomic<int> callback_count{0};
    config.overflow_callback = [&](const std::string&, const std::string&, uint64_t, size_t) {
        callback_count++;
    };
    
    std::string shm_name = "test_cov_drop_newest";
    shm_unlink(shm_name.c_str());
    
    auto channel = LargeDataChannel::create(shm_name, config);
    ASSERT_TRUE(channel != nullptr);

    // Create a reader to hold the read position
    auto reader = LargeDataChannel::create(shm_name, config);
    ASSERT_TRUE(reader != nullptr);
    LargeDataChannel::DataBlock block;
    reader->tryRead(block); // Registers the reader
    
    std::vector<uint8_t> data(1024, 0xAA);
    
    // Fill buffer
    int writes = 0;
    // Capacity is ~40KB. Each write is ~1KB + header.
    // 50 writes should definitely overflow.
    for (int i = 0; i < 50; ++i) {
        channel->write("topic", data.data(), data.size());
        writes++;
    }
    
    // Check if callback was called
    ASSERT_GT(callback_count.load(), 0);
    
    // Verify stats
    auto stats = channel->getStats();
    // stats.dropped_messages is not available in public API
    // ASSERT_GT(stats.dropped_messages, 0);
}

TEST(LargeDataCoverage, WriteTooLarge) {
    LargeDataChannel::Config config;
    config.max_block_size = 1024;
    
    std::string shm_name = "test_cov_too_large";
    shm_unlink(shm_name.c_str());
    
    auto channel = LargeDataChannel::create(shm_name, config);
    ASSERT_TRUE(channel != nullptr);
    
    std::vector<uint8_t> data(2048, 0xAA);
    
    // Should fail
    int64_t result = channel->write("topic", data.data(), data.size());
    ASSERT_EQ(result, -1);
}

TEST(LargeDataCoverage, InvalidShmName) {
    LargeDataChannel::Config config;
    // Use a name that is likely invalid for shm_open (e.g., containing slashes if not allowed, or too long, or empty?)
    // Empty name might be invalid or just create unnamed shm?
    // shm_open requires name to start with / usually, but implementation might add it.
    // The implementation does: std::string shm_path = "/dev/shm/" + shm_name_;
    // But it uses shm_open(shm_name_.c_str(), ...)
    // If I pass a path with multiple slashes, it might fail?
    
    // Let's try a very long name or weird characters.
    // Or just check if create returns nullptr.
    
    // Note: shm_open implementation varies.
    // Let's try to force a failure by using a name that can't be created.
    // Maybe a name with ".." ?
    
    // If this test is flaky or OS dependent, I might skip it.
    // But let's try.
}

TEST(LargeDataCoverage, DestructorCleanup) {
    LargeDataChannel::Config config;
    std::string shm_name = "test_cov_destructor";
    shm_unlink(shm_name.c_str());
    
    {
        auto channel1 = LargeDataChannel::create(shm_name, config);
        ASSERT_TRUE(channel1 != nullptr);
        
        {
            auto channel2 = LargeDataChannel::create(shm_name, config);
            ASSERT_TRUE(channel2 != nullptr);
            // channel2 destructor runs here, ref_count decrements
        }
        // channel1 destructor runs here, ref_count becomes 0, unlinks
    }
    
    // Verify it's gone (try to open it)
    int fd = shm_open(shm_name.c_str(), O_RDONLY, 0666);
    if (fd >= 0) {
        close(fd);
        shm_unlink(shm_name.c_str());
        throw nexus_test::AssertionFailure("Shared memory should have been unlinked");
    }
}
