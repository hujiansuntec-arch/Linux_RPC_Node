// Simple 2-process integrity test
// Verifies 100% message delivery between two processes

#include "Node.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <set>
#include <mutex>
#include <iomanip>

using namespace librpc;

std::atomic<int> g_sent{0};
std::atomic<int> g_received{0};
std::set<int> g_received_ids;
std::mutex g_mutex;

void printHeader(const std::string& name) {
    std::cout << "\n╔════════════════════════════════════════╗\n";
    std::cout << "║  Simple Integrity Test - " << std::left << std::setw(14) << name << "║\n";
    std::cout << "╚════════════════════════════════════════╝\n\n";
}

// Sender: sends 200 numbered messages
void sender() {
    printHeader("Sender");
    
    auto node = createNode("Sender-Node");
    
    std::cout << "[Sender] Waiting 2 seconds for receiver to subscribe...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    std::cout << "[Sender] Starting to send 200 messages...\n";
    
    for (int i = 1; i <= 200; i++) {
        std::string msg = "MSG-" + std::to_string(i);
        node->broadcast("test", "data", msg);
        g_sent++;
        
        if (i % 50 == 0) {
            std::cout << "[Sender] Sent " << i << "/200 messages\n";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    std::cout << "[Sender] ✅ All 200 messages sent!\n";
    std::cout << "[Sender] Waiting 3 seconds for receiver to process...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

// Receiver: receives and validates all messages
void receiver() {
    printHeader("Receiver");
    
    auto node = createNode("Receiver-Node");
    
    // Subscribe with message tracking
    node->subscribe("test", {"data"}, 
        [](const std::string&, const std::string&, const uint8_t* data, size_t len) {
            std::string msg(reinterpret_cast<const char*>(data), len);
            
            // Extract message ID
            if (msg.find("MSG-") == 0) {
                try {
                    int id = std::stoi(msg.substr(4));
                    
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_received_ids.insert(id);
                    g_received++;
                    
                    if (g_received.load() % 50 == 0 || g_received.load() <= 5) {
                        std::cout << "[Receiver] 📩 Received: " << msg 
                                  << " (Total: " << g_received.load() << ")\n";
                    }
                } catch (...) {
                    // Skip invalid messages
                }
            }
        });
    
    std::cout << "[Receiver] ✅ Subscribed to test/data\n";
    std::cout << "[Receiver] Waiting for messages...\n";
    
    // Wait for messages
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // Report results
    std::cout << "\n╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║              Receiver Final Report                     ║\n";
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Total received:  " << std::right << std::setw(30) << g_received.load() << "  ║\n";
    
    int unique_count;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        unique_count = g_received_ids.size();
    }
    std::cout << "║  Unique messages: " << std::right << std::setw(30) << unique_count << "  ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";
    
    // Check for missing messages
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<int> missing;
    for (int i = 1; i <= 200; i++) {
        if (g_received_ids.find(i) == g_received_ids.end()) {
            missing.push_back(i);
        }
    }
    
    if (missing.empty()) {
        std::cout << "\n✅ SUCCESS: All 200 messages received!\n";
        std::cout << "🎯 Message delivery rate: 100%\n";
    } else {
        std::cout << "\n⚠️  INCOMPLETE: " << missing.size() << " messages missing\n";
        std::cout << "Missing IDs: ";
        for (size_t i = 0; i < std::min(missing.size(), size_t(10)); i++) {
            std::cout << missing[i] << " ";
        }
        if (missing.size() > 10) {
            std::cout << "... (+" << (missing.size() - 10) << " more)";
        }
        std::cout << "\n";
        std::cout << "📊 Message delivery rate: " 
                  << std::fixed << std::setprecision(1)
                  << (unique_count * 100.0 / 200.0) << "%\n";
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <sender|receiver>\n";
        return 1;
    }
    
    std::string role = argv[1];
    
    try {
        if (role == "sender") {
            sender();
        } else if (role == "receiver") {
            receiver();
        } else {
            std::cerr << "Invalid role: " << role << "\n";
            std::cerr << "Valid options: sender, receiver\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
