#include "simple_test.h"
#include "nexus/transport/LargeDataChannel.h"
#include "nexus/transport/UdpTransport.h"
#include "nexus/utils/Logger.h"
#include <vector>
#include <string>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdexcept>

using namespace Nexus::rpc;

TEST(CoverageGapFinalTest, LargeData_Create_InvalidName) {
    // Suppress error logging for this test
    Logger::instance().setLevel(Logger::Level::NONE);
    
    LargeDataChannel::Config config;
    auto channel = LargeDataChannel::create("/invalid/name", config);
    ASSERT_TRUE(channel == nullptr);
    
    // Restore logging
    Logger::instance().setLevel(Logger::Level::INFO);
}

TEST(CoverageGapFinalTest, LargeData_Overflow_DropOldest) {
    std::string channel_name = "test_overflow_drop";
    shm_unlink(channel_name.c_str());
    
    LargeDataChannel::Config config;
    config.buffer_size = 1024; // Small buffer
    auto channel = LargeDataChannel::create(channel_name, config);
    ASSERT_NE(channel, nullptr);
    
    channel->setOverflowPolicy(LargeDataOverflowPolicy::DROP_OLDEST);
    
    // Fill buffer
    std::vector<uint8_t> data(400, 0xAA);
    ASSERT_TRUE(channel->write("topic", data.data(), data.size()) >= 0);
    ASSERT_TRUE(channel->write("topic", data.data(), data.size()) >= 0);
    
    // This should trigger drop oldest
    ASSERT_TRUE(channel->write("topic", data.data(), data.size()) >= 0);
}

TEST(CoverageGapFinalTest, LargeData_Overflow_CallbackException) {
    std::string channel_name = "test_overflow_cb_ex";
    shm_unlink(channel_name.c_str());
    
    LargeDataChannel::Config config;
    config.buffer_size = 1024;
    auto channel = LargeDataChannel::create(channel_name, config);
    ASSERT_NE(channel, nullptr);
    
    channel->setOverflowPolicy(LargeDataOverflowPolicy::DROP_NEWEST);
    channel->setOverflowCallback([](const std::string&, const std::string&, uint64_t, size_t) {
        throw std::runtime_error("Callback error");
    });
    
    std::vector<uint8_t> data(600, 0xAA);
    ASSERT_TRUE(channel->write("topic", data.data(), data.size()) >= 0);
    
    // This should trigger overflow and callback exception
    // The implementation likely catches the exception to prevent crash
    int64_t res = channel->write("topic", data.data(), data.size());
    (void)res; 
}

TEST(CoverageGapFinalTest, LargeData_TryRead_BehindMinPos) {
    std::string channel_name = "test_read_behind";
    shm_unlink(channel_name.c_str());
    
    LargeDataChannel::Config config;
    config.buffer_size = 1024;
    auto channel = LargeDataChannel::create(channel_name, config);
    
    std::vector<uint8_t> data(400, 0xAA);
    channel->write("topic", data.data(), data.size());
    channel->write("topic", data.data(), data.size());
    
    // Force overwrite
    channel->setOverflowPolicy(LargeDataOverflowPolicy::DROP_OLDEST);
    channel->write("topic", data.data(), data.size());
    
    LargeDataChannel::DataBlock block;
    channel->tryRead(block);
}

TEST(CoverageGapFinalTest, UdpTransport_Initialize_Twice) {
    UdpTransport transport;
    ASSERT_TRUE(transport.initialize(0));
    ASSERT_TRUE(transport.initialize(0)); // Should return true if already initialized
}

TEST(CoverageGapFinalTest, UdpTransport_Send_Uninitialized) {
    UdpTransport transport;
    std::vector<uint8_t> data(10, 0);
    ASSERT_FALSE(transport.send(data.data(), data.size(), "127.0.0.1", 12345));
}

TEST(CoverageGapFinalTest, UdpTransport_Send_InvalidAddress) {
    UdpTransport transport;
    ASSERT_TRUE(transport.initialize(0));
    
    std::vector<uint8_t> data(10, 0);
    ASSERT_FALSE(transport.send(data.data(), data.size(), "999.999.999.999", 12345));
}

TEST(CoverageGapFinalTest, UdpTransport_Send_EmptyData) {
    UdpTransport transport;
    ASSERT_TRUE(transport.initialize(0));
    
    ASSERT_FALSE(transport.send(nullptr, 10, "127.0.0.1", 12345));
    ASSERT_FALSE(transport.send((const uint8_t*)"test", 0, "127.0.0.1", 12345));
}
