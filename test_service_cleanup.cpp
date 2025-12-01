/**
 * Test to verify that GlobalRegistry::unregisterNode() cleans up all services
 * registered by the node to prevent "zombie services"
 */

#include "nexus/core/NodeImpl.h"
#include "nexus/registry/GlobalRegistry.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace librpc;

int main() {
    std::cout << "\n=== Testing Service Cleanup on Node Destruction ===\n\n";
    
    auto& registry = nexus::GlobalRegistry::instance();
    
    // Step 1: Create a node and register services manually
    std::cout << "[Step 1] Creating node and registering services manually...\n";
    {
        // Create node
        auto node_impl = std::make_shared<NodeImpl>("test_cleanup_node", false, 0);
        node_impl->initialize(0);
        
        // Manually register 3 services
        ServiceDescriptor svc1, svc2, svc3;
        svc1.node_id = "test_cleanup_node";
        svc1.group = "cleanup_test";
        svc1.topic = "topic1";
        svc1.type = ServiceType::ALL;
        
        svc2.node_id = "test_cleanup_node";
        svc2.group = "cleanup_test";
        svc2.topic = "topic2";
        svc2.type = ServiceType::ALL;
        
        svc3.node_id = "test_cleanup_node";
        svc3.group = "cleanup_test";
        svc3.topic = "topic3";
        svc3.type = ServiceType::ALL;
        
        registry.registerService("cleanup_test", svc1);
        registry.registerService("cleanup_test", svc2);
        registry.registerService("cleanup_test", svc3);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto services_before = registry.findServices("cleanup_test");
        std::cout << "  Services registered: " << services_before.size() << "\n";
        
        for (const auto& svc : services_before) {
            std::cout << "    - " << svc.node_id << " : " << svc.topic << "\n";
        }
        
        // Step 2: Node goes out of scope (destructor called)
        std::cout << "\n[Step 2] Destroying node (calling unregisterNode())...\n";
    }
    // Node destroyed here - unregisterNode() should clean up services
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Step 3: Verify services are cleaned up
    std::cout << "\n[Step 3] Checking if services were cleaned up...\n";
    auto services_after = registry.findServices("cleanup_test");
    std::cout << "  Services remaining: " << services_after.size() << "\n";
    
    if (services_after.empty()) {
        std::cout << "\n✅ Test PASSED: All services were cleaned up!\n";
        std::cout << "   (No zombie services found)\n\n";
        return 0;
    } else {
        std::cout << "\n❌ Test FAILED: Zombie services detected!\n";
        std::cout << "   Found " << services_after.size() << " orphaned services:\n";
        for (const auto& svc : services_after) {
            std::cout << "    - " << svc.node_id << " : " << svc.topic << "\n";
        }
        std::cout << "\n";
        return 1;
    }
}
