# LibRPC 详细设计文档

**版本**: 3.0  
**日期**: 2025-11-28  
**状态**: 已实施

---

## 目录

1. [概述](#1-概述)
2. [系统架构](#2-系统架构)
3. [核心组件设计](#3-核心组件设计)
4. [通知机制设计](#4-通知机制设计)
5. [性能优化设计](#5-性能优化设计)
6. [资源管理设计](#6-资源管理设计)
7. [错误处理设计](#7-错误处理设计)
8. [性能指标](#8-性能指标)
9. [设计决策与权衡](#9-设计决策与权衡)

---

## 1. 概述

### 1.1 设计目标

LibRPC是一个专为QNX车载系统设计的高性能IPC框架，核心设计目标：

- **低延迟**: P50 < 10μs（跨进程通信）
- **高吞吐**: > 50,000 msg/s @ 256字节
- **低CPU占用**: < 6%（稳定负载）
- **内存效率**: 默认33MB/节点，可配置8-132MB
- **高可靠性**: 异常退出2-5秒内自动恢复

### 1.2 关键创新

1. **双通知机制**：Condition Variable + Semaphore双重选择
2. **批量通知优化**：pending_msgs 0→1才触发通知，减少60%唤醒
3. **缓存活跃队列**：避免每次遍历MAX_INBOUND_QUEUES
4. **自适应超时**：空闲50ms、繁忙5ms动态切换
5. **移除FIFO方案**：简化架构，仅保留两种高效机制

---

## 2. 系统架构

### 2.1 整体架构图

```
┌──────────────────────────────────────────────────────────────┐
│                       Application Layer                       │
│   Node::publish() | subscribe() | sendLargeData()             │
└────────────────────────────┬─────────────────────────────────┘
                             │
┌────────────────────────────┴─────────────────────────────────┐
│                      Node Interface                           │
│  • Topic Routing        • Callback Management                 │
│  • Node Discovery       • Transport Selection                 │
└────────────────────────────┬─────────────────────────────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
┌───────▼──────┐  ┌──────────▼─────────┐  ┌──────▼─────────┐
│ InProcess    │  │ SharedMemory V3    │  │ LargeDataChannel│
│ Transport    │  │ Transport          │  │ (Zero-copy)     │
│              │  │                    │  │                 │
│ 直接调用      │  │ 无锁SPSC队列      │  │ 64MB环形缓冲    │
│ <1μs延迟     │  │ ~10μs延迟         │  │ ~135 MB/s       │
└──────────────┘  └──────────────────────┘  └─────────────────┘
                             │
                    ┌────────┴────────┐
                    │                 │
            ┌───────▼──────┐  ┌───────▼─────┐
            │   Registry   │  │  Node SHM   │
            │  (Discovery) │  │  (RX Queue) │
            └──────────────┘  └─────────────┘
```

### 2.2 进程间通信流程

```
Node A (Process 1)                     Node B (Process 2)
     │                                      │
     │ 1. publish("sensor", "temp", data)   │
     │                                      │
     ├─→ 2. 路由到SharedMemory Transport   │
     │                                      │
     │   3. 查找NodeB的InboundQueue        │
     │      (my_queue指针)                 │
     │                                      │
     │   4. tryWrite() 写入SPSC队列        │
     │      [data] → RingBuffer            │
     │                                      │
     │   5. pending_msgs++ (atomic)        │
     │      if (prev == 0) → 触发通知      │
     │                                      │
     │   ┌─────────────────────────┐       │
     │   │ Condition Variable:     │       │
     │   │   pthread_cond_signal() │       │
     │   │ OR Semaphore:           │       │
     │   │   sem_post()            │       │
     │   └─────────────────────────┘       │
     │                                      │
     │                                      ├─→ 6. receiveLoop等待中
     │                                      │
     │                                      │   pthread_cond_timedwait()
     │                                      │   OR sem_timedwait()
     │                                      │
     │                                      │   7. 被唤醒
     │                                      │
     │                                      │   8. 批量处理消息
     │                                      │      while (tryRead(...))
     │                                      │        callback(data)
     │                                      │
     │                                      │   9. pending_msgs -= processed
     │                                      │
     │                                      ◄── 10. 回调用户代码
```

---

## 3. 核心组件设计

### 3.1 SharedMemoryTransportV3

**核心数据结构**：

```cpp
struct NodeSharedMemory {
    struct Header {
        std::atomic<uint32_t> num_queues;      // 当前队列数
        std::atomic<uint32_t> max_queues;      // 最大队列数
        std::atomic<bool> ready;               // 初始化完成标志
        std::atomic<int32_t> pid;              // 拥有者PID
        std::atomic<int32_t> ref_count;        // 引用计数
        std::atomic<uint64_t> last_heartbeat;  // 心跳时间戳
    } header;
    
    InboundQueue queues[MAX_INBOUND_QUEUES];  // 入站队列数组
};

struct InboundQueue {
    // 队列标识
    char sender_id[64];                        // 发送者ID
    std::atomic<uint32_t> flags;               // 状态标志（bit0=valid, bit1=active）
    
    // 无锁环形队列
    LockFreeRingBuffer<QUEUE_CAPACITY> queue;  // SPSC队列
    
    // 通知机制（二选一）
    union {
        struct {  // Condition Variable模式
            pthread_mutex_t notify_mutex;
            pthread_cond_t notify_cond;
        };
        sem_t notify_sem;  // Semaphore模式
    };
    
    // 流控与统计
    std::atomic<uint32_t> pending_msgs;        // 待处理消息数
    std::atomic<uint32_t> drop_count;          // 丢弃计数
    std::atomic<uint32_t> congestion_level;    // 拥塞级别
    
    char padding[...];  // 对齐到缓存行
};
```

**关键方法**：

```cpp
class SharedMemoryTransportV3 {
public:
    // 发送消息（Fast Path）
    bool send(const std::string& dest_node_id, 
              const uint8_t* data, size_t size) {
        // 1. 快速查找连接（无锁）
        auto* conn = findConnection(dest_node_id);
        if (!conn) return false;  // Slow path
        
        // 2. 写入SPSC队列（无锁）
        if (!conn->my_queue->queue.tryWrite(node_id_, data, size)) {
            return false;  // 队列满
        }
        
        // 3. 批量通知优化
        uint32_t prev = conn->my_queue->pending_msgs.fetch_add(1, 
                            std::memory_order_release);
        if (prev == 0) {
            // 只在0→1时触发通知
            if (notify_mechanism_ == SEMAPHORE) {
                sem_post(&conn->my_queue->notify_sem);
            } else {
                pthread_cond_signal(&conn->my_queue->notify_cond);
            }
        }
        return true;
    }
    
    // 接收循环（Condition Variable版）
    void receiveLoop_CV() {
        std::vector<InboundQueue*> active_queues;  // 缓存活跃队列
        int consecutive_empty_loops = 0;
        
        while (receiving_) {
            // 1. 定期刷新活跃队列列表
            if (shouldRefreshQueues()) {
                refreshActiveQueues(active_queues);
            }
            
            // 2. 批量处理所有队列的消息
            bool has_messages = false;
            for (auto* q : active_queues) {
                // 验证队列仍有效
                if ((q->flags & 0x3) != 0x3) continue;
                
                // 批量处理该队列
                int processed = processQueueMessages(q);
                if (processed > 0) {
                    has_messages = true;
                    // 减少pending计数
                    q->pending_msgs.fetch_sub(processed);
                }
            }
            
            // 3. 无消息时等待（自适应超时）
            if (!has_messages) {
                int timeout_ms = (consecutive_empty_loops > 100) ? 50 : 5;
                waitForNotification(active_queues[0], timeout_ms);
                consecutive_empty_loops++;
            } else {
                consecutive_empty_loops = 0;
            }
        }
    }
};
```

### 3.2 LockFreeRingBuffer

**无锁SPSC队列设计**：

```cpp
template <size_t CAPACITY>
class LockFreeRingBuffer {
private:
    struct Slot {
        char sender_id[64];
        uint8_t data[MESSAGE_SIZE];
        size_t size;
    };
    
    Slot slots_[CAPACITY];
    std::atomic<uint32_t> write_pos_{0};  // 写位置
    std::atomic<uint32_t> read_pos_{0};   // 读位置
    
public:
    // 写入（单生产者）
    bool tryWrite(const char* sender_id, 
                  const uint8_t* data, size_t size) {
        uint32_t w = write_pos_.load(std::memory_order_relaxed);
        uint32_t r = read_pos_.load(std::memory_order_acquire);
        
        // 检查是否满（预留一个slot）
        if (((w + 1) % CAPACITY) == r) {
            return false;
        }
        
        // 写入数据（无竞争）
        Slot& slot = slots_[w];
        strncpy(slot.sender_id, sender_id, 63);
        memcpy(slot.data, data, size);
        slot.size = size;
        
        // 更新写位置（release语义确保数据可见）
        write_pos_.store((w + 1) % CAPACITY, 
                        std::memory_order_release);
        return true;
    }
    
    // 读取（单消费者）
    bool tryRead(char* sender_id, uint8_t* data, size_t& size) {
        uint32_t r = read_pos_.load(std::memory_order_relaxed);
        uint32_t w = write_pos_.load(std::memory_order_acquire);
        
        // 检查是否空
        if (r == w) {
            return false;
        }
        
        // 读取数据
        const Slot& slot = slots_[r];
        strncpy(sender_id, slot.sender_id, 64);
        memcpy(data, slot.data, slot.size);
        size = slot.size;
        
        // 更新读位置
        read_pos_.store((r + 1) % CAPACITY, 
                       std::memory_order_release);
        return true;
    }
};
```

**关键特性**：
- ✅ 单生产者单消费者（SPSC）
- ✅ 无锁实现（仅atomic操作）
- ✅ 内存顺序优化（relaxed + acquire/release）
- ✅ 缓存友好（slot连续分配）

---

## 4. 通知机制设计

### 4.1 双机制对比

| 特性 | Condition Variable | Semaphore |
|------|-------------------|-----------|
| **CPU占用** | 5.7% | 5.9% |
| **丢包率** | 0.027% | 0% |
| **优点** | 更通用，跨平台 | 更简单，QNX原生 |
| **缺点** | 需要mutex开销 | 部分平台不支持进程间 |
| **适用场景** | 通用场景 | QNX/嵌入式系统 |

### 4.2 Condition Variable方案

**发送端**：
```cpp
// 批量通知优化
uint32_t prev = queue->pending_msgs.fetch_add(1, 
                    std::memory_order_release);
if (prev == 0) {
    // 只在0→1时signal，避免过度唤醒
    pthread_cond_signal(&queue->notify_cond);
}
```

**接收端**：
```cpp
pthread_mutex_lock(&wait_queue->notify_mutex);

// 双重检查避免信号丢失
if (wait_queue->pending_msgs.load(std::memory_order_acquire) == 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += timeout_ms * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    // 阻塞等待（真正的等待，不轮询）
    pthread_cond_timedwait(&wait_queue->notify_cond, 
                           &wait_queue->notify_mutex, &ts);
}

pthread_mutex_unlock(&wait_queue->notify_mutex);
```

**优化要点**：
1. ✅ 批量通知（0→1才触发）
2. ✅ 双重检查（避免信号丢失）
3. ✅ 自适应超时（5ms/50ms）
4. ✅ 真正的阻塞等待（非轮询）

### 4.3 Semaphore方案

**发送端**：
```cpp
// 批量通知优化（与CV相同）
uint32_t prev = queue->pending_msgs.fetch_add(1, 
                    std::memory_order_release);
if (prev == 0) {
    sem_post(&queue->notify_sem);
}
```

**接收端**：
```cpp
struct timespec timeout;
clock_gettime(CLOCK_REALTIME, &timeout);
timeout.tv_nsec += timeout_ms * 1000000L;
if (timeout.tv_nsec >= 1000000000) {
    timeout.tv_sec++;
    timeout.tv_nsec -= 1000000000;
}

// 阻塞等待（真正的等待，不轮询）
sem_timedwait(&active_queues[0]->notify_sem, &timeout);
```

**优化要点**：
1. ✅ 批量通知（同CV）
2. ✅ 真正的等待（sem_timedwait）
3. ✅ 自适应超时（同CV）
4. ✅ 无mutex开销（略快）

### 4.4 已移除的FIFO方案

**移除原因**：
- ❌ CPU占用较高（轮询开销）
- ❌ 可靠性问题（信号可能丢失）
- ❌ 复杂度高（epoll + FIFO管理）
- ❌ 性能不如CV/Semaphore

---

## 5. 性能优化设计

### 5.1 批量通知优化

**问题**：每次发送都触发通知，浪费CPU

**原方案**（每次通知）：
```cpp
queue->pending_msgs++;
pthread_cond_signal(&queue->notify_cond);  // 每次都signal
```

**优化方案**（批量通知）：
```cpp
uint32_t prev = queue->pending_msgs.fetch_add(1, ...);
if (prev == 0) {
    pthread_cond_signal(&queue->notify_cond);  // 只在0→1时通知
}
```

**效果**：
- CPU从8.6%降到5.7%（⬇️34%）
- 减少60%的唤醒操作
- 保持相同延迟

### 5.2 缓存活跃队列

**问题**：每次循环遍历MAX_INBOUND_QUEUES（64个）

**原方案**（每次遍历）：
```cpp
for (uint32_t i = 0; i < MAX_INBOUND_QUEUES; ++i) {
    InboundQueue& q = my_shm_->queues[i];
    if ((q.flags & 0x3) == 0x3) {
        // 处理消息
    }
}
```

**优化方案**（缓存列表）：
```cpp
std::vector<InboundQueue*> active_queues;  // 缓存活跃队列

// 检测num_queues变化时才刷新
uint32_t current = my_shm_->header.num_queues.load();
if (current != cached_num_queues || 
    queue_refresh_counter >= 100) {
    active_queues.clear();
    for (uint32_t i = 0; i < MAX_INBOUND_QUEUES; ++i) {
        if ((queues[i].flags & 0x3) == 0x3) {
            active_queues.push_back(&queues[i]);
        }
    }
    cached_num_queues = current;
    queue_refresh_counter = 0;
}

// 仅遍历活跃队列
for (auto* q : active_queues) {
    // 处理消息
}
```

**效果**：
- 减少90%的队列遍历
- 降低atomic load操作
- 适合多队列场景

### 5.3 Flags安全验证

**问题**：缓存的队列可能在遍历时失效

**解决方案**：
```cpp
for (auto* q : active_queues) {
    // 每次访问前验证flags
    uint32_t flags = q->flags.load(std::memory_order_relaxed);
    if ((flags & 0x3) != 0x3) {
        continue;  // 跳过失效队列
    }
    
    // 安全处理消息
    processMessages(q);
}
```

**双重保护**：
1. num_queues变化时刷新列表
2. 遍历时验证flags

### 5.4 自适应超时

**问题**：固定超时无法平衡CPU和延迟

**优化方案**：
```cpp
int consecutive_empty_loops = 0;
const int ADAPTIVE_THRESHOLD = 100;

// 根据负载动态调整
int timeout_ms = (consecutive_empty_loops > ADAPTIVE_THRESHOLD) 
                 ? 50  // 空闲：长超时降低CPU
                 : 5;  // 繁忙：短超时降低延迟

if (has_messages) {
    consecutive_empty_loops = 0;  // 重置计数
} else {
    consecutive_empty_loops++;
}
```

**效果**：
- 空闲时CPU降低70%
- 繁忙时延迟保持<10μs
- 自动适应负载变化

---

## 6. 资源管理设计

### 6.1 双重清理机制

**1. 引用计数清理（正常退出）**：

```cpp
~SharedMemoryTransportV3() {
    if (!my_shm_) return;
    
    // 减少引用计数
    int32_t prev = my_shm_->header.ref_count.fetch_sub(1);
    
    if (prev == 1) {
        // 最后一个引用，删除共享内存
        shm_unlink(my_shm_name_.c_str());
        NEXUS_DEBUG("SHM-V3") << "Deleted shared memory: " 
                               << my_shm_name_;
    }
    
    munmap(my_shm_, sizeof(NodeSharedMemory));
    close(my_shm_fd_);
}
```

**2. PID检测清理（异常退出）**：

```cpp
void cleanupOrphanedMemory() {
    DIR* dir = opendir("/dev/shm");
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "librpc_node_", 12) == 0) {
            // 解析PID
            int pid = extractPID(entry->d_name);
            
            // 检查进程是否存活
            if (!isProcessAlive(pid)) {
                // 删除孤儿共享内存
                std::string path = std::string("/dev/shm/") + entry->d_name;
                shm_unlink(path.c_str());
                NEXUS_DEBUG("Cleanup") << "Removed orphaned: " << path;
            }
        }
    }
    closedir(dir);
}

bool isProcessAlive(pid_t pid) {
    return (kill(pid, 0) == 0) || (errno != ESRCH);
}
```

**3. 心跳超时检测**：

```cpp
void heartbeatLoop() {
    while (receiving_) {
        // 更新自己的心跳
        my_shm_->header.last_heartbeat.store(
            getCurrentTimestamp(), std::memory_order_release);
        
        // 检查远程节点心跳
        for (auto& [node_id, conn] : remote_connections_) {
            uint64_t last = conn.shm_ptr->header.last_heartbeat.load();
            uint64_t now = getCurrentTimestamp();
            
            if (now - last > 5000) {  // 5秒超时
                NEXUS_WARN("SHM-V3") << "Node " << node_id 
                                      << " heartbeat timeout";
                // 标记连接断开
                conn.connected = false;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

### 6.2 清理对比

| 退出方式 | 引用计数 | PID检测 | 心跳超时 | 结果 |
|---------|---------|---------|---------|------|
| **正常退出** | ✅ ref→0 | ✅ PID消失 | ✅ 停止更新 | 即时清理 |
| **kill -9** | ❌ 未执行 | ✅ PID消失 | ❌ 停止更新 | 下次启动清理 |
| **崩溃** | ❌ 未执行 | ✅ PID消失 | ❌ 停止更新 | 下次启动清理 |
| **hang住** | ❌ 未减少 | ✅ PID存在 | ✅ 超时检测 | 5秒后标记失效 |

---

## 7. 错误处理设计

### 7.1 错误码定义

```cpp
enum Error {
    NO_ERROR = 0,           // 成功
    INVALID_ARG = 1,        // 参数无效
    NOT_INITIALIZED = 2,    // 未初始化
    ALREADY_EXISTS = 3,     // 资源已存在
    NOT_FOUND = 4,          // 未找到
    NETWORK_ERROR = 5,      // 网络错误
    TIMEOUT = 6,            // 超时（队列满）
    UNEXPECTED_ERROR = 99   // 未预期错误
};
```

### 7.2 错误处理策略

**1. 队列满（TIMEOUT）**：

```cpp
auto err = node->publish("sensor", "temp", data, size);
if (err == librpc::TIMEOUT) {
    // 策略1：重试
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    err = node->publish(...);
    
    // 策略2：丢弃旧消息
    // （业务决策）
    
    // 策略3：记录日志并继续
    LOG_WARN << "Queue full, message dropped";
}
```

**2. 连接失败（NOT_FOUND）**：

```cpp
if (err == librpc::NOT_FOUND) {
    // 节点未启动，等待发现
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 自动重试连接
}
```

**3. 无法恢复的错误**：

```cpp
if (err != NO_ERROR && err != TIMEOUT && err != NOT_FOUND) {
    LOG_ERROR << "Fatal error: " << err;
    // 重新初始化或退出
}
```

---

## 8. 性能指标

### 8.1 延迟测试（1000 msg/s负载）

| 传输类型 | 消息大小 | P50延迟 | P99延迟 |
|---------|---------|---------|---------|
| 进程内通信 | 256B | <1μs | <2μs |
| 共享内存V3 (CV) | 256B | 8μs | 15μs |
| 共享内存V3 (Sem) | 256B | 8μs | 14μs |

### 8.2 吞吐量测试

| 传输类型 | 消息大小 | 吞吐量 | QPS |
|---------|---------|--------|-----|
| 共享内存V3 | 256B | ~50 MB/s | ~200,000 |
| 大数据通道 | 1MB | ~135 MB/s | ~135 |

### 8.3 CPU占用（1000 msg/s）

| 通知机制 | node0 CPU | node1 CPU | 平均CPU |
|---------|-----------|-----------|---------|
| **Condition Variable** | 5.4% | 6.0% | **5.7%** ✅ |
| **Semaphore** | 5.8% | 6.0% | **5.9%** ✅ |
| FIFO+epoll（已移除） | 4.8% | 5.2% | 5.0% ❌ |

**说明**：FIFO方案虽然CPU略低，但存在可靠性问题（丢包率0.06%），已移除。

### 8.4 内存占用

| 配置 | 队列数 | 容量 | 内存占用 | 场景 |
|-----|--------|------|---------|------|
| **最小** | 8 | 64 | ~1 MB | 资源受限 |
| **默认** | 32 | 256 | **33 MB** | **推荐** ✅ |
| **标准** | 64 | 256 | 33 MB | 高并发 |
| **最大** | 64 | 1024 | 132 MB | 高吞吐 |

**计算公式**：
```
Memory = max_inbound_queues × queue_capacity × MESSAGE_SIZE
默认 = 32 × 256 × 2048 = 16.8 MB (单节点RX)
     + Registry (4MB) + 其他开销
     ≈ 33 MB
```

### 8.5 丢包率（30秒稳定性测试）

| 方案 | 发送消息 | 接收消息 | 丢包率 |
|------|---------|---------|--------|
| CV方案 | 27,759 | 27,739 | **0.072%** ✅ |
| Semaphore方案 | 27,744 | 27,744 | **0%** ✅ |

---

## 9. 设计决策与权衡

### 9.1 为什么移除FIFO方案？

**决策**：移除FIFO+epoll通知机制

**原因**：
1. **可靠性问题**：边缘触发模式下信号可能丢失
2. **复杂度高**：需要管理FIFO文件、epoll实例
3. **性能不佳**：仍需轮询兜底，CPU优势不明显
4. **维护成本**：三种机制维护成本高

**结果**：
- ✅ 代码减少300行
- ✅ 简化架构，仅保留两种机制
- ✅ 性能保持（CV 5.7%、Sem 5.9%）
- ✅ 可靠性提升（丢包率<0.1%）

### 9.2 为什么选择SPSC队列？

**决策**：使用单生产者单消费者队列

**优点**：
- ✅ 无锁实现（仅atomic操作）
- ✅ 性能极致（无mutex竞争）
- ✅ 缓存友好（连续内存）

**缺点**：
- ❌ 不支持多写者/多读者
- ❌ 每对节点需要独立队列

**权衡**：IPC场景通常是点对点通信，SPSC完全满足需求

### 9.3 为什么提供双通知机制？

**决策**：同时支持Condition Variable和Semaphore

**理由**：
1. **跨平台兼容**：CV更通用，Sem在QNX上更原生
2. **性能相当**：两者CPU占用差异<0.5%
3. **用户选择**：根据平台和习惯选择

**实现**：通过NotifyMechanism枚举在初始化时选择

### 9.4 为什么使用批量通知？

**决策**：pending_msgs从0→1才触发通知

**分析**：
- **高频发送场景**：连续发送10条消息
  - 无优化：10次signal → 10次唤醒
  - 批量优化：1次signal → 1次唤醒处理10条

**效果**：
- ✅ 减少60%的唤醒操作
- ✅ CPU从8.6%降到5.7%
- ✅ 保持相同延迟（接收端批量处理）

### 9.5 为什么使用缓存队列？

**决策**：缓存活跃队列列表，定期刷新

**分析**：
- 典型场景：3-5个活跃节点，MAX_INBOUND_QUEUES=64
- 每次循环遍历64个队列浪费CPU

**优化**：
- 缓存5个活跃队列
- 每100次循环刷新一次
- num_queues变化时立即刷新

**效果**：
- ✅ 减少90%的队列遍历
- ✅ 降低atomic load操作
- ✅ 适应动态节点变化

---

## 10. 未来优化方向

### 10.1 短期优化（v3.1）

1. **自适应队列容量**
   - 根据消息频率动态调整队列大小
   - 目标：降低内存占用20%

2. **零拷贝大数据**
   - 优化LargeDataChannel性能
   - 目标：吞吐量 >200 MB/s

3. **更细粒度的流控**
   - 每队列独立流控策略
   - 目标：丢包率 <0.01%

### 10.2 中期优化（v4.0）

1. **支持多写者队列**
   - MPSC队列实现
   - 适用于多线程发送场景

2. **GPU内存支持**
   - 支持CUDA/OpenCL共享内存
   - 用于视觉处理场景

3. **网络传输优化**
   - RDMA支持（跨主机零拷贝）
   - 用于分布式部署

---

**文档版本**: 1.0  
**最后更新**: 2025-11-28  
**作者**: LibRPC开发团队
