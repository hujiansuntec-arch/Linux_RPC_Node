// Multi-process communication integrity test
// Tests message delivery completeness and accuracy across multiple processes

#include "Node.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <set>
#include <mutex>
#include <sstream>
#include <iomanip>

using namespace librpc;

// Message counters and trackers
struct ProcessStats {
    std::atomic<int> sent{0};
    std::atomic<int> received{0};
    std::set<std::string> received_messages;
    mutable std::mutex messages_mutex;
    
    void recordMessage(const std::string& msg) {
        std::lock_guard<std::mutex> lock(messages_mutex);
        received_messages.insert(msg);
        received++;
    }
    
    int getUniqueCount() const {
        std::lock_guard<std::mutex> lock(messages_mutex);
        return received_messages.size();
    }
    
    bool hasMessage(const std::string& msg) const {
        std::lock_guard<std::mutex> lock(messages_mutex);
        return received_messages.find(msg) != received_messages.end();
    }
};

ProcessStats g_stats;

void printHeader(const std::string& process_name, const std::string& role) {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Multi-Process Communication Integrity Test           ║\n";
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Process: " << std::left << std::setw(42) << process_name << "║\n";
    std::cout << "║  Role:    " << std::left << std::setw(42) << role << "║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";
    std::cout << std::endl;
}

void printStats(const std::string& process_name) {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║  " << std::left << std::setw(51) << (process_name + " - Final Statistics") << "║\n";
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Messages Sent:     " << std::right << std::setw(30) << g_stats.sent.load() << "  ║\n";
    std::cout << "║  Messages Received: " << std::right << std::setw(30) << g_stats.received.load() << "  ║\n";
    std::cout << "║  Unique Messages:   " << std::right << std::setw(30) << g_stats.getUniqueCount() << "  ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";
    std::cout << std::endl;
}

// Process A: Temperature sensor (sends 100 messages)
void processA() {
    printHeader("Process-A", "Temperature Sender");
    
    auto node1 = createNode("ProcessA-Node1");
    auto node2 = createNode("ProcessA-Node2");
    
    std::cout << "[Process-A] Nodes created, waiting for other processes...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    std::cout << "[Process-A] Starting to send temperature data...\n";
    
    // Send 100 temperature messages
    for (int i = 1; i <= 100; i++) {
        std::stringstream ss;
        ss << "Temp-A-" << std::setfill('0') << std::setw(3) << i << ":" << (20 + i % 30) << "C";
        std::string message = ss.str();
        
        node1->broadcast("sensor", "temperature", message);
        g_stats.sent++;
        
        if (i % 20 == 0) {
            std::cout << "[Process-A] Sent " << i << "/100 messages\n";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "[Process-A] Finished sending, waiting for responses...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    printStats("Process-A");
}

// Process B: Pressure sensor (sends 100 messages) + Temperature subscriber
void processB() {
    printHeader("Process-B", "Pressure Sender + Temperature Receiver");
    
    auto node1 = createNode("ProcessB-Node1");
    auto node2 = createNode("ProcessB-Node2");
    
    // Subscribe to temperature from Process A and Process C
    node1->subscribe("sensor", {"temperature"}, 
        [](const std::string& group, const std::string& topic, 
           const uint8_t* data, size_t len) {
            std::string message(reinterpret_cast<const char*>(data), len);
            g_stats.recordMessage(message);
            if (g_stats.received.load() % 20 == 0 || g_stats.received.load() <= 10) {
                std::cout << "[Process-B] 📩 Received: " << message << "\n";
            }
        });
    
    std::cout << "[Process-B] Subscribed to temperature, waiting for other processes...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    std::cout << "[Process-B] Starting to send pressure data...\n";
    
    // Send 100 pressure messages
    for (int i = 1; i <= 100; i++) {
        std::stringstream ss;
        ss << "Press-B-" << std::setfill('0') << std::setw(3) << i << ":" << (1000 + i % 50) << "hPa";
        std::string message = ss.str();
        
        node2->broadcast("sensor", "pressure", message);
        g_stats.sent++;
        
        if (i % 20 == 0) {
            std::cout << "[Process-B] Sent " << i << "/100 messages\n";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "[Process-B] Finished sending, waiting for final messages...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    printStats("Process-B");
}

// Process C: Humidity sensor (sends 50 messages) + Temperature + Pressure subscriber
void processC() {
    printHeader("Process-C", "Humidity Sender + Temp/Pressure Receiver");
    
    auto node1 = createNode("ProcessC-Node1");
    auto node2 = createNode("ProcessC-Node2");
    
    // Subscribe to temperature from Process A
    node1->subscribe("sensor", {"temperature"}, 
        [](const std::string& group, const std::string& topic, 
           const uint8_t* data, size_t len) {
            std::string message(reinterpret_cast<const char*>(data), len);
            g_stats.recordMessage("temp:" + message);
            if (g_stats.received.load() % 20 == 0 || g_stats.received.load() <= 10) {
                std::cout << "[Process-C] 📩 Temp: " << message << "\n";
            }
        });
    
    // Subscribe to pressure from Process B
    node2->subscribe("sensor", {"pressure"}, 
        [](const std::string& group, const std::string& topic, 
           const uint8_t* data, size_t len) {
            std::string message(reinterpret_cast<const char*>(data), len);
            g_stats.recordMessage("press:" + message);
            if (g_stats.received.load() % 20 == 0 || g_stats.received.load() <= 10) {
                std::cout << "[Process-C] 📩 Press: " << message << "\n";
            }
        });
    
    std::cout << "[Process-C] Subscribed to temp/pressure, waiting for other processes...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    std::cout << "[Process-C] Starting to send humidity data...\n";
    
    // Send 50 humidity messages
    for (int i = 1; i <= 50; i++) {
        std::stringstream ss;
        ss << "Humid-C-" << std::setfill('0') << std::setw(3) << i << ":" << (50 + i % 40) << "%";
        std::string message = ss.str();
        
        node1->broadcast("sensor", "humidity", message);
        g_stats.sent++;
        
        if (i % 10 == 0) {
            std::cout << "[Process-C] Sent " << i << "/50 messages\n";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    std::cout << "[Process-C] Finished sending, waiting for final messages...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    printStats("Process-C");
}

// Process D: Monitor (only receives, no sending)
void processD() {
    printHeader("Process-D", "Monitor (Receive All)");
    
    auto node1 = createNode("ProcessD-Node1");
    
    std::atomic<int> temp_count{0};
    std::atomic<int> press_count{0};
    std::atomic<int> humid_count{0};
    
    // Subscribe to all topics
    node1->subscribe("sensor", {"temperature"}, 
        [&temp_count](const std::string& group, const std::string& topic, 
           const uint8_t* data, size_t len) {
            std::string message(reinterpret_cast<const char*>(data), len);
            g_stats.recordMessage("temp:" + message);
            temp_count++;
            if (temp_count.load() <= 5 || temp_count.load() % 20 == 0) {
                std::cout << "[Process-D] 🌡️  Temp: " << message << "\n";
            }
        });
    
    node1->subscribe("sensor", {"pressure"}, 
        [&press_count](const std::string& group, const std::string& topic, 
           const uint8_t* data, size_t len) {
            std::string message(reinterpret_cast<const char*>(data), len);
            g_stats.recordMessage("press:" + message);
            press_count++;
            if (press_count.load() <= 5 || press_count.load() % 20 == 0) {
                std::cout << "[Process-D] 📊 Press: " << message << "\n";
            }
        });
    
    node1->subscribe("sensor", {"humidity"}, 
        [&humid_count](const std::string& group, const std::string& topic, 
           const uint8_t* data, size_t len) {
            std::string message(reinterpret_cast<const char*>(data), len);
            g_stats.recordMessage("humid:" + message);
            humid_count++;
            if (humid_count.load() <= 5 || humid_count.load() % 10 == 0) {
                std::cout << "[Process-D] 💧 Humid: " << message << "\n";
            }
        });
    
    std::cout << "[Process-D] Subscribed to all topics, monitoring...\n";
    
    // Monitor for 20 seconds
    for (int i = 0; i < 20; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (i % 5 == 4) {
            std::cout << "[Process-D] Monitoring... (Temp:" << temp_count.load() 
                      << ", Press:" << press_count.load() 
                      << ", Humid:" << humid_count.load() << ")\n";
        }
    }
    
    std::cout << "\n[Process-D] Final counts:\n";
    std::cout << "  - Temperature: " << temp_count.load() << " messages\n";
    std::cout << "  - Pressure:    " << press_count.load() << " messages\n";
    std::cout << "  - Humidity:    " << humid_count.load() << " messages\n";
    
    printStats("Process-D");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <process_type>\n";
        std::cout << "  process_type: a, b, c, d\n";
        std::cout << "\nTest scenario:\n";
        std::cout << "  Process A: Sends 100 temperature messages\n";
        std::cout << "  Process B: Sends 100 pressure messages, receives temperature\n";
        std::cout << "  Process C: Sends 50 humidity messages, receives temp + pressure\n";
        std::cout << "  Process D: Monitor only, receives all messages\n";
        return 1;
    }
    
    std::string process_type = argv[1];
    
    try {
        if (process_type == "a") {
            processA();
        } else if (process_type == "b") {
            processB();
        } else if (process_type == "c") {
            processC();
        } else if (process_type == "d") {
            processD();
        } else {
            std::cerr << "Invalid process type: " << process_type << "\n";
            std::cerr << "Valid options: a, b, c, d\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
