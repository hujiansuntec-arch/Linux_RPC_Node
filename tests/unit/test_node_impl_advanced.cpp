#include "simple_test.h"
#define private public
#include "nexus/core/NodeImpl.h"
#undef private
#include "nexus/core/Config.h"
#include <thread>
#include <chrono>
#include <vector>

using namespace Nexus::rpc;

// Helper class to access protected/private members
class NodeImplTestHelper : public NodeImpl {
public:
    using NodeImpl::NodeImpl;

    void test_handleServiceMessage(const std::string& from, const std::string& group, 
                                  const std::string& topic, const uint8_t* payload, 
                                  size_t len, bool is_reg) {
        handleServiceMessage(from, group, topic, payload, len, is_reg);
    }

    void test_handleNodeEvent(const std::string& from, bool joined) {
        handleNodeEvent(from, joined);
    }

    void test_checkUdpTimeouts() {
        checkUdpTimeouts();
    }
    
    void addRemoteNode(const std::string& id, const std::string& addr, uint16_t port, int age_ms) {
        std::lock_guard<std::mutex> lock(remote_nodes_mutex_);
        RemoteNodeInfo info;
        info.node_id = id;
        info.address = addr;
        info.port = port;
        info.last_heartbeat = std::chrono::steady_clock::now() - std::chrono::milliseconds(age_ms);
        remote_nodes_[id] = info;
    }
    
    bool hasRemoteNode(const std::string& id) {
        std::lock_guard<std::mutex> lock(remote_nodes_mutex_);
        return remote_nodes_.find(id) != remote_nodes_.end();
    }
};

TEST(NodeImplAdvancedTest, HandleServiceMessage_Invalid) {
    auto node = std::make_shared<NodeImplTestHelper>("test_node", false, 0);
    node->initialize(0);

    // Self message
    node->test_handleServiceMessage("test_node", "g", "t", nullptr, 0, true);

    // Invalid payload length
    uint8_t payload[4] = {0};
    node->test_handleServiceMessage("other", "g", "t", payload, 4, true); // Too short

    // Invalid dynamic length
    uint8_t payload2[10] = {0};
    payload2[2] = 100; // channel_len = 100, but total only 10
    node->test_handleServiceMessage("other", "g", "t", payload2, 10, true);
}

TEST(NodeImplAdvancedTest, HandleNodeEvent_Self) {
    auto node = std::make_shared<NodeImplTestHelper>("test_node", false, 0);
    node->initialize(0);

    // Should do nothing
    node->test_handleNodeEvent("test_node", true);
    node->test_handleNodeEvent("test_node", false);
}

TEST(NodeImplAdvancedTest, UdpTimeouts) {
    auto node = std::make_shared<NodeImplTestHelper>("test_node", true, 0);
    node->initialize(0);

    // Add a fresh node
    node->addRemoteNode("fresh_node", "127.0.0.1", 12345, 100);
    // Add a stale node (timeout is 3000ms usually, let's say 5000ms)
    node->addRemoteNode("stale_node", "127.0.0.1", 12346, 10000);

    node->test_checkUdpTimeouts();

    ASSERT_TRUE(node->hasRemoteNode("fresh_node"));
    ASSERT_FALSE(node->hasRemoteNode("stale_node"));
}

TEST(NodeImplAdvancedTest, SubscribeUnsubscribeEdgeCases) {
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);

    // Invalid args
    ASSERT_EQ(node->subscribe("", {}, nullptr), Node::Error::INVALID_ARG);
    ASSERT_EQ(node->unsubscribe("", {}), Node::Error::INVALID_ARG);

    // Unsubscribe non-existent
    ASSERT_EQ(node->unsubscribe("non_existent_group", {}), Node::Error::NOT_FOUND);
}

TEST(NodeImplAdvancedTest, SendLargeDataEdgeCases) {
    auto node = std::make_shared<NodeImpl>("test_node", false, 0);
    node->initialize(0);

    // Invalid args
    ASSERT_EQ(node->sendLargeData("", "ch", "t", nullptr, 0), Node::Error::INVALID_ARG);
    
    // Valid send (will create channel)
    std::vector<uint8_t> data(100, 0);
    ASSERT_EQ(node->sendLargeData("g", "ch_test", "t", data.data(), data.size()), Node::Error::NO_ERROR);
}
