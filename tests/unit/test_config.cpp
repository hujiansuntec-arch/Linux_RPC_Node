#include "simple_test.h"
#include "nexus/core/Config.h"
#include <cstdlib>

using namespace Nexus::rpc;

#include "simple_test.h"
#include "nexus/core/Config.h"
#include <cstdlib>

using namespace Nexus::rpc;

struct ConfigResetter {
    ConfigResetter() {
        reset();
    }
    ~ConfigResetter() {
        reset();
        // Also unset env vars
        unsetenv("NEXUS_MAX_INBOUND_QUEUES");
        unsetenv("NEXUS_QUEUE_CAPACITY");
        unsetenv("NEXUS_NUM_THREADS");
        unsetenv("NEXUS_MAX_QUEUE_SIZE");
        unsetenv("NEXUS_SHM_QUEUE_CAPACITY");
        unsetenv("NEXUS_HEARTBEAT_INTERVAL_MS");
        unsetenv("NEXUS_NODE_TIMEOUT_MS");
        unsetenv("NEXUS_BUFFER_SIZE");
        unsetenv("NEXUS_MAX_BLOCK_SIZE");
    }
    
    void reset() {
        Config& config = Config::instance();
        config.node = Config::NodeConfig();
        config.transport = Config::TransportConfig();
        config.shm = Config::SharedMemoryConfig();
        config.large_data = Config::LargeDataConfig();
    }
};

TEST(ConfigTest, DefaultValues) {
    ConfigResetter resetter;
    Config& config = Config::instance();
    
    ASSERT_EQ(config.node.max_inbound_queues, 32);
    ASSERT_EQ(config.node.queue_capacity, 1024);
    ASSERT_EQ(config.node.num_processing_threads, 4);
    ASSERT_EQ(config.node.max_queue_size, 25000);
    
    ASSERT_EQ(config.shm.queue_capacity, 1024);
    ASSERT_EQ(config.shm.max_inbound_queues, 64);
    ASSERT_EQ(config.shm.heartbeat_interval_ms, 1000);
    ASSERT_EQ(config.shm.node_timeout_ms, 5000);
    
    ASSERT_EQ(config.large_data.buffer_size, 64 * 1024 * 1024);
    ASSERT_EQ(config.large_data.max_block_size, 8 * 1024 * 1024);
}

TEST(ConfigTest, LoadFromEnvAllVars) {
    ConfigResetter resetter;
    
    setenv("NEXUS_MAX_INBOUND_QUEUES", "16", 1);
    setenv("NEXUS_QUEUE_CAPACITY", "512", 1);
    setenv("NEXUS_NUM_THREADS", "8", 1);
    setenv("NEXUS_MAX_QUEUE_SIZE", "10000", 1);
    setenv("NEXUS_SHM_QUEUE_CAPACITY", "512", 1);
    setenv("NEXUS_HEARTBEAT_INTERVAL_MS", "2000", 1);
    setenv("NEXUS_NODE_TIMEOUT_MS", "10000", 1);
    setenv("NEXUS_BUFFER_SIZE", "1048576", 1); // 1MB
    setenv("NEXUS_MAX_BLOCK_SIZE", "524288", 1); // 512KB
    
    Config& config = Config::instance();
    config.loadFromEnv();
    
    ASSERT_EQ(config.node.max_inbound_queues, 16);
    ASSERT_EQ(config.node.queue_capacity, 512);
    ASSERT_EQ(config.node.num_processing_threads, 8);
    ASSERT_EQ(config.node.max_queue_size, 10000);
    
    ASSERT_EQ(config.shm.queue_capacity, 512);
    ASSERT_EQ(config.shm.heartbeat_interval_ms, 2000);
    ASSERT_EQ(config.shm.node_timeout_ms, 10000);
    
    ASSERT_EQ(config.large_data.buffer_size, 1048576);
    ASSERT_EQ(config.large_data.max_block_size, 524288);
}

TEST(ConfigTest, LoadFromEnvClamping) {
    ConfigResetter resetter;
    Config& config = Config::instance();

    // Test min clamping
    setenv("NEXUS_MAX_INBOUND_QUEUES", "1", 1); // Min 8
    setenv("NEXUS_QUEUE_CAPACITY", "10", 1);    // Min 64
    setenv("NEXUS_NUM_THREADS", "0", 1);        // Min 1
    setenv("NEXUS_SHM_QUEUE_CAPACITY", "10", 1);// Min 64
    
    config.loadFromEnv();
    
    ASSERT_EQ(config.node.max_inbound_queues, 8);
    ASSERT_EQ(config.node.queue_capacity, 64);
    ASSERT_EQ(config.node.num_processing_threads, 1);
    ASSERT_EQ(config.shm.queue_capacity, 64);
    
    // Reset for max clamping
    resetter.reset();
    
    // Test max clamping
    setenv("NEXUS_MAX_INBOUND_QUEUES", "100", 1); // Max 64
    setenv("NEXUS_QUEUE_CAPACITY", "2000", 1);    // Max 1024
    setenv("NEXUS_NUM_THREADS", "32", 1);         // Max 16
    setenv("NEXUS_SHM_QUEUE_CAPACITY", "2000", 1);// Max 1024
    
    config.loadFromEnv();
    
    ASSERT_EQ(config.node.max_inbound_queues, 64);
    ASSERT_EQ(config.node.queue_capacity, 1024);
    ASSERT_EQ(config.node.num_processing_threads, 16);
    ASSERT_EQ(config.shm.queue_capacity, 1024);
}

TEST(ConfigTest, CalculateMemoryFootprint) {
    ConfigResetter resetter;
    Config& config = Config::instance();
    
    // Set known values
    config.node.max_inbound_queues = 10;
    config.shm.queue_capacity = 100;
    config.shm.message_size = 1000;
    
    config.large_data.buffer_size = 1000000;
    
    config.node.num_processing_threads = 2;
    config.node.max_queue_size = 1000;
    
    // Expected:
    // shm: 10 * 100 * 1000 = 1,000,000
    // large_data: 1,000,000
    // async: 2 * 1000 * 256 = 512,000
    // Total: 2,512,000
    
    size_t footprint = config.calculateMemoryFootprint();
    ASSERT_EQ(footprint, 2512000);
}
