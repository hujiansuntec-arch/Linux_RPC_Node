#include "simple_test.h"
#include "nexus/registry/SharedMemoryRegistry.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <atomic>

using namespace Nexus::rpc;

// Helper to access private members if needed (but we can't use friend)
// So we rely on public API and side effects.

TEST(RegistryCoverageFinal, ConstructorDestructor) {
    // Test default constructor
    SharedMemoryRegistry registry;
    // Destructor called automatically
}

TEST(RegistryCoverageFinal, Initialize_CreateNew) {
    // Ensure clean state
    shm_unlink("/librpc_registry");

    SharedMemoryRegistry registry;
    ASSERT_TRUE(registry.initialize());
    
    // Verify it created the file
    int fd = shm_open("/librpc_registry", O_RDONLY, 0666);
    ASSERT_NE(fd, -1);
    close(fd);
}

TEST(RegistryCoverageFinal, Initialize_OpenExisting) {
    // Ensure clean state
    shm_unlink("/librpc_registry");

    // Create first instance
    {
        SharedMemoryRegistry registry1;
        ASSERT_TRUE(registry1.initialize());
    } // registry1 destroyed, but file remains (ref_count logic might delete it if 0)

    // Wait, if ref_count drops to 0, it unlinks.
    // We need to keep one instance alive or manually create the SHM without the class to simulate "existing but not owned by this process"
    
    // Let's manually create the SHM file to simulate an existing registry
    int fd = shm_open("/librpc_registry", O_CREAT | O_RDWR, 0666);
    ASSERT_NE(fd, -1);
    ftruncate(fd, sizeof(SharedMemoryRegistry::MAX_REGISTRY_ENTRIES * 256 + 4096)); // Rough size
    // We need to initialize it properly or the class will think it's corrupted
    // Actually, the class handles initialization.
    // If we just create the file, the class might see size 0 or wrong size.
    
    // Let's use the class to create it, but prevent it from unlinking on destruction?
    // The destructor unlinks if ref_count == 0.
    // We can fork? No, simple_test doesn't support fork well.
    
    // We can just keep registry1 alive.
    SharedMemoryRegistry registry1;
    ASSERT_TRUE(registry1.initialize());
    
    SharedMemoryRegistry registry2;
    ASSERT_TRUE(registry2.initialize()); // Should open existing
}

TEST(RegistryCoverageFinal, Initialize_CorruptedSize) {
    shm_unlink("/librpc_registry");
    
    // Create a file with wrong size
    int fd = shm_open("/librpc_registry", O_CREAT | O_RDWR, 0666);
    ASSERT_NE(fd, -1);
    ftruncate(fd, 100); // Too small
    close(fd);
    
    SharedMemoryRegistry registry;
    // It should detect wrong size and fail (or try to fix if size is 0)
    // The code says: if size != sizeof(RegistryRegion), return false.
    ASSERT_FALSE(registry.initialize());
    
    shm_unlink("/librpc_registry");
}

TEST(RegistryCoverageFinal, Initialize_EmptyFile) {
    shm_unlink("/librpc_registry");
    
    // Create empty file
    int fd = shm_open("/librpc_registry", O_CREAT | O_RDWR, 0666);
    ASSERT_NE(fd, -1);
    // Don't truncate, size is 0
    close(fd);
    
    SharedMemoryRegistry registry;
    // Code says: if size == 0, resize and reinitialize
    ASSERT_TRUE(registry.initialize());
    
    shm_unlink("/librpc_registry");
}

TEST(RegistryCoverageFinal, Register_Unregister) {
    shm_unlink("/librpc_registry");
    SharedMemoryRegistry registry;
    ASSERT_TRUE(registry.initialize());
    
    ASSERT_TRUE(registry.registerNode("node1", "/shm1"));
    ASSERT_TRUE(registry.nodeExists("node1"));
    
    NodeInfo info;
    ASSERT_TRUE(registry.findNode("node1", info));
    ASSERT_EQ(info.node_id, "node1");
    ASSERT_EQ(info.shm_name, "/shm1");
    
    ASSERT_TRUE(registry.unregisterNode("node1"));
    ASSERT_FALSE(registry.nodeExists("node1"));
}

TEST(RegistryCoverageFinal, Register_Duplicate) {
    shm_unlink("/librpc_registry");
    SharedMemoryRegistry registry;
    ASSERT_TRUE(registry.initialize());
    
    ASSERT_TRUE(registry.registerNode("node1", "/shm1"));
    // Register same node again should update heartbeat or return true
    ASSERT_TRUE(registry.registerNode("node1", "/shm1"));
}

TEST(RegistryCoverageFinal, Register_Full) {
    shm_unlink("/librpc_registry");
    SharedMemoryRegistry registry;
    ASSERT_TRUE(registry.initialize());
    
    // Fill registry
    for (int i = 0; i < 256; ++i) {
        std::string id = "node" + std::to_string(i);
        ASSERT_TRUE(registry.registerNode(id, "/shm"));
    }
    
    // Next one should fail
    ASSERT_FALSE(registry.registerNode("overflow", "/shm"));
}

TEST(RegistryCoverageFinal, UpdateHeartbeat) {
    shm_unlink("/librpc_registry");
    SharedMemoryRegistry registry;
    ASSERT_TRUE(registry.initialize());
    
    ASSERT_TRUE(registry.registerNode("node1", "/shm1"));
    ASSERT_TRUE(registry.updateHeartbeat("node1"));
    ASSERT_FALSE(registry.updateHeartbeat("nonexistent"));
}

TEST(RegistryCoverageFinal, GetAllNodes) {
    shm_unlink("/librpc_registry");
    SharedMemoryRegistry registry;
    ASSERT_TRUE(registry.initialize());
    
    registry.registerNode("node1", "/shm1");
    registry.registerNode("node2", "/shm2");
    
    auto nodes = registry.getAllNodes();
    ASSERT_EQ(nodes.size(), 2);
}

TEST(RegistryCoverageFinal, CleanupStale) {
    shm_unlink("/librpc_registry");
    SharedMemoryRegistry registry;
    ASSERT_TRUE(registry.initialize());
    
    registry.registerNode("node1", "/shm1");
    
    // Manually hack the heartbeat? 
    // We can't easily access private memory to change heartbeat time.
    // But we can wait? No, waiting is slow.
    // The cleanupOrphanedNodes takes a timeout.
    // If we pass 0 timeout, it should clean up everything that hasn't updated *now*?
    // No, it checks current_time - last_heartbeat > timeout.
    // If we just registered, last_heartbeat is now.
    // So we need to wait at least 1 second if we pass timeout 0?
    // Or we can sleep for 1.1 seconds and pass timeout 1.
    
    // Let's try sleeping 10ms and pass timeout 0.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Note: time() resolution is seconds.
    // So we might need to wait 1 second.
    
    // To avoid slow tests, maybe we can skip this or accept 1s delay.
    // Or we can rely on the fact that we can't easily test this without mocking time.
    // But wait, we can register, then NOT update heartbeat.
    
    // Let's try to register, wait 2 seconds, then cleanup with timeout 1s.
    // This adds 2s to test suite. Acceptable.
    
    sleep(2);
    size_t cleaned = registry.cleanupOrphanedNodes(1000); // 1000ms = 1s
    ASSERT_EQ(cleaned, 1);
    ASSERT_FALSE(registry.nodeExists("node1"));
}

TEST(RegistryCoverageFinal, Destructor_RefCount) {
    shm_unlink("/librpc_registry");
    
    {
        SharedMemoryRegistry registry1;
        registry1.initialize();
        // ref_count = 1
        
        {
            SharedMemoryRegistry registry2;
            registry2.initialize();
            // ref_count = 2
        }
        // registry2 destr, ref_count = 1
        
        // Verify file still exists
        int fd = shm_open("/librpc_registry", O_RDONLY, 0666);
        ASSERT_NE(fd, -1);
        close(fd);
    }
    // registry1 destr, ref_count = 0 -> unlink
    
    // Verify file is gone
    int fd = shm_open("/librpc_registry", O_RDONLY, 0666);
    ASSERT_EQ(fd, -1);
}
