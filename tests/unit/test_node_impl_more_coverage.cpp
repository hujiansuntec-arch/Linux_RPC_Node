#include "simple_test.h"
#define private public
#include "nexus/core/NodeImpl.h"
#include "nexus/core/Message.h"
#include "nexus/registry/GlobalRegistry.h"
#include "nexus/transport/SharedMemoryTransportV3.h"
#undef private
#include "nexus/utils/Logger.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace Nexus::rpc;

TEST(NodeImplMoreCoverage, UdpInitialization) {
    // Create node with UDP enabled
    auto node = std::make_shared<NodeImpl>("test_udp_node_cov", true, 0, TransportMode::LOCK_FREE_SHM);
    node->initialize(0);
    
    // Verify UDP is working (by checking port > 0)
    ASSERT_GT(node->getUdpPort(), 0);
    
    // Create another node with UDP enabled (should pick different port)
    auto node2 = std::make_shared<NodeImpl>("test_udp_node2_cov", true, 0, TransportMode::LOCK_FREE_SHM);
    node2->initialize(0);
    
    ASSERT_GT(node2->getUdpPort(), 0);
    ASSERT_NE(node->getUdpPort(), node2->getUdpPort());
}

TEST(NodeImplMoreCoverage, SystemMessages) {
    auto node = std::make_shared<NodeImpl>("test_sys_msg_cov", false, 0, TransportMode::LOCK_FREE_SHM);
    node->initialize(0);
    
    // Inject a SERVICE_REGISTER system message
    std::string source = "remote_node";
    std::string group = "test_group";
    std::string topic = "test_topic";
    
    // Construct a valid payload for SERVICE_REGISTER
    // Format: type(1) + transport(1) + channel_len(1) + udp_len(2) + [channel] + [udp]
    std::vector<uint8_t> payload = {
        1, // ServiceType::NORMAL_MESSAGE
        1, // TransportType::SHARED_MEMORY
        0, // Channel Name Length
        0, // UDP Address Length LSB
        0  // UDP Address Length MSB
    };
    
    node->enqueueSystemMessage(NodeImpl::SystemMessageType::SERVICE_REGISTER, source, group, topic, payload.data(), payload.size());
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify service is registered (indirectly, by checking if we can find it)
    auto services = Nexus::rpc::GlobalRegistry::instance().findServices(group);
    bool found = false;
    for (const auto& svc : services) {
        if (svc.node_id == source && svc.topic == topic) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
    
    // Inject SERVICE_UNREGISTER
    node->enqueueSystemMessage(NodeImpl::SystemMessageType::SERVICE_UNREGISTER, source, group, topic, payload.data(), payload.size());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    services = Nexus::rpc::GlobalRegistry::instance().findServices(group);
    found = false;
    for (const auto& svc : services) {
        if (svc.node_id == source && svc.topic == topic) {
            found = true;
            break;
        }
    }
    ASSERT_FALSE(found);
}

TEST(NodeImplMoreCoverage, IgnoredMessages) {
    auto node = std::make_shared<NodeImpl>("test_ignored_msg", false, 0, TransportMode::LOCK_FREE_SHM);
    node->initialize(0);
    
    auto sender = std::make_shared<NodeImpl>("sender_node", false, 0, TransportMode::LOCK_FREE_SHM);
    sender->initialize(0);
    
    if (sender->shm_transport_v3_) {
        // Construct packet
        std::vector<uint8_t> buffer(sizeof(MessagePacket) + 10);
        MessagePacket* packet = reinterpret_cast<MessagePacket*>(buffer.data());
        packet->magic = MessagePacket::MAGIC;
        packet->version = 1;
        packet->msg_type = (uint8_t)MessageType::SUBSCRIBE; // Ignored type
        strncpy(packet->node_id, "sender_node", 63);
        packet->group_len = 0;
        packet->topic_len = 0;
        packet->payload_len = 0;
        packet->checksum = 0;
        
        packet->checksum = packet->calculateChecksum();
        
        sender->shm_transport_v3_->send("test_ignored_msg", buffer.data(), buffer.size());
        
        // Also send UNSUBSCRIBE
        packet->msg_type = (uint8_t)MessageType::UNSUBSCRIBE;
        packet->checksum = 0;
        packet->checksum = packet->calculateChecksum();
        sender->shm_transport_v3_->send("test_ignored_msg", buffer.data(), buffer.size());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

TEST(NodeImplMoreCoverage, CleanupThread) {
    auto node = std::make_shared<NodeImpl>("test_cleanup_thread", false, 0, TransportMode::LOCK_FREE_SHM);
    node->initialize(0);
    
    ASSERT_TRUE(node->cleanup_running_);
    
    // We can manually call cleanupOrphanedChannels
    node->cleanupOrphanedChannels();
}
