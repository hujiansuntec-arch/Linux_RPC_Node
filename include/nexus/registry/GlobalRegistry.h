#pragma once

#include <memory>
#include <map>
#include <vector>
#include <mutex>

namespace librpc {
    // Forward declaration
    class NodeImpl;
    struct ServiceDescriptor;
}

namespace nexus {

/**
 * @brief Global registry for managing nodes and services
 * Replaces scattered static members across multiple files
 */
class GlobalRegistry {
public:
    /**
     * @brief Get global registry instance (singleton)
     */
    static GlobalRegistry& instance();
    
    // Node registry
    void registerNode(const std::string& node_id, std::weak_ptr<librpc::NodeImpl> node);
    void unregisterNode(const std::string& node_id);
    std::vector<std::shared_ptr<librpc::NodeImpl>> getAllNodes();
    std::shared_ptr<librpc::NodeImpl> findNode(const std::string& node_id);
    
    // Service registry
    void registerService(const std::string& group, const librpc::ServiceDescriptor& svc);
    void unregisterService(const std::string& group, const librpc::ServiceDescriptor& svc);
    std::vector<librpc::ServiceDescriptor> findServices(const std::string& group = "");
    void clearServices();
    
    // Statistics
    size_t getNodeCount() const;
    size_t getServiceCount() const;
    
private:
    GlobalRegistry() = default;
    ~GlobalRegistry() = default;
    
    // Disable copy and move
    GlobalRegistry(const GlobalRegistry&) = delete;
    GlobalRegistry& operator=(const GlobalRegistry&) = delete;
    
    // Node registry (C++14: use mutex instead of shared_mutex)
    mutable std::mutex nodes_mutex_;
    std::map<std::string, std::weak_ptr<librpc::NodeImpl>> nodes_;
    
    // Service registry: group -> services (C++14: use mutex instead of shared_mutex)
    mutable std::mutex services_mutex_;
    std::map<std::string, std::vector<librpc::ServiceDescriptor>> services_;
};

} // namespace nexus
