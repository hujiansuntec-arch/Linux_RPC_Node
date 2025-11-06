// Test program for in-process full-duplex communication
// Verifies that multiple nodes within the same process can send and receive messages simultaneously

#include "Node.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <set>
#include <mutex>
#include <iomanip>
#include <sstream>

using namespace librpc;

// Statistics for each node
struct NodeStats {
    std::string name;
    std::atomic<int> sent{0};
    std::atomic<int> received{0};
    std::set<std::string> received_messages;
    mutable std::mutex mutex;
    
    void recordMessage(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex);
        received_messages.insert(msg);
        received++;
    }
    
    int getUniqueCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return received_messages.size();
    }
    
    void printStats() const {
        std::cout << "  [" << std::left << std::setw(8) << name << "] "
                  << "Sent: " << std::right << std::setw(3) << sent.load()
                  << " | Received: " << std::setw(3) << received.load()
                  << " (Unique: " << std::setw(3) << getUniqueCount() << ")"
                  << std::endl;
    }
};

NodeStats statsA, statsB, statsC;

void printHeader() {
    std::cout << "\n╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║     In-Process Full-Duplex Communication Test         ║\n";
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Scenario: 3 nodes in same process                    ║\n";
    std::cout << "║  - NodeA: Sends temperature, receives pressure        ║\n";
    std::cout << "║  - NodeB: Sends pressure, receives humidity           ║\n";
    std::cout << "║  - NodeC: Sends humidity, receives temperature        ║\n";
    std::cout << "║  Each pair communicates in full-duplex mode           ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";
}

void printSummary() {
    std::cout << "\n╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║              Final Statistics Summary                 ║\n";
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    statsA.printStats();
    statsB.printStats();
    statsC.printStats();
    std::cout << "╚════════════════════════════════════════════════════════╝\n";
}

void verifyFullDuplex() {
    std::cout << "\n╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║              Full-Duplex Verification                 ║\n";
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    
    bool all_passed = true;
    
    // NodeA should have sent 50 and received 50
    bool nodeA_ok = (statsA.sent.load() == 50) && (statsA.received.load() == 50);
    std::cout << "║  NodeA: Sent " << statsA.sent.load() << "/50, Received " 
              << statsA.received.load() << "/50 " 
              << (nodeA_ok ? "✅" : "❌") << std::string(18, ' ') << "║\n";
    all_passed &= nodeA_ok;
    
    // NodeB should have sent 50 and received 50
    bool nodeB_ok = (statsB.sent.load() == 50) && (statsB.received.load() == 50);
    std::cout << "║  NodeB: Sent " << statsB.sent.load() << "/50, Received " 
              << statsB.received.load() << "/50 " 
              << (nodeB_ok ? "✅" : "❌") << std::string(18, ' ') << "║\n";
    all_passed &= nodeB_ok;
    
    // NodeC should have sent 50 and received 50
    bool nodeC_ok = (statsC.sent.load() == 50) && (statsC.received.load() == 50);
    std::cout << "║  NodeC: Sent " << statsC.sent.load() << "/50, Received " 
              << statsC.received.load() << "/50 " 
              << (nodeC_ok ? "✅" : "❌") << std::string(18, ' ') << "║\n";
    all_passed &= nodeC_ok;
    
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    
    // Check for self-message reception (should be 0)
    {
        std::lock_guard<std::mutex> lockA(statsA.mutex);
        int self_msgs = 0;
        for (const auto& msg : statsA.received_messages) {
            if (msg.find("TEMP-A-") != std::string::npos) {
                self_msgs++;
            }
        }
        bool no_self = (self_msgs == 0);
        std::cout << "║  NodeA self-messages: " << self_msgs << " " 
                  << (no_self ? "✅" : "❌") << std::string(29, ' ') << "║\n";
        all_passed &= no_self;
    }
    
    {
        std::lock_guard<std::mutex> lockB(statsB.mutex);
        int self_msgs = 0;
        for (const auto& msg : statsB.received_messages) {
            if (msg.find("PRESS-B-") != std::string::npos) {
                self_msgs++;
            }
        }
        bool no_self = (self_msgs == 0);
        std::cout << "║  NodeB self-messages: " << self_msgs << " " 
                  << (no_self ? "✅" : "❌") << std::string(29, ' ') << "║\n";
        all_passed &= no_self;
    }
    
    {
        std::lock_guard<std::mutex> lockC(statsC.mutex);
        int self_msgs = 0;
        for (const auto& msg : statsC.received_messages) {
            if (msg.find("HUMID-C-") != std::string::npos) {
                self_msgs++;
            }
        }
        bool no_self = (self_msgs == 0);
        std::cout << "║  NodeC self-messages: " << self_msgs << " " 
                  << (no_self ? "✅" : "❌") << std::string(29, ' ') << "║\n";
        all_passed &= no_self;
    }
    
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    
    if (all_passed) {
        std::cout << "║                                                        ║\n";
        std::cout << "║  🎉 SUCCESS: Full-duplex communication verified!      ║\n";
        std::cout << "║  ✅ All nodes sent and received simultaneously         ║\n";
        std::cout << "║  ✅ No self-message reception                          ║\n";
        std::cout << "║  ✅ 100% message delivery rate                         ║\n";
        std::cout << "║                                                        ║\n";
    } else {
        std::cout << "║  ❌ FAILED: Some verification checks failed           ║\n";
    }
    
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";
}

int main() {
    printHeader();
    
    // Initialize stats
    statsA.name = "NodeA";
    statsB.name = "NodeB";
    statsC.name = "NodeC";
    
    // Create three nodes in the same process
    std::cout << "Creating three nodes in the same process...\n";
    auto nodeA = createNode("NodeA");
    auto nodeB = createNode("NodeB");
    auto nodeC = createNode("NodeC");
    std::cout << "✅ Created NodeA, NodeB, NodeC\n\n";
    
    // Setup subscriptions
    std::cout << "Setting up subscriptions...\n";
    
    // NodeA receives pressure (from NodeB)
    nodeA->subscribe("data", {"pressure"}, 
        [](const std::string&, const std::string&, const uint8_t* data, size_t len) {
            std::string msg(reinterpret_cast<const char*>(data), len);
            statsA.recordMessage(msg);
            if (statsA.received.load() <= 5 || statsA.received.load() % 10 == 0) {
                std::cout << "  [NodeA] 📩 Received: " << msg << "\n";
            }
        });
    
    // NodeB receives humidity (from NodeC)
    nodeB->subscribe("data", {"humidity"}, 
        [](const std::string&, const std::string&, const uint8_t* data, size_t len) {
            std::string msg(reinterpret_cast<const char*>(data), len);
            statsB.recordMessage(msg);
            if (statsB.received.load() <= 5 || statsB.received.load() % 10 == 0) {
                std::cout << "  [NodeB] 📩 Received: " << msg << "\n";
            }
        });
    
    // NodeC receives temperature (from NodeA)
    nodeC->subscribe("data", {"temperature"}, 
        [](const std::string&, const std::string&, const uint8_t* data, size_t len) {
            std::string msg(reinterpret_cast<const char*>(data), len);
            statsC.recordMessage(msg);
            if (statsC.received.load() <= 5 || statsC.received.load() % 10 == 0) {
                std::cout << "  [NodeC] 📩 Received: " << msg << "\n";
            }
        });
    
    std::cout << "✅ NodeA subscribed to: pressure\n";
    std::cout << "✅ NodeB subscribed to: humidity\n";
    std::cout << "✅ NodeC subscribed to: temperature\n\n";
    
    // Wait a moment for subscription registration
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Starting Full-Duplex Communication            ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";
    
    // Start three threads to send messages simultaneously
    std::atomic<bool> ready{false};
    std::atomic<int> threads_ready{0};
    
    // Thread A: NodeA sends temperature
    std::thread threadA([&]() {
        threads_ready++;
        while (!ready.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        for (int i = 1; i <= 50; i++) {
            std::string msg = "TEMP-A-" + std::to_string(i) + ":" + std::to_string(20 + i) + "C";
            nodeA->broadcast("data", "temperature", msg);
            statsA.sent++;
            
            if (i <= 3 || i % 10 == 0) {
                std::cout << "  [NodeA] 📤 Sent: " << msg << "\n";
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    // Thread B: NodeB sends pressure
    std::thread threadB([&]() {
        threads_ready++;
        while (!ready.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        for (int i = 1; i <= 50; i++) {
            std::string msg = "PRESS-B-" + std::to_string(i) + ":" + std::to_string(1000 + i) + "hPa";
            nodeB->broadcast("data", "pressure", msg);
            statsB.sent++;
            
            if (i <= 3 || i % 10 == 0) {
                std::cout << "  [NodeB] 📤 Sent: " << msg << "\n";
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    // Thread C: NodeC sends humidity
    std::thread threadC([&]() {
        threads_ready++;
        while (!ready.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        for (int i = 1; i <= 50; i++) {
            std::string msg = "HUMID-C-" + std::to_string(i) + ":" + std::to_string(50 + i) + "%";
            nodeC->broadcast("data", "humidity", msg);
            statsC.sent++;
            
            if (i <= 3 || i % 10 == 0) {
                std::cout << "  [NodeC] 📤 Sent: " << msg << "\n";
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    // Wait for all threads to be ready
    while (threads_ready.load() < 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "All sender threads ready, starting simultaneous transmission...\n\n";
    
    // Start all threads simultaneously
    ready.store(true);
    
    // Wait for all threads to complete
    threadA.join();
    threadB.join();
    threadC.join();
    
    std::cout << "\n✅ All sending completed\n";
    
    // Wait a moment for all messages to be delivered
    std::cout << "Waiting for message delivery to complete...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Print results
    printSummary();
    verifyFullDuplex();
    
    // Detailed message verification
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║            Message Completeness Check                 ║\n";
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    
    // Check NodeA received all pressure messages from NodeB
    {
        std::lock_guard<std::mutex> lock(statsA.mutex);
        std::vector<int> missing;
        for (int i = 1; i <= 50; i++) {
            std::string expected = "PRESS-B-" + std::to_string(i) + ":" + std::to_string(1000 + i) + "hPa";
            if (statsA.received_messages.find(expected) == statsA.received_messages.end()) {
                missing.push_back(i);
            }
        }
        
        if (missing.empty()) {
            std::cout << "║  NodeA: ✅ Received all 50 pressure messages          ║\n";
        } else {
            std::cout << "║  NodeA: ❌ Missing " << missing.size() << " pressure messages";
            std::cout << std::string(52 - 35 - std::to_string(missing.size()).length(), ' ') << "║\n";
        }
    }
    
    // Check NodeB received all humidity messages from NodeC
    {
        std::lock_guard<std::mutex> lock(statsB.mutex);
        std::vector<int> missing;
        for (int i = 1; i <= 50; i++) {
            std::string expected = "HUMID-C-" + std::to_string(i) + ":" + std::to_string(50 + i) + "%";
            if (statsB.received_messages.find(expected) == statsB.received_messages.end()) {
                missing.push_back(i);
            }
        }
        
        if (missing.empty()) {
            std::cout << "║  NodeB: ✅ Received all 50 humidity messages          ║\n";
        } else {
            std::cout << "║  NodeB: ❌ Missing " << missing.size() << " humidity messages";
            std::cout << std::string(52 - 35 - std::to_string(missing.size()).length(), ' ') << "║\n";
        }
    }
    
    // Check NodeC received all temperature messages from NodeA
    {
        std::lock_guard<std::mutex> lock(statsC.mutex);
        std::vector<int> missing;
        for (int i = 1; i <= 50; i++) {
            std::string expected = "TEMP-A-" + std::to_string(i) + ":" + std::to_string(20 + i) + "C";
            if (statsC.received_messages.find(expected) == statsC.received_messages.end()) {
                missing.push_back(i);
            }
        }
        
        if (missing.empty()) {
            std::cout << "║  NodeC: ✅ Received all 50 temperature messages       ║\n";
        } else {
            std::cout << "║  NodeC: ❌ Missing " << missing.size() << " temperature messages";
            std::cout << std::string(52 - 40 - std::to_string(missing.size()).length(), ' ') << "║\n";
        }
    }
    
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";
    
    std::cout << "🎯 In-process full-duplex communication test complete!\n\n";
    
    return 0;
}
