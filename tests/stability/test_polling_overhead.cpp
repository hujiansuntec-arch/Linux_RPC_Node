/**
 * @file test_polling_overhead.cpp
 * @brief 测试轮询开销 - 只订阅不发布，观察纯轮询的CPU占用
 */

#include "nexus/core/Node.h"
#include "nexus/registry/GlobalRegistry.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <signal.h>

using namespace librpc;
using namespace std::chrono;

static std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down...\n";
    g_running = false;
}

void onMessage(const std::string& group, 
               const std::string& topic,
               const uint8_t* data, 
               size_t len) {
    // 空回调，不做任何处理
    (void)group;
    (void)topic;
    (void)data;
    (void)len;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <process_id> [duration_seconds]\n";
        return 1;
    }
    
    int process_id = std::atoi(argv[1]);
    int duration = argc >= 3 ? std::atoi(argv[2]) : 60;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║      Polling Overhead Test - Process " << process_id << "               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "这个测试只订阅，不发布任何消息\n";
    std::cout << "用于观察共享内存轮询的纯CPU开销\n\n";
    
    // 创建节点
    std::string node_name = "polling_test_" + std::to_string(process_id);
    std::shared_ptr<Node> node = createNode(node_name);
    if (!node) {
        std::cerr << "Failed to create node\n";
        return 1;
    }
    
    std::cout << "Node created: " << node_name << "\n";
    
    // 订阅6个topics（但没有人发布，所以不会收到消息）
    std::vector<std::string> topics = {
        "sensor_data", "control_cmd", "status_health",
        "sync_data", "event_trigger", "metric_value"
    };
    
    node->subscribe("polling_inprocess", topics, onMessage);
    node->subscribe("polling_cross", topics, onMessage);
    
    std::cout << "Subscribed to 12 topics (6 inprocess + 6 cross)\n";
    std::cout << "\n🔍 观察CPU占用（应该是纯轮询开销，因为没有消息）\n";
    std::cout << "运行 " << duration << " 秒...\n\n";
    
    // 等待指定时间
    auto end_time = steady_clock::now() + seconds(duration);
    while (g_running && steady_clock::now() < end_time) {
        std::this_thread::sleep_for(seconds(1));
    }
    
    std::cout << "\n✓ 测试完成\n";
    
    return 0;
}
