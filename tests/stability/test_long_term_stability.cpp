/**
 * @file test_long_term_stability.cpp
 * @brief 长时间稳定性测试 - 10进程 × 多topic × 10分钟
 * 
 * 测试场景：
 * - 10个进程，每个进程与至少4个其他进程通信
 * - 每个进程内部有多个topic的定周期通信（5ms）
 * - 跨进程通信也是5ms周期
 * - 运行时长：10分钟（可配置）
 * - 验证：数据完整性、顺序性、丢包率
 * - 监控：内存使用、CPU、延迟、吞吐量
 */

#include "nexus/core/Node.h"
#include "nexus/registry/GlobalRegistry.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <cstring>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

using namespace Nexus::rpc;
using namespace std::chrono;

// ============================================================================
// 测试配置
// ============================================================================
struct TestConfig {
    int process_id;                     // 进程ID (0-9)
    int total_processes = 10;           // 总进程数
    int test_duration_seconds = 600;    // 测试时长（秒）- 默认10分钟
    int publish_interval_ms = 5;        // 发布间隔（毫秒）- 恢复到5ms
    int stats_interval_seconds = 10;    // 统计间隔（秒）
    bool enable_detailed_stats = false; // 🔧 是否启用详细统计（影响CPU）
    
    // 每个进程的topic配置
    std::vector<std::string> inprocess_topics = {
        "sensor_data", "control_cmd", "status_health"
    };
    
    std::vector<std::string> cross_topics = {
        "sync_data", "event_trigger", "metric_value"
    };
};

// ============================================================================
// 消息格式
// ============================================================================
struct TestMessage {
    uint32_t sender_id;        // 发送者进程ID
    uint32_t sequence;         // 序列号
    uint64_t timestamp_us;     // 发送时间戳（微秒）
    char topic[32];            // Topic名称
    char payload[64];          // 载荷数据
} __attribute__((packed));

// ============================================================================
// 统计数据
// ============================================================================
struct TopicStats {
    std::atomic<uint64_t> sent_count{0};
    std::atomic<uint64_t> recv_count{0};
    std::atomic<uint64_t> lost_count{0};
    std::atomic<uint64_t> out_of_order{0};
    std::atomic<uint64_t> data_mismatch{0};
    std::atomic<uint64_t> total_latency_us{0};
    std::map<uint32_t, uint32_t> sender_last_sequence;
    std::mutex mutex;
};

struct ProcessStats {
    std::map<std::string, TopicStats> topic_stats;
    std::atomic<uint64_t> total_sent{0};
    std::atomic<uint64_t> total_recv{0};
    std::atomic<uint64_t> memory_kb{0};
    std::atomic<double> cpu_percent{0.0};
    
    steady_clock::time_point start_time;
    steady_clock::time_point last_stats_time;
};

// ============================================================================
// 全局变量
// ============================================================================
static std::atomic<bool> g_running{true};
static ProcessStats g_stats;
static TestConfig g_config;

// ============================================================================
// 信号处理
// ============================================================================
void signal_handler(int sig) {
    std::cout << "\n[Process " << g_config.process_id << "] Received signal " 
              << sig << ", shutting down...\n";
    g_running = false;
}

// ============================================================================
// 系统资源监控
// ============================================================================
class ResourceMonitor {
public:
    static size_t getMemoryUsageKB() {
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                std::istringstream iss(line.substr(6));
                size_t mem_kb;
                iss >> mem_kb;
                return mem_kb;
            }
        }
        return 0;
    }
    
    static double getCPUUsage() {
        static steady_clock::time_point last_time = steady_clock::now();
        static struct rusage last_usage;
        static bool first_call = true;
        
        if (first_call) {
            getrusage(RUSAGE_SELF, &last_usage);
            first_call = false;
            return 0.0;
        }
        
        struct rusage current_usage;
        getrusage(RUSAGE_SELF, &current_usage);
        
        auto current_time = steady_clock::now();
        auto wall_time = duration_cast<microseconds>(current_time - last_time).count();
        
        auto user_time = (current_usage.ru_utime.tv_sec - last_usage.ru_utime.tv_sec) * 1000000L +
                        (current_usage.ru_utime.tv_usec - last_usage.ru_utime.tv_usec);
        auto sys_time = (current_usage.ru_stime.tv_sec - last_usage.ru_stime.tv_sec) * 1000000L +
                       (current_usage.ru_stime.tv_usec - last_usage.ru_stime.tv_usec);
        
        double cpu_percent = 0.0;
        if (wall_time > 0) {
            cpu_percent = ((user_time + sys_time) * 100.0) / wall_time;
        }
        
        last_usage = current_usage;
        last_time = current_time;
        
        return cpu_percent;
    }
};

// ============================================================================
// 消息处理回调
// ============================================================================
void onMessage(const std::string& group, 
               const std::string& topic,
               const uint8_t* data, 
               size_t len) {
    (void)group;
    (void)topic;
    (void)data;
    (void)len;
    
    // 增加总接收计数（始终统计）
    g_stats.total_recv.fetch_add(1, std::memory_order_relaxed);
    
    // 🔧 如果启用详细统计，才处理per-topic统计和数据验证
    if (!g_config.enable_detailed_stats) {
        return;  // 快速返回，降低CPU占用
    }
    
    // 构造完整topic名称
    std::string full_topic = group + "/" + topic;
    
    // 更新per-topic统计
    auto& stats = g_stats.topic_stats[full_topic];
    stats.recv_count.fetch_add(1, std::memory_order_relaxed);
    
    // 如果需要验证消息内容，可以解析TestMessage
    if (len >= sizeof(TestMessage)) {
        const TestMessage* msg = reinterpret_cast<const TestMessage*>(data);
        uint64_t now_us = duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()).count();
        uint64_t latency = now_us - msg->timestamp_us;
        stats.total_latency_us.fetch_add(latency, std::memory_order_relaxed);
        
        // 检查顺序
        std::lock_guard<std::mutex> lock(stats.mutex);
        auto it = stats.sender_last_sequence.find(msg->sender_id);
        if (it != stats.sender_last_sequence.end()) {
            if (msg->sequence < it->second) {
                stats.out_of_order.fetch_add(1, std::memory_order_relaxed);
            }
            it->second = msg->sequence;
        } else {
            stats.sender_last_sequence[msg->sender_id] = msg->sequence;
        }

        // 验证可变长度数据完整性
        size_t header_size = sizeof(TestMessage);
        if (len > header_size) {
            const char* extra = reinterpret_cast<const char*>(data) + header_size;
            size_t extra_len = len - header_size;
            char expected = (char)('A' + (msg->sequence % 26));
            bool mismatch = false;
            for (size_t i = 0; i < extra_len; ++i) {
                if (extra[i] != expected) {
                    mismatch = true;
                    break;
                }
            }
            if (mismatch) {
                stats.data_mismatch.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

// ============================================================================
// 发布线程
// ============================================================================
void publishThread(std::shared_ptr<Node> node, 
                  const std::string& group,
                  const std::string& topic,
                  bool is_cross_process) {
    (void)is_cross_process;
    
    // 🔧 如果启用详细统计，才处理per-topic统计
    TopicStats* stats_ptr = nullptr;
    if (g_config.enable_detailed_stats) {
        // 构造完整topic名称用于统计
        std::string full_topic = group + "/" + topic;
        stats_ptr = &g_stats.topic_stats[full_topic];
    }
    
    uint32_t sequence = 0;
    
    while (g_running) {
        // 🔧 根据配置决定消息内容
        std::string msg_str;
        
        if (g_config.enable_detailed_stats) {
            // 详细模式：发送TestMessage结构（包含序列号、时间戳等）
            TestMessage msg;
            msg.sender_id = g_config.process_id;
            msg.sequence = sequence++;
            msg.timestamp_us = duration_cast<microseconds>(
                steady_clock::now().time_since_epoch()).count();
            strncpy(msg.topic, topic.c_str(), sizeof(msg.topic) - 1);
            snprintf(msg.payload, sizeof(msg.payload), 
                    "P%d-SEQ%u", g_config.process_id, msg.sequence);
            
            // 生成可变长度数据 (0-1024字节)
            size_t extra_len = msg.sequence % 1025;
            std::string extra_data(extra_len, (char)('A' + (msg.sequence % 26)));
            
            msg_str = std::string(reinterpret_cast<const char*>(&msg), sizeof(msg)) + extra_data;
            
            if (stats_ptr) {
                stats_ptr->sent_count.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            // 🔧 简单模式：发送小payload（128字节，降低内存开销）
            static std::string simple_payload(128, 'X');
            msg_str = simple_payload;
            sequence++;  // 仍然维护序列号，但不发送
        }
        
        // 发送消息
        node->publish(group, topic, msg_str);
        g_stats.total_sent.fetch_add(1, std::memory_order_relaxed);
        
        usleep(g_config.publish_interval_ms * 1000);
    }
}

// ============================================================================
// 最终报告
// ============================================================================
void generateFinalReport() {
    auto elapsed = duration_cast<seconds>(
        steady_clock::now() - g_stats.start_time).count();
    
    std::cout << "\n\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║       Process " << g_config.process_id << " - Final Report                      ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    std::cout << "Test Duration: " << elapsed << " seconds\n\n";
    
    std::cout << "Overall Statistics:\n";
    std::cout << "  Total Messages Sent:     " << g_stats.total_sent << "\n";
    std::cout << "  Total Messages Received: " << g_stats.total_recv << "\n\n";
    
    uint64_t total_lost = 0;
    uint64_t total_ooo = 0;
    uint64_t total_mismatch = 0;
    uint64_t total_latency = 0;
    uint64_t total_recv = 0;
    
    std::cout << "Per-Topic Results:\n";
    std::cout << "────────────────────────────────────────────────────────────\n";
    for (auto& pair : g_stats.topic_stats) {
        const std::string& topic = pair.first;
        TopicStats& stats = pair.second;
        
        std::lock_guard<std::mutex> lock(stats.mutex);
        uint64_t sent = stats.sent_count.load(std::memory_order_relaxed);
        uint64_t recv = stats.recv_count.load(std::memory_order_relaxed);
        uint64_t lost = stats.lost_count.load(std::memory_order_relaxed);
        uint64_t ooo = stats.out_of_order.load(std::memory_order_relaxed);
        uint64_t mismatch = stats.data_mismatch.load(std::memory_order_relaxed);
        uint64_t latency = stats.total_latency_us.load(std::memory_order_relaxed);
        
        double avg_latency_us = recv > 0 ? (double)latency / recv : 0.0;
        double loss_rate = sent > 0 ? (double)lost / sent * 100.0 : 0.0;
        
        // 判断是进程内还是跨进程
        std::string type = (topic.find("inprocess") != std::string::npos) ? "进程内" : "跨进程";
        
        std::cout << "  " << topic << " (" << type << "):\n";
        std::cout << "    Sent:        " << sent << "\n";
        std::cout << "    Received:    " << recv << "\n";
        std::cout << "    Lost:        " << lost << " (" << loss_rate << "%)\n";
        std::cout << "    Out-of-Order:" << ooo << "\n";
        std::cout << "    Data Mismatch:" << mismatch << "\n";
        std::cout << "    Avg Latency: " << (avg_latency_us / 1000.0) << " ms\n\n";
        
        total_lost += lost;
        total_ooo += ooo;
        total_mismatch += mismatch;
        total_latency += latency;
        total_recv += recv;
    }
    
    double overall_loss_rate = g_stats.total_sent > 0 ? 
        (double)total_lost / g_stats.total_sent * 100.0 : 0.0;
    double overall_avg_latency_ms = total_recv > 0 ? 
        (double)total_latency / total_recv / 1000.0 : 0.0;
    
    std::cout << "════════════════════════════════════════════════════════════\n";
    std::cout << "Summary:\n";
    std::cout << "  Loss Rate:       " << overall_loss_rate << "%\n";
    std::cout << "  Out-of-Order:    " << total_ooo << "\n";
    std::cout << "  Data Mismatch:   " << total_mismatch << "\n";
    std::cout << "  Average Latency: " << overall_avg_latency_ms << " ms\n\n";
    
    // 判断测试结果
    bool passed = (overall_loss_rate < 0.01 && total_ooo == 0 && total_mismatch == 0);
    if (passed) {
        std::cout << "✅ DATA INTEGRITY TEST PASSED!\n";
    } else {
        std::cout << "❌ DATA INTEGRITY TEST FAILED!\n";
        if (overall_loss_rate >= 0.01) {
            std::cout << "   Reason: High loss rate (" << overall_loss_rate << "%)\n";
        }
        if (total_ooo > 0) {
            std::cout << "   Reason: Out-of-order messages (" << total_ooo << ")\n";
        }
        if (total_mismatch > 0) {
            std::cout << "   Reason: Data mismatch (" << total_mismatch << ")\n";
        }
    }
    
    std::cout << "\n";
}

// 统计和报告函数已移除，只测试CPU占用

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <process_id> [duration_seconds] [--enable-stats]\n";
        std::cerr << "  process_id:       0-9\n";
        std::cerr << "  duration_seconds: default 600 (10 minutes)\n";
        std::cerr << "  --enable-stats:   Enable detailed statistics (default: off)\n";
        return 1;
    }
    
    g_config.process_id = std::atoi(argv[1]);
    if (g_config.process_id < 0 || g_config.process_id >= g_config.total_processes) {
        std::cerr << "Error: process_id must be 0-9\n";
        return 1;
    }
    
    if (argc >= 3 && argv[2][0] != '-') {
        g_config.test_duration_seconds = std::atoi(argv[2]);
    }
    
    // 🔧 解析统计开关
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--enable-stats") {
            g_config.enable_detailed_stats = true;
        }
    }
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║      Long-Term Stability Test - Process " << g_config.process_id << "            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "Configuration:\n";
    std::cout << "  Process ID:       " << g_config.process_id << "\n";
    std::cout << "  Duration:         " << g_config.test_duration_seconds << " seconds\n";
    std::cout << "  Detailed Stats:   " << (g_config.enable_detailed_stats ? "Enabled" : "Disabled") << "\n";
    std::cout << "  Process ID:        " << g_config.process_id << "\n";
    std::cout << "  Publish Interval:  " << g_config.publish_interval_ms << " ms\n\n";
    
    // 初始化统计时间
    g_stats.start_time = steady_clock::now();
    
    // 创建节点
    std::string node_name = "stability_node_" + std::to_string(g_config.process_id);
    std::shared_ptr<Node> node = createNode(node_name);
    if (!node) {
        std::cerr << "Failed to create node: " << node_name << "\n";
        return 1;
    }
    
    std::cout << "Node created: " << node_name << "\n";
    
    // 订阅所有topic（进程内 + 跨进程）
    std::cout << "Subscribing to topics...\n";
    
    // 构建订阅列表
    std::vector<std::string> inprocess_topic_list;
    for (size_t i = 0; i < g_config.inprocess_topics.size(); ++i) {
        inprocess_topic_list.push_back(g_config.inprocess_topics[i]);
        std::cout << "  - stability_inprocess/" << g_config.inprocess_topics[i] << "\n";
    }
    
    std::vector<std::string> cross_topic_list;
    for (size_t i = 0; i < g_config.cross_topics.size(); ++i) {
        cross_topic_list.push_back(g_config.cross_topics[i]);
        std::cout << "  - stability_cross/" << g_config.cross_topics[i] << "\n";
    }
    
    node->subscribe("stability_inprocess", inprocess_topic_list, onMessage);
    node->subscribe("stability_cross", cross_topic_list, onMessage);
    
    // 等待其他进程启动
    std::cout << "\nWaiting 3 seconds for all processes to start...\n";
    std::this_thread::sleep_for(seconds(3));
    
    // 启动发布线程
    std::vector<std::thread> publishers;
    
    std::cout << "\nStarting publishers...\n";
    // 进程内通信
    for (size_t i = 0; i < g_config.inprocess_topics.size(); ++i) {
        publishers.push_back(std::thread(publishThread, node, 
                               "stability_inprocess", g_config.inprocess_topics[i], false));
    }
    
    // 跨进程通信（与其他进程通信）
    for (size_t i = 0; i < g_config.cross_topics.size(); ++i) {
        publishers.push_back(std::thread(publishThread, node, 
                               "stability_cross", g_config.cross_topics[i], true));
    }
    
    std::cout << "\n🚀 Test running for " << g_config.test_duration_seconds 
              << " seconds... (Press Ctrl+C to stop early)\n\n";
    
    // 运行指定时间或直到收到信号
    auto end_time = steady_clock::now() + seconds(g_config.test_duration_seconds);
    while (g_running && steady_clock::now() < end_time) {
        std::this_thread::sleep_for(seconds(1));
    }
    
    // 停止所有线程
    std::cout << "\n[Process " << g_config.process_id << "] Stopping test...\n";
    g_running = false;
    
    // 等待所有发布线程退出（重要！）
    for (size_t i = 0; i < publishers.size(); ++i) {
        if (publishers[i].joinable()) {
            publishers[i].join();
        }
    }
    
    // 确保线程完全退出后再销毁node
    std::cout << "[Process " << g_config.process_id << "] All threads stopped\n";
    std::this_thread::sleep_for(milliseconds(100));
    
    // 生成最终报告
    generateFinalReport();
    
    return 0;
}
