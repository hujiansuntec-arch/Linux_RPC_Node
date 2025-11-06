// Copyright (c) 2025 Baidu.com, Inc. All Rights Reserved

#include "NodeImpl.h"
#include "UdpTransport.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>

namespace librpc {

// Port range constants for node discovery
static constexpr uint16_t PORT_BASE = 47200;
static constexpr uint16_t PORT_MAX = 47999;
static constexpr uint16_t PORT_COUNT = PORT_MAX - PORT_BASE + 1;  // 800 ports

// Static members
std::mutex NodeImpl::registry_mutex_;
std::map<std::string, std::weak_ptr<NodeImpl>> NodeImpl::node_registry_;

// Generate unique node ID
static std::string generateNodeId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::stringstream ss;
    ss << "node_" << std::hex << std::setfill('0') << std::setw(12) << ms;
    return ss.str();
}

NodeImpl::NodeImpl(const std::string& node_id, bool use_udp, uint16_t udp_port)
    : node_id_(node_id.empty() ? generateNodeId() : node_id)
    , use_udp_(use_udp)
    , running_(true) {
    
    // UDP transport initialization will be done in a separate init method
}

NodeImpl::~NodeImpl() {
    running_ = false;
    
    // Shutdown UDP transport
    if (udp_transport_) {
        udp_transport_->shutdown();
    }
    
    // Unregister this node
    unregisterNode();
}

// Initialization method to be called after construction
void NodeImpl::initialize(uint16_t udp_port) {
    // Register this node in global registry
    registerNode();
    
    // Initialize UDP transport if enabled
    if (use_udp_) {
        // Create callback handler (shared between both sockets)
        auto receiveCallback = [this](const uint8_t* data, size_t size, const std::string& from_addr) {
            // Parse and handle received message
            if (size < sizeof(MessagePacket)) {
                return;
            }
            
            const MessagePacket* packet = reinterpret_cast<const MessagePacket*>(data);
            if (!packet->isValid()) {
                return;
            }
            
            // Extract message components
            std::string source_node(packet->node_id);
            std::string group(packet->getGroup(), packet->group_len);
            std::string topic(packet->getTopic(), packet->topic_len);
            uint16_t sender_port = packet->udp_port;
            MessageType msg_type = static_cast<MessageType>(packet->msg_type);
            
            // Skip our own messages
            if (source_node == node_id_) {
                return;
            }
            
            // Handle based on message type
            switch (msg_type) {
                case MessageType::SUBSCRIBE:
                    handleSubscribe(source_node, sender_port, from_addr, group, topic);
                    break;
                    
                case MessageType::UNSUBSCRIBE:
                    handleUnsubscribe(source_node, group, topic);
                    break;
                    
                case MessageType::DATA:
                    handleMessage(source_node, group, topic, 
                                packet->getPayload(), packet->payload_len);
                    break;
                    
                case MessageType::QUERY_SUBSCRIPTIONS:
                    // Another node is asking for our subscriptions
                    handleQuerySubscriptions(source_node, sender_port, from_addr);
                    break;
                    
                case MessageType::SUBSCRIPTION_REPLY:
                    // Received subscription info from an existing node
                    handleSubscriptionReply(source_node, sender_port, from_addr,
                                           group, topic);
                    break;
            }
        };
        
        // Initialize main UDP socket (for all messages)
        // Use a fixed base port range (47200-47999) for easier discovery (800 ports)
        udp_transport_ = std::make_unique<UdpTransport>();
        
        // Try ports in our scan range first
        uint16_t target_port = udp_port;
        bool bound = false;
        
        if (target_port == 0) {
            // Auto-select from our known range (47200-47999)
            // Use random starting point to reduce collision in multi-process scenarios
            static std::atomic<uint16_t> next_port{0};
            
            // Initialize on first use with a random offset to spread allocations
            uint16_t current = next_port.load();
            if (current == 0) {
                auto now = std::chrono::high_resolution_clock::now();
                uint16_t random_offset = static_cast<uint16_t>(
                    now.time_since_epoch().count() % 800);
                next_port.store(47200 + random_offset);
            }
            
            // Try up to 800 ports in the range (47200-47999)
            for (int attempts = 0; attempts < PORT_COUNT && !bound; attempts++) {
                target_port = next_port.fetch_add(1);
                
                // Wrap around if we exceed the range
                if (target_port > PORT_MAX) {
                    // Reset to base and try again
                    uint16_t expected = target_port;
                    next_port.compare_exchange_strong(expected, PORT_BASE);
                    target_port = next_port.fetch_add(1);
                }
                
                bound = udp_transport_->initialize(target_port);
            }
            
            // If still not bound, let system choose
            if (!bound) {
                bound = udp_transport_->initialize(0);
            }
        } else {
            // User specified port
            bound = udp_transport_->initialize(target_port);
        }
        
        if (!bound) {
            // Failed to initialize
            return;
        }
        udp_transport_->setReceiveCallback(receiveCallback);
        
        // After UDP transport is ready, query existing nodes for their subscriptions
        // Use port scanning to discover nodes on localhost
        queryExistingSubscriptions();
    }
}

Node::Error NodeImpl::broadcast(const Property& msg_group, 
                                const Property& topic, 
                                const Property& payload) {
    if (msg_group.empty() || topic.empty()) {
        return Error::INVALID_ARG;
    }
    
    if (!running_) {
        return Error::NOT_INITIALIZED;
    }
    
    // Build message packet
    auto packet = MessageBuilder::build(node_id_, msg_group, topic, payload, 
                                       getUdpPort(), MessageType::DATA);
    
    // Deliver to in-process subscribers
    deliverInProcess(msg_group, topic, 
                    reinterpret_cast<const uint8_t*>(payload.data()), 
                    payload.size());
    
    // Deliver via UDP to inter-process subscribers
    if (use_udp_ && udp_transport_ && udp_transport_->isInitialized()) {
        deliverViaUdp(packet, msg_group, topic);
    }
    
    return Error::NO_ERROR;
}

Node::Error NodeImpl::subscribe(const Property& msg_group, 
                                const std::vector<Property>& topics, 
                                const Callback& callback) {
    if (msg_group.empty() || topics.empty() || !callback) {
        return Error::INVALID_ARG;
    }
    
    if (!running_) {
        return Error::NOT_INITIALIZED;
    }
    
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    // Get or create subscription info for this group
    auto& sub_info = subscriptions_[msg_group];
    
    // Add topics
    for (const auto& topic : topics) {
        if (!topic.empty()) {
            sub_info.topics.insert(topic);
        }
    }
    
    // Update callback
    sub_info.callback = callback;
    
    // Broadcast subscription to remote nodes via UDP
    for (const auto& topic : topics) {
        if (!topic.empty()) {
            broadcastSubscription(msg_group, topic, true);
        }
    }
    
    return Error::NO_ERROR;
}

Node::Error NodeImpl::unsubscribe(const Property& msg_group, 
                                  const std::vector<Property>& topics) {
    if (msg_group.empty()) {
        return Error::INVALID_ARG;
    }
    
    if (!running_) {
        return Error::NOT_INITIALIZED;
    }
    
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    auto it = subscriptions_.find(msg_group);
    if (it == subscriptions_.end()) {
        return Error::NOT_FOUND;
    }
    
    if (topics.empty()) {
        // Remove entire group
        subscriptions_.erase(it);
        
        // Broadcast unsubscribe for all topics in the group
        for (const auto& topic : it->second.topics) {
            broadcastSubscription(msg_group, topic, false);
        }
    } else {
        // Remove specific topics
        for (const auto& topic : topics) {
            it->second.topics.erase(topic);
            // Broadcast unsubscribe for this topic
            broadcastSubscription(msg_group, topic, false);
        }
        
        // Remove group if no topics left
        if (it->second.topics.empty()) {
            subscriptions_.erase(it);
        }
    }
    
    return Error::NO_ERROR;
}

std::vector<std::pair<Node::Property, std::vector<Node::Property>>> 
NodeImpl::getSubscriptions() const {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    std::vector<std::pair<Property, std::vector<Property>>> result;
    result.reserve(subscriptions_.size());
    
    for (const auto& pair : subscriptions_) {
        std::vector<Property> topics(pair.second.topics.begin(), pair.second.topics.end());
        result.emplace_back(pair.first, std::move(topics));
    }
    
    return result;
}

bool NodeImpl::isSubscribed(const Property& msg_group, const Property& topic) const {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    auto it = subscriptions_.find(msg_group);
    if (it == subscriptions_.end()) {
        return false;
    }
    
    return it->second.topics.find(topic) != it->second.topics.end();
}

uint16_t NodeImpl::getUdpPort() const {
    if (udp_transport_) {
        return udp_transport_->getPort();
    }
    return 0;
}

void NodeImpl::handleMessage(const std::string& source_node_id,
                            const std::string& group,
                            const std::string& topic,
                            const uint8_t* payload,
                            size_t payload_len) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    // Find subscription for this group
    auto it = subscriptions_.find(group);
    if (it == subscriptions_.end()) {
        return; // Not subscribed to this group
    }
    
    // Check if subscribed to this topic
    if (it->second.topics.find(topic) == it->second.topics.end()) {
        return; // Not subscribed to this topic
    }
    
    // Invoke callback
    if (it->second.callback) {
        try {
            it->second.callback(group, topic, payload, payload_len);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

void NodeImpl::deliverInProcess(const std::string& group,
                               const std::string& topic,
                               const uint8_t* payload,
                               size_t payload_len) {
    // Get all registered nodes
    auto nodes = getAllNodes();
    
    // Deliver to each node (except ourselves)
    for (const auto& node : nodes) {
        if (node && node.get() != this) {
            node->handleMessage(node_id_, group, topic, payload, payload_len);
        }
    }
}

void NodeImpl::deliverViaUdp(const std::vector<uint8_t>& packet,
                            const std::string& group,
                            const std::string& topic) {
    if (!udp_transport_) {
        return;
    }
    
    // Build a set of local node IDs once to avoid repeated lookups
    std::set<std::string> local_node_ids;
    {
        std::lock_guard<std::mutex> registry_lock(registry_mutex_);
        for (const auto& pair : node_registry_) {
            if (pair.second.lock()) {
                local_node_ids.insert(pair.first);
            }
        }
    }
    
    std::lock_guard<std::mutex> lock(remote_nodes_mutex_);
    
    // Send to remote nodes that subscribed to this group/topic
    SubscriptionKey key(group, topic);
    for (const auto& pair : remote_nodes_) {
        const auto& remote_node = pair.second;
        
        // Skip local nodes - they are already handled by deliverInProcess
        if (local_node_ids.count(remote_node.node_id) > 0) {
            continue;
        }
        
        if (remote_node.subscriptions.find(key) != remote_node.subscriptions.end()) {
            // This remote node subscribed to this topic, send via UDP
            if (!remote_node.address.empty() && remote_node.port > 0) {
                udp_transport_->send(packet.data(), packet.size(), 
                                   remote_node.address, remote_node.port);
            }
        }
    }
}

void NodeImpl::broadcastSubscription(const std::string& group,
                                    const std::string& topic,
                                    bool is_subscribe) {
    if (!use_udp_ || !udp_transport_ || !udp_transport_->isInitialized()) {
        return;
    }
    
    MessageType msg_type = is_subscribe ? MessageType::SUBSCRIBE : MessageType::UNSUBSCRIBE;
    auto packet = MessageBuilder::build(node_id_, group, topic, "", 
                                       getUdpPort(), msg_type);
    
    std::lock_guard<std::mutex> lock(remote_nodes_mutex_);
    
    // Send to all known remote nodes
    for (const auto& pair : remote_nodes_) {
        const auto& remote_node = pair.second;
        if (!remote_node.address.empty() && remote_node.port > 0) {
            udp_transport_->send(packet.data(), packet.size(), 
                               remote_node.address, remote_node.port);
        }
    }
    
    // If no known nodes yet, do a full scan to announce ourselves
    if (remote_nodes_.empty()) {
        const uint16_t my_port = getUdpPort();
        
        // Scan all ports to announce our subscription
        for (int port = PORT_BASE; port <= PORT_MAX; port++) {
            if (port != my_port) {
                udp_transport_->send(packet.data(), packet.size(), "127.0.0.1", port);
            }
        }
    }
}

void NodeImpl::handleSubscribe(const std::string& remote_node_id,
                               uint16_t remote_port,
                               const std::string& remote_addr,
                               const std::string& group,
                               const std::string& topic) {
    // Don't add ourselves to remote nodes list (self-subscription filter)
    if (remote_node_id == node_id_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(remote_nodes_mutex_);
    
    // Get or create remote node info
    auto& remote_node = remote_nodes_[remote_node_id];
    remote_node.node_id = remote_node_id;
    remote_node.port = remote_port;
    remote_node.address = remote_addr;
    
    // Add subscription
    SubscriptionKey key(group, topic);
    remote_node.subscriptions.insert(key);
}

void NodeImpl::handleUnsubscribe(const std::string& remote_node_id,
                                const std::string& group,
                                const std::string& topic) {
    std::lock_guard<std::mutex> lock(remote_nodes_mutex_);
    
    auto it = remote_nodes_.find(remote_node_id);
    if (it == remote_nodes_.end()) {
        return;
    }
    
    // Remove subscription
    SubscriptionKey key(group, topic);
    it->second.subscriptions.erase(key);
    
    // Remove remote node if no subscriptions left
    if (it->second.subscriptions.empty()) {
        remote_nodes_.erase(it);
    }
}

void NodeImpl::queryExistingSubscriptions() {
    if (!use_udp_ || !udp_transport_ || !udp_transport_->isInitialized()) {
        return;
    }
    
    // Scan all ports in the range to discover existing nodes
    // While this is 800 probes, QUERY messages are small and it's only done once at startup
    
    auto packet = MessageBuilder::build(node_id_, "", "", "", 
                                       getUdpPort(), MessageType::QUERY_SUBSCRIPTIONS);
    
    const uint16_t my_port = getUdpPort();
    
    // Scan all ports in range
    for (int port = PORT_BASE; port <= PORT_MAX; port++) {
        if (port != my_port) {
            udp_transport_->send(packet.data(), packet.size(), "127.0.0.1", port);
        }
    }
}

void NodeImpl::handleQuerySubscriptions(const std::string& remote_node_id,
                                       uint16_t remote_port,
                                       const std::string& remote_addr) {
    if (!use_udp_ || !udp_transport_ || !udp_transport_->isInitialized()) {
        return;
    }
    
    // Reply with all our subscriptions
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    for (const auto& sub_pair : subscriptions_) {
        const std::string& group = sub_pair.first;
        const auto& sub_info = sub_pair.second;
        
        for (const auto& topic : sub_info.topics) {
            // Send SUBSCRIPTION_REPLY message for each subscription
            auto packet = MessageBuilder::build(node_id_, group, topic, "", 
                                               getUdpPort(), MessageType::SUBSCRIPTION_REPLY);
            
            // Send directly to the querying node
            if (!remote_addr.empty() && remote_port > 0) {
                udp_transport_->send(packet.data(), packet.size(), remote_addr, remote_port);
            }
        }
    }
}

void NodeImpl::handleSubscriptionReply(const std::string& remote_node_id,
                                       uint16_t remote_port,
                                       const std::string& remote_addr,
                                       const std::string& group,
                                       const std::string& topic) {
    // Don't add ourselves to remote nodes list (self-subscription filter)
    if (remote_node_id == node_id_) {
        return;
    }
    
    // This is essentially the same as handleSubscribe
    // The remote node is telling us about one of their subscriptions
    std::lock_guard<std::mutex> lock(remote_nodes_mutex_);
    
    auto& remote_node = remote_nodes_[remote_node_id];
    remote_node.node_id = remote_node_id;
    remote_node.port = remote_port;
    remote_node.address = remote_addr;
    
    // Add subscription
    SubscriptionKey key(group, topic);
    remote_node.subscriptions.insert(key);
}

void NodeImpl::registerNode() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    node_registry_[node_id_] = shared_from_this();
}

void NodeImpl::unregisterNode() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    node_registry_.erase(node_id_);
}

std::vector<std::shared_ptr<NodeImpl>> NodeImpl::getAllNodes() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    std::vector<std::shared_ptr<NodeImpl>> result;
    result.reserve(node_registry_.size());
    
    // Clean up expired weak_ptr and collect valid nodes
    for (auto it = node_registry_.begin(); it != node_registry_.end(); ) {
        if (auto node = it->second.lock()) {
            result.push_back(node);
            ++it;
        } else {
            it = node_registry_.erase(it);
        }
    }
    
    return result;
}

// Factory functions
std::shared_ptr<Node> createNode(const std::string& node_id) {
    // Always enable UDP with auto-selected port (0 = auto-select)
    // The framework will automatically choose between in-process and inter-process communication
    auto node = std::make_shared<NodeImpl>(node_id, true, 0);
    std::static_pointer_cast<NodeImpl>(node)->initialize(0);
    return node;
}

// Default instance (singleton)
static std::weak_ptr<Node> g_default_node;
static std::mutex g_default_mutex;

std::shared_ptr<Node> communicationInterface() {
    std::lock_guard<std::mutex> lock(g_default_mutex);
    
    auto node = g_default_node.lock();
    if (!node) {
        node = createNode("default_node");
        g_default_node = node;
    }
    
    return node;
}

} // namespace librpc
