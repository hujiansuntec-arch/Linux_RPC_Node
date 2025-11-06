// Copyright (c) 2025 Baidu.com, Inc. All Rights Reserved

#pragma once

#include "Node.h"
#include "Message.h"
#include <map>
#include <set>
#include <mutex>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>

namespace librpc {

// Forward declarations
class UdpTransport;

/**
 * @brief Node implementation supporting both in-process and inter-process communication
 */
class NodeImpl : public Node, public std::enable_shared_from_this<NodeImpl> {
public:
    NodeImpl(const std::string& node_id, bool use_udp, uint16_t udp_port);
    ~NodeImpl() override;

    // Initialize after construction (must be called after shared_ptr is created)
    void initialize(uint16_t udp_port);

    // Node interface implementation
    Error broadcast(const Property& msg_group, 
                   const Property& topic, 
                   const Property& payload) override;

    Error subscribe(const Property& msg_group, 
                   const std::vector<Property>& topics, 
                   const Callback& callback) override;

    Error unsubscribe(const Property& msg_group, 
                     const std::vector<Property>& topics) override;

    std::vector<std::pair<Property, std::vector<Property>>> 
    getSubscriptions() const override;

    bool isSubscribed(const Property& msg_group, 
                     const Property& topic) const override;

    // Internal methods
    std::string getNodeId() const { return node_id_; }
    uint16_t getUdpPort() const;
    
private:
    // Message handling
    void handleMessage(const std::string& source_node_id,
                      const std::string& group,
                      const std::string& topic,
                      const uint8_t* payload,
                      size_t payload_len);
    
    void handleSubscribe(const std::string& remote_node_id,
                        uint16_t remote_port,
                        const std::string& remote_addr,
                        const std::string& group,
                        const std::string& topic);
    
    void handleUnsubscribe(const std::string& remote_node_id,
                          const std::string& group,
                          const std::string& topic);
    
    // Query existing nodes for their subscriptions (called when a new node joins)
    void queryExistingSubscriptions();
    
    // Handle subscription query from a new node
    void handleQuerySubscriptions(const std::string& remote_node_id,
                                 uint16_t remote_port,
                                 const std::string& remote_addr);
    
    // Handle subscription reply from existing nodes
    void handleSubscriptionReply(const std::string& remote_node_id,
                                uint16_t remote_port,
                                const std::string& remote_addr,
                                const std::string& group,
                                const std::string& topic);
    
    // In-process delivery
    void deliverInProcess(const std::string& group,
                         const std::string& topic,
                         const uint8_t* payload,
                         size_t payload_len);
    
    // Inter-process delivery via UDP (targeted)
    void deliverViaUdp(const std::vector<uint8_t>& packet,
                      const std::string& group,
                      const std::string& topic);
    
    // Send subscription registration to all nodes
    void broadcastSubscription(const std::string& group,
                              const std::string& topic,
                              bool is_subscribe);
    
    // Subscription management
    struct SubscriptionInfo {
        std::set<std::string> topics;  // Topics within this group
        Callback callback;             // Callback for this group
    };
    
    // Remote node subscription info
    struct RemoteNodeInfo {
        std::string node_id;
        std::string address;
        uint16_t port;
        std::set<SubscriptionKey> subscriptions;  // What this remote node subscribed to
    };
    
private:
    std::string node_id_;
    bool use_udp_;
    std::atomic<bool> running_;
    
    // Local subscriptions: group -> SubscriptionInfo
    mutable std::mutex subscriptions_mutex_;
    std::map<std::string, SubscriptionInfo> subscriptions_;
    
    // Remote nodes registry: node_id -> RemoteNodeInfo
    mutable std::mutex remote_nodes_mutex_;
    std::map<std::string, RemoteNodeInfo> remote_nodes_;
    
    // UDP transport (single socket for all communication)
    std::unique_ptr<UdpTransport> udp_transport_;
    
    // Global registry for in-process communication
    static std::mutex registry_mutex_;
    static std::map<std::string, std::weak_ptr<NodeImpl>> node_registry_;
    
    // Register/unregister this node
    void registerNode();
    void unregisterNode();
    
    // Get all nodes (including this one)
    static std::vector<std::shared_ptr<NodeImpl>> getAllNodes();
};

} // namespace librpc
