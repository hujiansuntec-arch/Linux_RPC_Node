// Test bidirectional communication between processes
// Each process both sends and receives messages simultaneously

#include "Node.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>

using namespace librpc;

std::atomic<int> sent_count{0};
std::atomic<int> received_count{0};

std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%H:%M:%S")
       << "." << std::setfill('0') << std::setw(3) << ms;
    return ss.str();
}

void runProcessA() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "      Process A - Bidirectional Test" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    auto nodeA = createNode("ProcessA-Node");
    
    // Subscribe to messages from Process B
    nodeA->subscribe("comm", {"data"},
        [](const std::string& /*group*/, const std::string& /*topic*/,
           const uint8_t* data, size_t size) {
            received_count++;
            std::string msg(reinterpret_cast<const char*>(data), size);
            std::cout << "[" << getTimestamp() << "][ProcessA] 📩 Received: " 
                      << msg << " (total: " << received_count << ")" << std::endl;
        });
    
    std::cout << "[ProcessA] Subscribed to 'comm/data'" << std::endl;
    std::cout << "[ProcessA] Waiting 2 seconds for Process B..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    std::cout << "\n[ProcessA] Starting bidirectional communication..." << std::endl;
    
    // Start sending and receiving simultaneously
    std::thread send_thread([&nodeA]() {
        for (int i = 1; i <= 20; i++) {
            std::string msg = "A-Message-" + std::to_string(i);
            nodeA->broadcast("comm", "data", msg);
            sent_count++;
            
            if (i % 5 == 0) {
                std::cout << "[ProcessA] Sent " << sent_count << " messages" << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    });
    
    // Wait for messages while sending
    send_thread.join();
    
    // Wait a bit more to receive remaining messages
    std::cout << "\n[ProcessA] Waiting for remaining messages..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "         Process A Complete" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Sent:     " << sent_count << std::endl;
    std::cout << "Received: " << received_count << std::endl;
    std::cout << "Status:   " << (received_count > 0 ? "✓ SUCCESS" : "✗ FAILED") << std::endl;
}

void runProcessB() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "      Process B - Bidirectional Test" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    auto nodeB = createNode("ProcessB-Node");
    
    // Subscribe to messages from Process A
    nodeB->subscribe("comm", {"data"},
        [](const std::string& /*group*/, const std::string& /*topic*/,
           const uint8_t* data, size_t size) {
            received_count++;
            std::string msg(reinterpret_cast<const char*>(data), size);
            std::cout << "[" << getTimestamp() << "][ProcessB] 📩 Received: " 
                      << msg << " (total: " << received_count << ")" << std::endl;
        });
    
    std::cout << "[ProcessB] Subscribed to 'comm/data'" << std::endl;
    std::cout << "[ProcessB] Waiting 1 second for subscription sync..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    std::cout << "\n[ProcessB] Starting bidirectional communication..." << std::endl;
    
    // Start sending and receiving simultaneously
    std::thread send_thread([&nodeB]() {
        for (int i = 1; i <= 20; i++) {
            std::string msg = "B-Message-" + std::to_string(i);
            nodeB->broadcast("comm", "data", msg);
            sent_count++;
            
            if (i % 5 == 0) {
                std::cout << "[ProcessB] Sent " << sent_count << " messages" << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    });
    
    // Wait for messages while sending
    send_thread.join();
    
    // Wait a bit more to receive remaining messages
    std::cout << "\n[ProcessB] Waiting for remaining messages..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "         Process B Complete" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Sent:     " << sent_count << std::endl;
    std::cout << "Received: " << received_count << std::endl;
    std::cout << "Status:   " << (received_count > 0 ? "✓ SUCCESS" : "✗ FAILED") << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   Bidirectional Communication Test    ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    if (argc < 2) {
        std::cout << "\nUsage: " << argv[0] << " [process]" << std::endl;
        std::cout << "\nProcess:" << std::endl;
        std::cout << "  a  - Process A (start first)" << std::endl;
        std::cout << "  b  - Process B (start second)" << std::endl;
        std::cout << "\nExample:" << std::endl;
        std::cout << "  Terminal 1: " << argv[0] << " a" << std::endl;
        std::cout << "  Terminal 2: " << argv[0] << " b" << std::endl;
        return 1;
    }
    
    std::string process(argv[1]);
    
    if (process == "a") {
        runProcessA();
    } else if (process == "b") {
        runProcessB();
    } else {
        std::cerr << "Invalid process: " << process << std::endl;
        std::cerr << "Valid options: a, b" << std::endl;
        return 1;
    }
    
    return 0;
}
