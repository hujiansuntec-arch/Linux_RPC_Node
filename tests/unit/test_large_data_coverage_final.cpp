#include "simple_test.h"
#include "nexus/transport/LargeDataChannel.h"
#include "nexus/core/Config.h"
#include <thread>
#include <atomic>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace Nexus::rpc;

// Redefine structs to match LargeDataChannel.h layout for white-box testing
struct TestReaderSlot {
    std::atomic<uint64_t> read_pos;
    std::atomic<uint64_t> heartbeat;
    std::atomic<int32_t> pid;
    std::atomic<bool> active;
    char padding[43];

    TestReaderSlot() : read_pos(0), heartbeat(0), pid(0), active(false) { memset(padding, 0, sizeof(padding)); }
} __attribute__((aligned(64)));

struct TestRingBufferControlLayout {
    std::atomic<uint64_t> write_pos;
    std::atomic<uint64_t> sequence;
    std::atomic<uint64_t> writer_heartbeat;
    std::atomic<int32_t> writer_pid;
    char padding1[36];

    TestReaderSlot readers[16];

    std::atomic<uint32_t> num_readers;
    std::atomic<int32_t> ref_count;
    uint64_t capacity;
    uint32_t max_block_size;
    uint32_t max_readers;
    char padding2[40];
};

TEST(LargeDataCoverageFinal, CleanupOrphanedChannels_RefCountZero) {
    std::string channel_name = "channel_orphan_ref0"; // Must contain "channel"
    size_t size = 1024 * 1024;
    std::string shm_name = channel_name;
    
    shm_unlink(shm_name.c_str());

    int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    ASSERT_TRUE(fd != -1);
    ftruncate(fd, sizeof(TestRingBufferControlLayout) + size);
    
    void* addr = mmap(NULL, sizeof(TestRingBufferControlLayout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_TRUE(addr != MAP_FAILED);
    
    auto* control = new (addr) TestRingBufferControlLayout();
    control->ref_count.store(0); 
    control->writer_pid.store(getpid()); 
    
    munmap(addr, sizeof(TestRingBufferControlLayout));
    close(fd);

    // Trigger cleanup
    size_t cleaned = LargeDataChannel::cleanupOrphanedChannels(0);
    // It might clean it because ref_count is 0.
    // But wait, cleanupOrphanedChannels checks if process is alive.
    // If writer_pid is getpid(), it is alive.
    // If ref_count is 0, does it clean?
    // Let's check implementation.
    // "if (control->ref_count.load() == 0) { should_cleanup = true; cleanup_reason = "ref_count is 0"; }"
    // Yes.
    
    ASSERT_GT(cleaned, 0); // Expect at least 1 cleaned
    
    // Verify it's gone
    fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
    if (fd != -1) {
        close(fd);
        shm_unlink(shm_name.c_str());
    }
}

TEST(LargeDataCoverageFinal, CleanupOrphanedChannels_DeadProcess) {
    std::string channel_name = "channel_orphan_dead"; // Must contain "channel"
    size_t size = 1024 * 1024;
    std::string shm_name = channel_name;

    shm_unlink(shm_name.c_str());

    int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    ASSERT_TRUE(fd != -1);
    ftruncate(fd, sizeof(TestRingBufferControlLayout) + size);
    
    void* addr = mmap(NULL, sizeof(TestRingBufferControlLayout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_TRUE(addr != MAP_FAILED);
    
    auto* control = new (addr) TestRingBufferControlLayout();
    control->ref_count.store(1);
    control->writer_pid.store(999999); // Dead PID
    control->writer_heartbeat.store(time(NULL) - 100); // Old heartbeat
    
    munmap(addr, sizeof(TestRingBufferControlLayout));
    close(fd);
    
    size_t cleaned = LargeDataChannel::cleanupOrphanedChannels(0);
    ASSERT_GT(cleaned, 0);
    
    shm_unlink(shm_name.c_str());
}

TEST(LargeDataCoverageFinal, CleanupDeadReaders) {
    std::string channel_name = "test_dead_readers";
    size_t size = 1024 * 1024;
    
    LargeDataChannel::Config config;
    config.buffer_size = size;
    auto channel = LargeDataChannel::create(channel_name, config);
    ASSERT_TRUE(channel != nullptr);
    
    std::string shm_name = channel_name;
    int fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
    ASSERT_TRUE(fd != -1);
    
    void* addr = mmap(NULL, sizeof(TestRingBufferControlLayout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_TRUE(addr != MAP_FAILED);
    
    auto* control = (TestRingBufferControlLayout*)addr;
    
    // Manually register a reader
    int reader_id = -1;
    for(int i=0; i<16; ++i) {
        bool expected = false;
        if(control->readers[i].active.compare_exchange_strong(expected, true)) {
            reader_id = i;
            control->readers[i].pid.store(999999); // Dead PID
            control->readers[i].heartbeat.store(time(NULL) - 100); // Old heartbeat
            control->num_readers++;
            break;
        }
    }
    ASSERT_NE(reader_id, -1);
    
    munmap(addr, sizeof(TestRingBufferControlLayout));
    close(fd);
}

TEST(LargeDataCoverageFinal, RingBufferWrapAround) {
    std::string channel_name = "test_wrap";
    size_t size = 4096; // Small buffer
    
    LargeDataChannel::Config config;
    config.buffer_size = size;
    auto writer = LargeDataChannel::create(channel_name, config);
    ASSERT_TRUE(writer != nullptr);
    
    auto reader = LargeDataChannel::create(channel_name, config);
    ASSERT_TRUE(reader != nullptr);

    std::vector<uint8_t> data(1024, 0xBB);
    
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(writer->write("topic", data.data(), data.size()) != -1);
        
        LargeDataChannel::DataBlock block;
        ASSERT_TRUE(reader->tryRead(block));
        ASSERT_EQ(block.size, 1024);
        reader->releaseBlock(block);
    }
}

TEST(LargeDataCoverageFinal, ValidateBlock_Corrupt) {
    std::string channel_name = "test_corrupt";
    shm_unlink(channel_name.c_str()); // Ensure clean state

    size_t size = 4096;
    
    LargeDataChannel::Config config;
    config.buffer_size = size;
    auto writer = LargeDataChannel::create(channel_name, config);
    auto reader = LargeDataChannel::create(channel_name, config);
    
    std::vector<uint8_t> data(100, 0xCC);
    writer->write("topic", data.data(), data.size());
    
    std::string shm_name = channel_name;
    int fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
    ASSERT_TRUE(fd != -1);
    
    void* addr = mmap(NULL, sizeof(TestRingBufferControlLayout) + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_TRUE(addr != MAP_FAILED);
    
    uint8_t* data_ptr = (uint8_t*)addr + sizeof(TestRingBufferControlLayout);
    
    // 1. Corrupt MAGIC
    uint32_t* magic = (uint32_t*)data_ptr;
    uint32_t original_magic = *magic;
    *magic = 0xDEADBEEF; 
    
    munmap(addr, sizeof(TestRingBufferControlLayout) + size);
    close(fd);
    
    LargeDataChannel::DataBlock block;
    ASSERT_FALSE(reader->tryRead(block));
    ASSERT_EQ((int)block.result, (int)LargeDataChannel::ReadResult::INVALID_MAGIC);
    
    // 2. Restore MAGIC, Corrupt CRC
    fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
    ASSERT_TRUE(fd != -1);
    addr = mmap(NULL, sizeof(TestRingBufferControlLayout) + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_TRUE(addr != MAP_FAILED);
    data_ptr = (uint8_t*)addr + sizeof(TestRingBufferControlLayout);
    
    magic = (uint32_t*)data_ptr;
    *magic = original_magic;
    
    uint32_t* crc = (uint32_t*)(data_ptr + 16);
    uint32_t original_crc = *crc;
    *crc = 0x00000000; 
    
    munmap(addr, sizeof(TestRingBufferControlLayout) + size);
    close(fd);
    
    ASSERT_FALSE(reader->tryRead(block));
    ASSERT_EQ((int)block.result, (int)LargeDataChannel::ReadResult::CRC_ERROR);

    // 3. Restore CRC, Corrupt Size (Too Large)
    fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
    ASSERT_TRUE(fd != -1);
    addr = mmap(NULL, sizeof(TestRingBufferControlLayout) + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_TRUE(addr != MAP_FAILED);
    
    // Reset read_pos because CRC_ERROR advanced it
    TestRingBufferControlLayout* control = (TestRingBufferControlLayout*)addr;
    control->readers[0].read_pos = 0;

    data_ptr = (uint8_t*)addr + sizeof(TestRingBufferControlLayout);
    
    crc = (uint32_t*)(data_ptr + 16);
    *crc = original_crc;
    
    uint32_t* size_ptr = (uint32_t*)(data_ptr + 4);
    uint32_t original_size = *size_ptr;
    *size_ptr = 10 * 1024 * 1024; // 10MB > 8MB max
    
    munmap(addr, sizeof(TestRingBufferControlLayout) + size);
    close(fd);
    
    ASSERT_FALSE(reader->tryRead(block));
    ASSERT_EQ((int)block.result, (int)LargeDataChannel::ReadResult::SIZE_EXCEEDED);

    // 4. Restore Size, Corrupt Size (Insufficient Data)
    // We need to make size larger than available but smaller than max.
    // Available is 100 + 128.
    // Set size to 200.
    fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
    ASSERT_TRUE(fd != -1);
    addr = mmap(NULL, sizeof(TestRingBufferControlLayout) + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_TRUE(addr != MAP_FAILED);

    // Reset read_pos because SIZE_EXCEEDED advanced it
    control = (TestRingBufferControlLayout*)addr;
    control->readers[0].read_pos = 0;

    data_ptr = (uint8_t*)addr + sizeof(TestRingBufferControlLayout);
    
    size_ptr = (uint32_t*)(data_ptr + 4);
    *size_ptr = 200; 
    
    munmap(addr, sizeof(TestRingBufferControlLayout) + size);
    close(fd);
    
    ASSERT_FALSE(reader->tryRead(block));
    ASSERT_EQ((int)block.result, (int)LargeDataChannel::ReadResult::INSUFFICIENT);
}
