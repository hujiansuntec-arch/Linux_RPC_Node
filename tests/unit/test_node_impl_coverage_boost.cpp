#include "simple_test.h"

// Pre-include standard headers to avoid affecting them with the macro
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>

// Pre-include dependencies of NodeImpl.h
#include "nexus/core/Message.h"
#include "nexus/core/Node.h"
#include "nexus/transport/LargeDataChannel.h"

// Define private/protected as public to access internals for testing
#define private public
#define protected public
#include "nexus/core/NodeImpl.h"
#undef private
#undef protected

#include "nexus/core/Config.h"
#include "nexus/registry/GlobalRegistry.h"
#include "nexus/transport/UdpTransport.h"

using namespace Nexus;
using namespace Nexus::rpc;

namespace Nexus {
namespace rpc {

// Helper class to access private members of NodeImpl
class NodeImplTester {
public:
    static void setRunning(std::shared_ptr<NodeImpl> node, bool running) {
        node->running_ = running;
    }

    static void enqueueMessage(std::shared_ptr<NodeImpl> node, const std::string& source_node_id, 
                               const std::string& group, const std::string& topic, 
                               const uint8_t* payload, size_t payload_len) {
        node->enqueueMessage(source_node_id, group, topic, payload, payload_len);
    }

    static void handleServiceMessage(std::shared_ptr<NodeImpl> node, const std::string& from_node, 
                                     const std::string& group, const std::string& topic, 
                                     const uint8_t* payload, size_t payload_len, bool is_register) {
        node->handleServiceMessage(from_node, group, topic, payload, payload_len, is_register);
    }

    static void handleUdpHeartbeat(std::shared_ptr<NodeImpl> node, const std::string& from_node, 
                                   const std::string& from_addr, uint16_t from_port) {
        node->handleUdpHeartbeat(from_node, from_addr, from_port);
    }

    static size_t getRemoteNodesCount(std::shared_ptr<NodeImpl> node) {
        std::lock_guard<std::mutex> lock(node->remote_nodes_mutex_);
        return node->remote_nodes_.size();
    }

    static void addRemoteNode(std::shared_ptr<NodeImpl> node, const std::string& node_id, 
                              const std::string& address, uint16_t port, 
                              std::chrono::steady_clock::time_point last_heartbeat) {
        std::lock_guard<std::mutex> lock(node->remote_nodes_mutex_);
        NodeImpl::RemoteNodeInfo info;
        info.node_id = node_id;
        info.address = address;
        info.port = port;
        info.last_heartbeat = last_heartbeat;
        node->remote_nodes_[node_id] = info;
    }

    static void checkUdpTimeouts(std::shared_ptr<NodeImpl> node) {
        node->checkUdpTimeouts();
    }

    static void broadcastServiceUpdate(std::shared_ptr<NodeImpl> node, const ServiceDescriptor& svc, bool is_add) {
        node->broadcastServiceUpdate(svc, is_add);
    }

    static void handleQuerySubscriptions(std::shared_ptr<NodeImpl> node, const std::string& remote_node_id, 
                                         uint16_t remote_port, const std::string& remote_addr) {
        node->handleQuerySubscriptions(remote_node_id, remote_port, remote_addr);
    }

    static void processPacket(std::shared_ptr<NodeImpl> node, const uint8_t* data, size_t size, const std::string& from) {
        node->processPacket(data, size, from);
    }

    enum class SystemMessageType { SERVICE_REGISTER, SERVICE_UNREGISTER, NODE_JOIN, NODE_LEAVE };

    static void enqueueSystemMessage(std::shared_ptr<NodeImpl> node, SystemMessageType type, 
                                     const std::string& source_node_id, const std::string& group, 
                                     const std::string& topic, const uint8_t* payload, size_t payload_len) {
        NodeImpl::SystemMessageType implType;
        switch(type) {
            case SystemMessageType::SERVICE_REGISTER: implType = NodeImpl::SystemMessageType::SERVICE_REGISTER; break;
            case SystemMessageType::SERVICE_UNREGISTER: implType = NodeImpl::SystemMessageType::SERVICE_UNREGISTER; break;
            case SystemMessageType::NODE_JOIN: implType = NodeImpl::SystemMessageType::NODE_JOIN; break;
            case SystemMessageType::NODE_LEAVE: implType = NodeImpl::SystemMessageType::NODE_LEAVE; break;
        }
        node->enqueueSystemMessage(implType, source_node_id, group, topic, payload, payload_len);
    }

    static void handleSubscribe(std::shared_ptr<NodeImpl> node, const std::string& remote_node_id, 
                                uint16_t remote_port, const std::string& remote_addr, 
                                const std::string& group, const std::string& topic) {
        node->handleSubscribe(remote_node_id, remote_port, remote_addr, group, topic);
    }

    static void handleUnsubscribe(std::shared_ptr<NodeImpl> node, const std::string& remote_node_id, 
                                  const std::string& group, const std::string& topic) {
        node->handleUnsubscribe(remote_node_id, group, topic);
    }

    static void queryExistingSubscriptions(std::shared_ptr<NodeImpl> node) {
        node->queryExistingSubscriptions();
    }

    static void registerService(std::shared_ptr<NodeImpl> node, const ServiceDescriptor& svc) {
        node->registerService(svc);
    }
};

} // namespace rpc
} // namespace Nexus

// Helper to reset state
void ResetState() {
    auto& config = Config::instance();
    config.node.max_queue_size = 100;
    GlobalRegistry::instance().clearServices();
}

using NodeImplTester = Nexus::rpc::NodeImplTester;

TEST(NodeImplCoverageBoostTest, PublishErrorHandling) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);

    // Test invalid args
    ASSERT_EQ((int)node->publish("", "topic", "payload"), (int)Node::Error::INVALID_ARG);
    ASSERT_EQ((int)node->publish("group", "", "payload"), (int)Node::Error::INVALID_ARG);

    // Test not initialized
    NodeImplTester::setRunning(node, false);
    ASSERT_EQ((int)node->publish("group", "topic", "payload"), (int)Node::Error::NOT_INITIALIZED);
}

TEST(NodeImplCoverageBoostTest, SubscribeErrorHandling) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);

    // Test invalid args
    ASSERT_EQ((int)node->subscribe("", {"topic"}, [](const std::string&, const std::string&, const uint8_t*, size_t){}), (int)Node::Error::INVALID_ARG);
    ASSERT_EQ((int)node->subscribe("group", {}, [](const std::string&, const std::string&, const uint8_t*, size_t){}), (int)Node::Error::INVALID_ARG);
    ASSERT_EQ((int)node->subscribe("group", {"topic"}, nullptr), (int)Node::Error::INVALID_ARG);

    // Test not initialized
    NodeImplTester::setRunning(node, false);
    ASSERT_EQ((int)node->subscribe("group", {"topic"}, [](const std::string&, const std::string&, const uint8_t*, size_t){}), (int)Node::Error::NOT_INITIALIZED);
}

TEST(NodeImplCoverageBoostTest, UnsubscribeErrorHandling) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);

    // Test invalid args
    ASSERT_EQ((int)node->unsubscribe("", {"topic"}), (int)Node::Error::INVALID_ARG);

    // Test not initialized
    NodeImplTester::setRunning(node, false);
    ASSERT_EQ((int)node->unsubscribe("group", {"topic"}), (int)Node::Error::NOT_INITIALIZED);
    
    NodeImplTester::setRunning(node, true);
    // Test not found
    ASSERT_EQ((int)node->unsubscribe("non_existent_group", {"topic"}), (int)Node::Error::NOT_FOUND);
}

TEST(NodeImplCoverageBoostTest, SendLargeDataErrorHandling) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);

    std::string group = "group";
    std::string channel = "channel";
    std::string topic = "topic";
    std::vector<uint8_t> data(10, 0);

    // Test invalid args
    ASSERT_EQ((int)node->sendLargeData("", channel, topic, data.data(), data.size()), (int)Node::Error::INVALID_ARG);
    ASSERT_EQ((int)node->sendLargeData(group, "", topic, data.data(), data.size()), (int)Node::Error::INVALID_ARG);
    ASSERT_EQ((int)node->sendLargeData(group, channel, "", data.data(), data.size()), (int)Node::Error::INVALID_ARG);
    ASSERT_EQ((int)node->sendLargeData(group, channel, topic, nullptr, data.size()), (int)Node::Error::INVALID_ARG);
    ASSERT_EQ((int)node->sendLargeData(group, channel, topic, data.data(), 0), (int)Node::Error::INVALID_ARG);

    // Test not initialized
    NodeImplTester::setRunning(node, false);
    ASSERT_EQ((int)node->sendLargeData(group, channel, topic, data.data(), data.size()), (int)Node::Error::NOT_INITIALIZED);
}

TEST(NodeImplCoverageBoostTest, QueueOverflowPolicies) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);

    // Set small queue size
    Config::instance().node.max_queue_size = 2;
    
    // Subscribe to ensure messages are enqueued
    node->subscribe("group", {"topic"}, [](const std::string&, const std::string&, const uint8_t*, size_t){});

    // 1. Test DROP_OLDEST (default)
    node->setQueueOverflowPolicy(QueueOverflowPolicy::DROP_OLDEST);
    
    // Fill queue
    std::vector<uint8_t> payload(10, 0);
    NodeImplTester::enqueueMessage(node, "src", "group", "topic", payload.data(), payload.size());
    NodeImplTester::enqueueMessage(node, "src", "group", "topic", payload.data(), payload.size());
    
    // Add one more, should drop oldest
    NodeImplTester::enqueueMessage(node, "src", "group", "topic", payload.data(), payload.size());
    
    // 2. Test DROP_NEWEST
    node->setQueueOverflowPolicy(QueueOverflowPolicy::DROP_NEWEST);
    
    for(int i=0; i<10; ++i) {
        NodeImplTester::enqueueMessage(node, "src", "group", "topic", payload.data(), payload.size());
    }
    
    // 3. Test BLOCK (fallback to DROP_OLDEST in implementation)
    node->setQueueOverflowPolicy(QueueOverflowPolicy::BLOCK);
    
    for(int i=0; i<10; ++i) {
        NodeImplTester::enqueueMessage(node, "src", "group", "topic", payload.data(), payload.size());
    }
}

TEST(NodeImplCoverageBoostTest, HandleServiceMessageEdgeCases) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);

    // Test self message (should be ignored)
    NodeImplTester::handleServiceMessage(node, node->getNodeId(), "group", "topic", nullptr, 0, true);
    
    // Test invalid payload length
    std::vector<uint8_t> short_payload(4, 0);
    NodeImplTester::handleServiceMessage(node, "remote", "group", "topic", short_payload.data(), short_payload.size(), true);
    
    // Test invalid payload content (lengths don't match)
    std::vector<uint8_t> invalid_payload = {
        0, // type
        0, // transport
        10, // channel_len (but no data)
        0, 0 // udp_addr_len
    };
    NodeImplTester::handleServiceMessage(node, "remote", "group", "topic", invalid_payload.data(), invalid_payload.size(), true);
}

TEST(NodeImplCoverageBoostTest, HandleUdpHeartbeatEdgeCases) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", true, 0); // Enable UDP
    node->initialize(0);

    // Test self heartbeat
    NodeImplTester::handleUdpHeartbeat(node, node->getNodeId(), "127.0.0.1", 12345);
    
    // Verify no remote node added
    ASSERT_EQ(NodeImplTester::getRemoteNodesCount(node), 0);

    // Test new node
    NodeImplTester::handleUdpHeartbeat(node, "remote", "127.0.0.1", 12345);
    ASSERT_EQ(NodeImplTester::getRemoteNodesCount(node), 1);
    
    // Test existing node update
    NodeImplTester::handleUdpHeartbeat(node, "remote", "127.0.0.1", 12345);
    ASSERT_EQ(NodeImplTester::getRemoteNodesCount(node), 1);
}

TEST(NodeImplCoverageBoostTest, CheckUdpTimeouts) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", true, 0);
    node->initialize(0);

    // Add a node manually
    NodeImplTester::addRemoteNode(node, "remote", "127.0.0.1", 12345, std::chrono::steady_clock::now() - std::chrono::seconds(10));

    // Register a service for this node to verify cleanup
    ServiceDescriptor svc;
    svc.node_id = "remote";
    svc.group = "group";
    svc.topic = "topic";
    svc.transport = TransportType::UDP;
    GlobalRegistry::instance().registerService("group", svc);

    // Run check
    NodeImplTester::checkUdpTimeouts(node);

    // Verify node removed
    ASSERT_EQ(NodeImplTester::getRemoteNodesCount(node), 0);
    
    // Verify service removed
    auto services = GlobalRegistry::instance().findServices("group");
    ASSERT_EQ(services.size(), 0);
}

TEST(NodeImplCoverageBoostTest, BroadcastServiceUpdateEdgeCases) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", true, 0);
    node->initialize(0);

    ServiceDescriptor svc;
    svc.node_id = "remote";
    svc.group = "group";
    svc.topic = "topic";
    svc.transport = TransportType::UDP;
    
    // 1. Invalid endpoint format
    svc.udp_address = "invalid_endpoint";
    GlobalRegistry::instance().registerService("group", svc);
    NodeImplTester::broadcastServiceUpdate(node, svc, true); // Should not crash

    // 2. Invalid port
    svc.udp_address = "127.0.0.1:invalid";
    GlobalRegistry::instance().registerService("group", svc);
    NodeImplTester::broadcastServiceUpdate(node, svc, true); // Should not crash
    
    // 3. Valid endpoint
    svc.udp_address = "127.0.0.1:12345";
    GlobalRegistry::instance().registerService("group", svc);
    NodeImplTester::broadcastServiceUpdate(node, svc, true);
}

TEST(NodeImplCoverageBoostTest, HandleQuerySubscriptionsEdgeCases) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", true, 0);
    node->initialize(0);

    // Test invalid port
    NodeImplTester::handleQuerySubscriptions(node, "remote", 0, "127.0.0.1");
    
    // Test valid query
    NodeImplTester::handleQuerySubscriptions(node, "remote", 12345, "127.0.0.1");
}

TEST(NodeImplCoverageBoostTest, CleanupOrphanedChannels) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);
    
    // Just call it, hard to verify side effects without mocking filesystem/shm
    node->cleanupOrphanedChannels();
}

TEST(NodeImplCoverageBoostTest, ProcessPacketEdgeCases) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);

    // 1. Test too small packet
    uint8_t small_data[4] = {0};
    NodeImplTester::processPacket(node, small_data, sizeof(small_data), "sender");

    // 2. Test invalid packet (bad magic)
    MessagePacket invalid_packet;
    invalid_packet.magic = 0xDEADBEEF; // Wrong magic
    NodeImplTester::processPacket(node, (uint8_t*)&invalid_packet, sizeof(invalid_packet), "sender");

    // 3. Test self-message
    MessagePacket self_packet;
    self_packet.magic = MessagePacket::MAGIC;
    strncpy(self_packet.node_id, node->getNodeId().c_str(), 31);
    NodeImplTester::processPacket(node, (uint8_t*)&self_packet, sizeof(self_packet), "sender");

    // 4. Test various message types
    struct TestCase {
        MessageType type;
        std::string desc;
    };
    
    std::vector<TestCase> cases = {
        {MessageType::SERVICE_REGISTER, "SERVICE_REGISTER"},
        {MessageType::SERVICE_UNREGISTER, "SERVICE_UNREGISTER"},
        {MessageType::NODE_JOIN, "NODE_JOIN"},
        {MessageType::NODE_LEAVE, "NODE_LEAVE"},
        {MessageType::SUBSCRIBE, "SUBSCRIBE"}, // Should be ignored
        {MessageType::HEARTBEAT, "HEARTBEAT"} // Should be ignored
    };

    for (const auto& tc : cases) {
        MessagePacket packet;
        packet.magic = MessagePacket::MAGIC;
        strncpy(packet.node_id, "remote_node", 31);
        packet.msg_type = (uint8_t)tc.type;
        packet.group_len = 0;
        packet.topic_len = 0;
        packet.payload_len = 0;
        
        NodeImplTester::processPacket(node, (uint8_t*)&packet, sizeof(packet), "sender");
    }
}

TEST(NodeImplCoverageBoostTest, CreateNodeFactory) {
    auto node = Nexus::rpc::createNode("factory_node", Nexus::rpc::TransportMode::UDP);
    ASSERT_NE(node, nullptr);
    // Cast to NodeImpl to check ID
    auto nodeImpl = std::dynamic_pointer_cast<Nexus::rpc::NodeImpl>(node);
    ASSERT_NE(nodeImpl, nullptr);
    ASSERT_FALSE(nodeImpl->getNodeId().empty());
}

TEST(NodeImplCoverageBoostTest, UdpCallbackCoverage) {
    // Create a node with UDP
    auto node = std::make_shared<Nexus::rpc::NodeImpl>("udp_cov_node", true, 0);
    node->initialize(0);
    uint16_t port = node->getUdpPort();
    ASSERT_GT(port, 0);

    // Create a sender transport
    Nexus::rpc::UdpTransport sender;
    sender.initialize(0);

    // Helper to send packet
    auto sendPacket = [&](Nexus::rpc::MessageType type) {
        auto packet = Nexus::rpc::MessageBuilder::build("sender_node", "group", "topic", nullptr, 0, 0, type);
        sender.send(packet.data(), packet.size(), "127.0.0.1", port);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Wait for processing
    };

    // Send various types to hit switch cases in UDP callback
    sendPacket(Nexus::rpc::MessageType::SERVICE_REGISTER);
    sendPacket(Nexus::rpc::MessageType::SERVICE_UNREGISTER);
    sendPacket(Nexus::rpc::MessageType::NODE_JOIN);
    sendPacket(Nexus::rpc::MessageType::NODE_LEAVE);
    sendPacket(Nexus::rpc::MessageType::SUBSCRIBE); // Should be ignored
    sendPacket(Nexus::rpc::MessageType::UNSUBSCRIBE); // Should be ignored
    
    // Send invalid packet (too small)
    uint8_t small_data[4] = {0};
    sender.send(small_data, 4, "127.0.0.1", port);
    
    // Send packet from self (should be ignored)
    auto self_packet = Nexus::rpc::MessageBuilder::build("udp_cov_node", "group", "topic", nullptr, 0, 0, Nexus::rpc::MessageType::DATA);
    sender.send(self_packet.data(), self_packet.size(), "127.0.0.1", port);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST(NodeImplCoverageBoostTest, InterProcessDeliveryCoverage) {
    auto node = std::make_shared<Nexus::rpc::NodeImpl>("ip_del_node", true, 0);
    node->initialize(0);

    // Use a unique node ID that definitely won't clash with local nodes
    std::string unique_remote_id = "remote_udp_node_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    // Register a fake UDP service in GlobalRegistry to trigger UDP delivery path
    Nexus::rpc::ServiceDescriptor svc;
    svc.node_id = unique_remote_id;
    svc.group = "udp_group";
    svc.topic = "udp_topic";
    svc.type = Nexus::rpc::ServiceType::NORMAL_MESSAGE;
    svc.transport = Nexus::rpc::TransportType::UDP;
    svc.udp_address = "127.0.0.1:54321"; // Fake address

    Nexus::rpc::GlobalRegistry::instance().registerService("udp_group", svc);

    // Publish message - should try to deliver to udp_group/udp_topic via UDP
    node->publish("udp_group", "udp_topic", "payload");

    // Register a fake SHM service
    Nexus::rpc::ServiceDescriptor svc_shm;
    svc_shm.node_id = "remote_shm_node";
    svc_shm.group = "shm_group";
    svc_shm.topic = "shm_topic";
    svc_shm.type = Nexus::rpc::ServiceType::NORMAL_MESSAGE;
    svc_shm.transport = Nexus::rpc::TransportType::SHARED_MEMORY;
    
    Nexus::rpc::GlobalRegistry::instance().registerService("shm_group", svc_shm);

    // Publish message - should try to deliver to shm_group/shm_topic via SHM
    // Note: This might fail silently if SHM node is not actually connected, but it covers the code path
    node->publish("shm_group", "shm_topic", "payload");

    // Clean up
    Nexus::rpc::GlobalRegistry::instance().unregisterService("udp_group", svc);
    Nexus::rpc::GlobalRegistry::instance().unregisterService("shm_group", svc_shm);
}

TEST(NodeImplCoverageBoostTest, ProcessPacketCoverage) {
    auto node = std::make_shared<Nexus::rpc::NodeImpl>("pp_cov_node", false, 0);
    node->initialize(0);

    // Helper to process packet
    auto process = [&](Nexus::rpc::MessageType type, const std::string& src_node) {
        auto packet = Nexus::rpc::MessageBuilder::build(src_node, "group", "topic", nullptr, 0, 0, type);
        NodeImplTester::processPacket(node, packet.data(), packet.size(), "shm");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    };

    // 1. Valid packets
    process(Nexus::rpc::MessageType::SERVICE_REGISTER, "remote_node");
    process(Nexus::rpc::MessageType::SERVICE_UNREGISTER, "remote_node");
    process(Nexus::rpc::MessageType::NODE_JOIN, "remote_node");
    process(Nexus::rpc::MessageType::NODE_LEAVE, "remote_node");
    
    // 2. Ignored types
    process(Nexus::rpc::MessageType::SUBSCRIBE, "remote_node");
    process(Nexus::rpc::MessageType::HEARTBEAT, "remote_node");

    // 3. Self message (should be ignored)
    process(Nexus::rpc::MessageType::DATA, "pp_cov_node");
}

TEST(NodeImplCoverageBoostTest, SystemMessageCoverage) {
    auto node = std::make_shared<NodeImpl>("sys_msg_node", false, 0);
    node->initialize(0);

    // Enqueue system messages directly
    NodeImplTester::enqueueSystemMessage(node, NodeImplTester::SystemMessageType::NODE_JOIN, "remote_node", "", "", nullptr, 0);
    NodeImplTester::enqueueSystemMessage(node, NodeImplTester::SystemMessageType::NODE_LEAVE, "remote_node", "", "", nullptr, 0);
    
    // Service messages
    Nexus::rpc::ServiceDescriptor svc;
    svc.node_id = "remote_node";
    svc.group = "group";
    svc.topic = "topic";
    std::vector<uint8_t> payload(sizeof(svc)); // Mock payload
    
    NodeImplTester::enqueueSystemMessage(node, NodeImplTester::SystemMessageType::SERVICE_REGISTER, "remote_node", "group", "topic", payload.data(), payload.size());
    NodeImplTester::enqueueSystemMessage(node, NodeImplTester::SystemMessageType::SERVICE_UNREGISTER, "remote_node", "group", "topic", payload.data(), payload.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST(NodeImplCoverageBoostTest, UnsubscribeWithUdp) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("udp_unsub_node", true, 0);
    node->initialize(0);

    node->subscribe("group", {"topic"}, [](const std::string&, const std::string&, const uint8_t*, size_t){});
    
    // This should trigger the UDP unregister path
    node->unsubscribe("group", {"topic"});
}

TEST(NodeImplCoverageBoostTest, LargeDataChannelErrors) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("ld_err_node", false, 0);
    node->initialize(0);

    // Test empty channel name
    ASSERT_EQ(node->getLargeDataChannel(""), std::shared_ptr<LargeDataChannel>(nullptr));
}

TEST(NodeImplCoverageBoostTest, UnusedHandlers) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("unused_node", false, 0);
    node->initialize(0);

    NodeImplTester::handleSubscribe(node, "remote", 12345, "127.0.0.1", "group", "topic");
    NodeImplTester::handleUnsubscribe(node, "remote", "group", "topic");
}

TEST(NodeImplCoverageBoostTest, PublishFiltering) {
    ResetState();
    auto node = std::make_shared<NodeImpl>("pub_filter_node", false, 0);
    node->initialize(0);

    // 1. Register service with different topic
    Nexus::rpc::ServiceDescriptor svc1;
    svc1.node_id = "remote1";
    svc1.group = "group";
    svc1.topic = "other_topic";
    svc1.type = Nexus::rpc::ServiceType::NORMAL_MESSAGE;
    svc1.transport = Nexus::rpc::TransportType::SHARED_MEMORY;
    Nexus::rpc::GlobalRegistry::instance().registerService("group", svc1);

    // 2. Register service with different type (LARGE_DATA)
    Nexus::rpc::ServiceDescriptor svc2;
    svc2.node_id = "remote2";
    svc2.group = "group";
    svc2.topic = "topic";
    svc2.type = Nexus::rpc::ServiceType::LARGE_DATA;
    svc2.transport = Nexus::rpc::TransportType::SHARED_MEMORY;
    Nexus::rpc::GlobalRegistry::instance().registerService("group", svc2);

    // 3. Register duplicate service (same node, different transport - though registry usually overwrites)
    // To simulate duplicate delivery check, we use two UDP services for the same node.
    Nexus::rpc::ServiceDescriptor svc3a;
    svc3a.node_id = "remote3";
    svc3a.group = "group";
    svc3a.topic = "topic";
    svc3a.type = Nexus::rpc::ServiceType::NORMAL_MESSAGE;
    svc3a.transport = Nexus::rpc::TransportType::UDP;
    svc3a.udp_address = "127.0.0.1:10001";
    Nexus::rpc::GlobalRegistry::instance().registerService("group", svc3a);

    Nexus::rpc::ServiceDescriptor svc3b;
    svc3b.node_id = "remote3"; // Same ID
    svc3b.group = "group";
    svc3b.topic = "topic";
    svc3b.type = Nexus::rpc::ServiceType::NORMAL_MESSAGE;
    svc3b.transport = Nexus::rpc::TransportType::UDP;
    svc3b.udp_address = "127.0.0.1:10002";
    Nexus::rpc::GlobalRegistry::instance().registerService("group", svc3b);

    // Publish
    node->publish("group", "topic", "payload");
}

TEST(NodeImplCoverageBoostTest, ShmDelivery) {
    ResetState();
    // Create two nodes. They should discover each other via SHM.
    auto node1 = std::make_shared<NodeImpl>("shm_node_1", false, 0);
    node1->initialize(0);
    
    auto node2 = std::make_shared<NodeImpl>("shm_node_2", false, 0);
    node2->initialize(0);

    // Wait for discovery
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Register service for node2
    Nexus::rpc::ServiceDescriptor svc;
    svc.node_id = "shm_node_2";
    svc.group = "group";
    svc.topic = "topic";
    svc.type = Nexus::rpc::ServiceType::NORMAL_MESSAGE;
    svc.transport = Nexus::rpc::TransportType::SHARED_MEMORY;
    Nexus::rpc::GlobalRegistry::instance().registerService("group", svc);

    // Publish from node1. Should deliver to node2 via SHM.
    node1->publish("group", "topic", "payload");
}

TEST(NodeImplCoverageBoostTest, QuerySubscriptionsEarlyReturn) {
    ResetState();
    // Create node with UDP disabled
    auto node = std::make_shared<NodeImpl>("no_udp_node", false, 0);
    node->initialize(0);

    // Call queryExistingSubscriptions. Should return early because UDP is disabled.
    NodeImplTester::queryExistingSubscriptions(node);
}

TEST(NodeImplCoverageBoostTest, SystemQueueOverflow) {
    ResetState();
    // Create a node
    auto node = createNode("sys_overflow_node");
    auto node_impl = std::dynamic_pointer_cast<NodeImpl>(node);
    ASSERT_NE(node_impl, nullptr);

    // Flood the system queue
    // MAX_SYSTEM_QUEUE_SIZE is typically 1024. We send 2000 messages.
    const int MSG_COUNT = 2000; 
    for (int i = 0; i < MSG_COUNT; ++i) {
        // Use friend access to enqueue directly
        NodeImplTester::enqueueSystemMessage(node_impl, NodeImplTester::SystemMessageType::NODE_JOIN, "remote_node", "", "", nullptr, 0);
    }
}

TEST(NodeImplCoverageBoostTest, DiscoverServicesFiltering) {
    ResetState();
    auto node = createNode("disc_filter_node");
    auto node_impl = std::dynamic_pointer_cast<NodeImpl>(node);
    ASSERT_NE(node_impl, nullptr);
    
    // Register mixed services
    ServiceDescriptor svc1;
    svc1.group = "group1";
    svc1.topic = "topic1";
    svc1.type = ServiceType::NORMAL_MESSAGE;
    NodeImplTester::registerService(node_impl, svc1);
    
    ServiceDescriptor svc2;
    svc2.group = "group1";
    svc2.topic = "topic2";
    svc2.type = ServiceType::LARGE_DATA;
    NodeImplTester::registerService(node_impl, svc2);
    
    // Discover only NORMAL_MESSAGE
    auto results = node->discoverServices("group1", ServiceType::NORMAL_MESSAGE);
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0].topic, "topic1");
    
    // Discover only LARGE_DATA
    results = node->discoverServices("group1", ServiceType::LARGE_DATA);
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0].topic, "topic2");
    
    // Discover ALL
    results = node->discoverServices("group1", ServiceType::ALL);
    ASSERT_EQ(results.size(), 2);
}

TEST(NodeImplCoverageBoostTest, HandleServiceMessageComplexPayload) {
    ResetState();
    auto node = createNode("complex_payload_node");
    auto node_impl = std::dynamic_pointer_cast<NodeImpl>(node);
    
    // Construct a payload with channel name and UDP address
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>(ServiceType::LARGE_DATA));
    payload.push_back(static_cast<uint8_t>(TransportType::SHARED_MEMORY));
    
    std::string channel = "test_channel";
    std::string udp = "127.0.0.1:5000";
    
    payload.push_back(static_cast<uint8_t>(channel.size()));
    uint16_t udp_len = static_cast<uint16_t>(udp.size());
    payload.push_back(static_cast<uint8_t>(udp_len & 0xFF));
    payload.push_back(static_cast<uint8_t>((udp_len >> 8) & 0xFF));
    
    // Add strings
    payload.insert(payload.end(), channel.begin(), channel.end());
    payload.insert(payload.end(), udp.begin(), udp.end());
    
    // Enqueue as SERVICE_REGISTER
    NodeImplTester::enqueueSystemMessage(node_impl, NodeImplTester::SystemMessageType::SERVICE_REGISTER, "remote_node", "group", "topic", payload.data(), payload.size());
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify service was registered
    auto services = node->discoverServices("group");
    bool found = false;
    for (const auto& s : services) {
        if (s.channel_name == channel && s.udp_address == udp) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

TEST(NodeImplCoverageBoostTest, ShmDeliveryRetry) {
    ResetState();
    // Create two nodes
    auto node1 = createNode("shm_retry_1");
    auto node2 = createNode("shm_retry_2");
    
    // Register SHM service on node 2
    ServiceDescriptor svc;
    svc.node_id = "shm_retry_2"; // Must match node2 ID
    svc.group = "shm_group";
    svc.topic = "shm_topic";
    svc.type = ServiceType::NORMAL_MESSAGE;
    svc.transport = TransportType::SHARED_MEMORY;
    
    // Manually register in global registry to simulate discovery
    Nexus::rpc::GlobalRegistry::instance().registerService("shm_group", svc);
    
    // Wait for registry propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Send message from node1
    std::string payload = "test_data";
    node1->publish("shm_group", "shm_topic", payload);
}

TEST(NodeImplCoverageBoostTest, UdpHeartbeatAndTimeout) {
    ResetState();
    auto node = createNode("udp_hb_node");
    auto node_impl = std::dynamic_pointer_cast<NodeImpl>(node);
    ASSERT_NE(node_impl, nullptr);

    // 1. Handle heartbeat from new node
    NodeImplTester::handleUdpHeartbeat(node_impl, "remote1", "127.0.0.1", 5000);
    ASSERT_EQ(NodeImplTester::getRemoteNodesCount(node_impl), 1);

    // 2. Handle heartbeat from self (should be ignored)
    NodeImplTester::handleUdpHeartbeat(node_impl, node_impl->getNodeId(), "127.0.0.1", 5000);
    ASSERT_EQ(NodeImplTester::getRemoteNodesCount(node_impl), 1);

    // 3. Add a timed-out node manually
    auto now = std::chrono::steady_clock::now();
    auto old_time = now - std::chrono::seconds(10); // 10s ago (timeout is 5s)
    NodeImplTester::addRemoteNode(node_impl, "remote2", "127.0.0.1", 5001, old_time);
    ASSERT_EQ(NodeImplTester::getRemoteNodesCount(node_impl), 2);

    // 4. Check timeouts
    NodeImplTester::checkUdpTimeouts(node_impl);
    
    // remote2 should be removed, remote1 should stay
    ASSERT_EQ(NodeImplTester::getRemoteNodesCount(node_impl), 1);
}

