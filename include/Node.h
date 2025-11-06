// Copyright (c) 2025 Baidu.com, Inc. All Rights Reserved

#pragma once

#include <stdint.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace librpc {

/**
 * @brief Node interface for peer-to-peer communication
 * 
 * Features:
 * - Subscribe to topics within message groups
 * - Broadcast messages to subscribers
 * - Support both in-process and inter-process communication
 * - Multiple nodes can coexist in the same process
 */
class Node {
public:
    using Property = std::string;
    using Callback = std::function<void(const Property& msg_group, 
                                       const Property& topic, 
                                       const uint8_t* payload, 
                                       size_t size)>;

    enum Error {
        NO_ERROR         = 0,
        INVALID_ARG      = 1,
        NOT_INITIALIZED  = 2,
        ALREADY_EXISTS   = 3,
        NOT_FOUND        = 4,
        NETWORK_ERROR    = 5,
        TIMEOUT          = 6,
        UNEXPECTED_ERROR = 99,
    };

    virtual ~Node() = default;

    /**
     * @brief Broadcast a message to all subscribers of a topic
     * @param msg_group Message group name
     * @param topic Topic name within the group
     * @param payload Message payload data
     * @return Error code
     */
    virtual Error broadcast(const Property& msg_group, 
                          const Property& topic, 
                          const Property& payload) = 0;

    /**
     * @brief Subscribe to topics within a message group
     * @param msg_group Message group name
     * @param topics List of topic names to subscribe
     * @param callback Callback function to receive messages
     * @return Error code
     */
    virtual Error subscribe(const Property& msg_group, 
                          const std::vector<Property>& topics, 
                          const Callback& callback) = 0;

    /**
     * @brief Unsubscribe from topics within a message group
     * @param msg_group Message group name
     * @param topics List of topic names to unsubscribe
     * @return Error code
     */
    virtual Error unsubscribe(const Property& msg_group, 
                            const std::vector<Property>& topics) = 0;

    /**
     * @brief Get list of subscribed groups and topics
     * @return Vector of <group, topics> pairs
     */
    virtual std::vector<std::pair<Property, std::vector<Property>>> 
    getSubscriptions() const = 0;

    /**
     * @brief Check if subscribed to a specific topic
     * @param msg_group Message group name
     * @param topic Topic name
     * @return true if subscribed
     */
    virtual bool isSubscribed(const Property& msg_group, 
                             const Property& topic) const = 0;
};

/**
 * @brief Create a new Node instance
 * 
 * The node automatically supports both in-process and inter-process communication:
 * - In-process: Direct function calls (zero-copy, <1μs latency)
 * - Inter-process: UDP communication (automatic discovery and routing)
 * 
 * The framework automatically determines the communication method when broadcasting:
 * - If target nodes are in the same process → in-process delivery
 * - If target nodes are in other processes → UDP delivery
 * 
 * @param node_id Unique identifier for this node (optional, auto-generated if empty)
 * @return Shared pointer to Node instance
 */
std::shared_ptr<Node> createNode(const std::string& node_id = "");

/**
 * @brief Get the default communication interface (singleton node)
 * @return Shared pointer to default Node instance
 */
std::shared_ptr<Node> communicationInterface();

} // namespace librpc
