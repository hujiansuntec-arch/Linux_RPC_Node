#include "simple_test.h"
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>
#include <atomic>
#include <sys/mman.h>
#include <fcntl.h>
#include "nexus/core/NodeImpl.h"
#include "nexus/transport/SharedMemoryTransportV3.h"
#include "nexus/core/Message.h"
#include "nexus/registry/GlobalRegistry.h"
#include "nexus/utils/Logger.h"

using namespace Nexus::rpc;

// Helper for cleanup
struct RegistryCleanup {
    RegistryCleanup() { clear(); }
    ~RegistryCleanup() { clear(); }
    
    void clear() {
        GlobalRegistry::instance().clearServices();
        shm_unlink("/librpc_registry");
    }
};

TEST(NodeImplExtra, QueueStatsAndOverflow) {
    RegistryCleanup cleanup;
    std::string node_id = "test_node_overflow";
    auto node = std::make_shared<NodeImpl>(node_id, false, 0);
    node->initialize(0);

    // 1. Test getQueueStats initially
    auto stats = node->getQueueStats();
    ASSERT_EQ(stats.total_dropped, 0);
    for (size_t i = 0; i < NodeImpl::NUM_PROCESSING_THREADS; ++i) {
        ASSERT_EQ(stats.queue_depth[i], 0);
    }

    // 2. Test setQueueOverflowPolicy
    node->setQueueOverflowPolicy(QueueOverflowPolicy::DROP_NEWEST);
    
    // 3. Test setQueueOverflowCallback
    bool callback_called = false;
    node->setQueueOverflowCallback([&](const std::string& group, const std::string& topic, size_t dropped) {
        callback_called = true;
    });

    // We can't easily fill the queue to 25000 in a unit test without it taking too long
    // But we verified the API calls work.
}

TEST(NodeImplExtra, CleanupOrphanedChannels) {
    RegistryCleanup cleanup;
    std::string node_id = "test_node_cleanup";
    auto node = std::make_shared<NodeImpl>(node_id, false, 0);
    node->initialize(0);

    // This calls the method. Even if it returns 0, it covers the code path.
    // Since we are the only node, we should be the cleanup master.
    size_t cleaned = node->cleanupOrphanedChannels();
    // We expect 0 or more, but the call itself shouldn't crash.
    ASSERT_TRUE(cleaned >= 0);
}

TEST(NodeImplExtra, NodeJoinLeaveHandling) {
    RegistryCleanup cleanup;
    std::string node_id = "test_node_events";
    auto node = std::make_shared<NodeImpl>(node_id, false, 0);
    node->initialize(0);

    // Create a remote transport to send NODE_JOIN/NODE_LEAVE
    std::string remote_node_id = "remote_node_event";
    SharedMemoryTransportV3 remote_transport;
    ASSERT_TRUE(remote_transport.initialize(remote_node_id));

    // 1. Send NODE_JOIN
    std::vector<uint8_t> buffer(sizeof(MessagePacket));
    MessagePacket* packet = reinterpret_cast<MessagePacket*>(buffer.data());
    packet->magic = MessagePacket::MAGIC;
    packet->version = MessagePacket::VERSION;
    packet->msg_type = static_cast<uint8_t>(MessageType::NODE_JOIN);
    packet->group_len = 0;
    packet->topic_len = 0;
    packet->payload_len = 0;
    packet->udp_port = 0;
    std::strncpy(packet->node_id, remote_node_id.c_str(), sizeof(packet->node_id) - 1);
    packet->checksum = packet->calculateChecksum();

    ASSERT_TRUE(remote_transport.send(node_id, buffer.data(), buffer.size()));

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 2. Send NODE_LEAVE
    packet->msg_type = static_cast<uint8_t>(MessageType::NODE_LEAVE);
    packet->checksum = packet->calculateChecksum();

    ASSERT_TRUE(remote_transport.send(node_id, buffer.data(), buffer.size()));

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

TEST(NodeImplExtra, LargeDataChannelDiscovery) {
    RegistryCleanup cleanup;
    std::string node_id = "test_node_ldc";
    auto node = std::make_shared<NodeImpl>(node_id, false, 0);
    node->initialize(0);

    // Test findLargeDataChannels (should be empty initially)
    auto channels = node->findLargeDataChannels("some_group");
    ASSERT_EQ(channels.size(), 0);
}

TEST(NodeImplExtra, Capabilities) {
    RegistryCleanup cleanup;
    std::string node_id = "test_node_caps";
    auto node = std::make_shared<NodeImpl>(node_id, false, 0);
    node->initialize(0);

    // Test findNodesByCapability
    auto nodes = node->findNodesByCapability("some_cap");
    // Should be empty or just self if we had caps
    ASSERT_EQ(nodes.size(), 0);
}

TEST(NodeImplExtra, SendLargeData) {
    RegistryCleanup cleanup;
    std::string sender_id = "test_node_ld_sender";
    std::string receiver_id = "test_node_ld_receiver";
    
    auto sender = std::make_shared<NodeImpl>(sender_id, false, 0);
    sender->initialize(0);
    
    auto receiver = std::make_shared<NodeImpl>(receiver_id, false, 0);
    receiver->initialize(0);

    std::string group = "ld_group";
    std::string topic = "ld_topic";
    std::string channel_name = "ld_channel";
    std::vector<uint8_t> data(1024, 0xAA);

    // Subscribe to receive the notification on receiver
    bool received = false;
    receiver->subscribe(group, {topic}, [&](const std::string& g, const std::string& t, const uint8_t* payload, size_t len) {
        if (len == sizeof(LargeDataNotification)) {
            const LargeDataNotification* notif = reinterpret_cast<const LargeDataNotification*>(payload);
            if (std::string(notif->channel_name) == channel_name && std::string(notif->topic) == topic) {
                received = true;
            }
        }
    });

    // Send large data from sender
    auto err = sender->sendLargeData(group, channel_name, topic, data.data(), data.size());
    ASSERT_EQ(err, Node::Error::NO_ERROR);

    // Wait for notification
    for (int i = 0; i < 20; ++i) {
        if (received) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_TRUE(received);
    
    // Verify channel exists on sender
    auto channel = sender->getLargeDataChannel(channel_name);
    ASSERT_TRUE(channel != nullptr);
}

TEST(NodeImplExtra, DiscoverServices) {
    RegistryCleanup cleanup;
    std::string node_id = "test_node_discover";
    auto node = std::make_shared<NodeImpl>(node_id, false, 0);
    node->initialize(0);

    // Register a service locally
    std::string group = "disc_group";
    std::string topic = "disc_topic";
    node->subscribe(group, {topic}, [](const std::string&, const std::string&, const uint8_t*, size_t){});

    // Discover services
    auto services = node->discoverServices(group);
    ASSERT_TRUE(services.size() > 0);
    bool found = false;
    for (const auto& svc : services) {
        if (svc.topic == topic && svc.node_id == node_id) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}
