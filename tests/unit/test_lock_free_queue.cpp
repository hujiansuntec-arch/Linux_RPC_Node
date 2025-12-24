#include "simple_test.h"
#include "nexus/transport/LockFreeQueue.h"
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>

using namespace Nexus::rpc;

TEST(LockFreeQueueTest, BasicPushPop) {
    // Use a larger buffer for variable length test
    LockFreeRingBuffer<1024> queue;
    
    uint8_t data[128];
    size_t size;
    
    // Initially empty
    ASSERT_FALSE(queue.tryRead(data, size));

    uint8_t msg1[] = {1, 2, 3};
    ASSERT_TRUE(queue.tryWrite(msg1, sizeof(msg1)));
    
    uint8_t msg2[] = {4, 5, 6};
    ASSERT_TRUE(queue.tryWrite(msg2, sizeof(msg2)));

    // Read msg1
    ASSERT_TRUE(queue.tryRead(data, size));
    ASSERT_EQ(sizeof(msg1), size);
    ASSERT_EQ(1, data[0]);

    // Read msg2
    ASSERT_TRUE(queue.tryRead(data, size));
    ASSERT_EQ(sizeof(msg2), size);
    ASSERT_EQ(4, data[0]);

    // Empty again
    ASSERT_FALSE(queue.tryRead(data, size));
}

TEST(LockFreeQueueTest, OverwriteBehavior) {
    // Small buffer to force wrap/drop
    // Header(8) + Data(1) + Padding(7) = 16 bytes per message
    // Capacity 64 bytes = 4 messages
    LockFreeRingBuffer<64> queue;
    
    // Push 5 items
    // 1, 2, 3, 4 -> Full
    // 5 -> Should fail or drop? 
    // Current implementation returns false if full, does NOT auto-drop for variable length
    // because dropping variable length messages is complex (need to parse headers)
    
    for (int i = 1; i <= 4; ++i) {
        uint8_t val = static_cast<uint8_t>(i);
        ASSERT_TRUE(queue.tryWrite(&val, 1));
    }
    
    // 5th should fail
    uint8_t val = 5;
    ASSERT_FALSE(queue.tryWrite(&val, 1));
    
    uint8_t data[128];
    size_t size;
    
    // First read should be 1
    ASSERT_TRUE(queue.tryRead(data, size));
    ASSERT_EQ(1, data[0]);

    // Note: With the current implementation (head == tail means empty),
    // we cannot write if the new head would equal tail.
    // Since we read 16 bytes (tail=16) and want to write 16 bytes (needed=16),
    // writing at 0 would make head=16, so head==tail.
    // Thus, we need to read one more item to free up enough space to avoid ambiguity.
    ASSERT_TRUE(queue.tryRead(data, size));
    ASSERT_EQ(2, data[0]);
    
    // Now we can write 5
    ASSERT_TRUE(queue.tryWrite(&val, 1));
}

TEST(LockFreeQueueTest, MultiThreaded) {
    LockFreeRingBuffer<65536> queue; // 64KB
    const int ITERATIONS = 1000;
    
    std::thread producer([&]() {
        for (int i = 0; i < ITERATIONS; ++i) {
            uint8_t val = static_cast<uint8_t>(i % 255);
            while (!queue.tryWrite(&val, 1)) {
                std::this_thread::yield(); // Wait if full
            }
        }
    });

    std::thread consumer([&]() {
        int count = 0;
        uint8_t data[128];
        size_t size;
        
        while (count < ITERATIONS) {
            if (queue.tryRead(data, size)) {
                count++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
}
