#include "nexus/registry/GlobalRegistry.h"
#include "nexus/core/NodeImpl.h"  // librpc namespace
#include "nexus/core/Node.h"
#include <algorithm>

namespace nexus {

GlobalRegistry& GlobalRegistry::instance() {
    static GlobalRegistry registry;
    return registry;
}

void GlobalRegistry::registerNode(const std::string& node_id, std::weak_ptr<librpc::NodeImpl> node) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    nodes_[node_id] = node;
}

void GlobalRegistry::unregisterNode(const std::string& node_id) {
    // First, remove the node from the registry
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        nodes_.erase(node_id);
    }
    
    // Then, clean up all services registered by this node
    // This prevents "zombie services" from remaining after node destruction
    {
        std::lock_guard<std::mutex> lock(services_mutex_);
        for (auto it = services_.begin(); it != services_.end();) {
            auto& vec = it->second;
            
            // Remove all services from this node
            vec.erase(
                std::remove_if(vec.begin(), vec.end(),
                    [&node_id](const librpc::ServiceDescriptor& s) {
                        return s.node_id == node_id;
                    }),
                vec.end()
            );
            
            // Remove empty groups
            if (vec.empty()) {
                it = services_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

std::vector<std::shared_ptr<librpc::NodeImpl>> GlobalRegistry::getAllNodes() {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::vector<std::shared_ptr<librpc::NodeImpl>> result;
    
    for (auto it = nodes_.begin(); it != nodes_.end();) {
        if (auto node = it->second.lock()) {
            result.push_back(node);
            ++it;
        } else {
            // Remove expired weak_ptr
            it = nodes_.erase(it);
        }
    }
    
    return result;
}

std::shared_ptr<librpc::NodeImpl> GlobalRegistry::findNode(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        return it->second.lock();
    }
    return nullptr;
}

void GlobalRegistry::registerService(const std::string& group, const librpc::ServiceDescriptor& svc) {
    std::lock_guard<std::mutex> lock(services_mutex_);
    services_[group].push_back(svc);
}

void GlobalRegistry::unregisterService(const std::string& group, const librpc::ServiceDescriptor& svc) {
    std::lock_guard<std::mutex> lock(services_mutex_);
    
    auto it = services_.find(group);
    if (it != services_.end()) {
        auto& vec = it->second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                [&svc](const librpc::ServiceDescriptor& s) {
                    return s.node_id == svc.node_id && 
                           s.topic == svc.topic;
                }),
            vec.end()
        );
        
        if (vec.empty()) {
            services_.erase(it);
        }
    }
}

std::vector<librpc::ServiceDescriptor> GlobalRegistry::findServices(const std::string& group) {
    std::lock_guard<std::mutex> lock(services_mutex_);
    std::vector<librpc::ServiceDescriptor> result;
    
    if (group.empty()) {
        // Return all services - C++14 compatible iteration
        for (auto it = services_.begin(); it != services_.end(); ++it) {
            const auto& svcs = it->second;
            result.insert(result.end(), svcs.begin(), svcs.end());
        }
    } else {
        // Return services for specific group
        auto it = services_.find(group);
        if (it != services_.end()) {
            result = it->second;
        }
    }
    
    return result;
}

void GlobalRegistry::clearServices() {
    std::lock_guard<std::mutex> lock(services_mutex_);
    services_.clear();
}

size_t GlobalRegistry::getNodeCount() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    return nodes_.size();
}

size_t GlobalRegistry::getServiceCount() const {
    std::lock_guard<std::mutex> lock(services_mutex_);
    size_t count = 0;
    for (auto it = services_.begin(); it != services_.end(); ++it) {
        count += it->second.size();
    }
    return count;
}

} // namespace nexus
