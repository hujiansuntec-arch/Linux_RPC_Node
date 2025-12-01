# Nexus IPC Framework - 长时间稳定性测试指南

## 📋 测试概述

本稳定性测试包含两种测试方案：

### 方案1：完整多进程稳定性测试（推荐用于生产环境验证）
- **10个独立进程**，每个进程与其他进程通信
- **多topic通信**：每个进程3个进程内topic + 3个跨进程topic
- **高频率**：5ms 发送间隔
- **长时间运行**：默认10分钟（可配置）
- **全面监控**：内存、CPU、延迟、丢包率、乱序检测

### 方案2：简化单进程压力测试（快速验证）
- 基于现有 `test_inprocess` 的循环测试
- 快速启动，易于调试
- 适合快速验证稳定性

---

## 🚀 方案1：完整多进程测试

### 编译
```bash
cd /home/fz296w/workspace/polaris_rpc_qnx/librpc/build
cmake .. && make test_long_term_stability -j4
```

### 运行（推荐使用脚本）
```bash
cd /home/fz296w/workspace/polaris_rpc_qnx/librpc/tests/stability
chmod +x run_stability_test.sh
./run_stability_test.sh [duration_seconds]
```

**参数说明**：
- `duration_seconds`：可选，测试时长（秒），默认600（10分钟）

**示例**：
```bash
# 运行10分钟测试（默认）
./run_stability_test.sh

# 运行30分钟测试
./run_stability_test.sh 1800

# 运行1小时测试
./run_stability_test.sh 3600
```

### 手动运行单个进程（调试用）
```bash
# 进程0，运行60秒
./test_long_term_stability 0 60

# 进程5，运行600秒
./test_long_term_stability 5 600
```

### 监控运行状态
```bash
# 实时查看所有进程日志
tail -f tests/stability/logs/process_*.log

# 查看特定进程日志
tail -f tests/stability/logs/process_0.log

# 监控进程状态
ps aux | grep test_long_term_stability
```

### 测试输出

**运行时输出**（每10秒）：
```
========== Process 0 - Stats at 30s ==========
Total Sent: 18000 | Total Recv: 18000
Memory: 45.2 MB | CPU: 12.3%

Per-Topic Stats:
  stability_inprocess/sensor_data: Sent=6000 Recv=6000 Lost=0 OOO=0 AvgLat=0.15ms
  stability_inprocess/control_cmd: Sent=6000 Recv=6000 Lost=0 OOO=0 AvgLat=0.14ms
  ...
  
Summary: Loss Rate=0% Out-of-Order=0 Avg Latency=0.15ms
```

**最终报告**：
```
╔══════════════════════════════════════════════════════════╗
║       Process 0 - Final Report                           ║
╚══════════════════════════════════════════════════════════╝

Test Duration: 600 seconds (10 minutes)

Overall Statistics:
  Total Messages Sent:     720000
  Total Messages Received: 720000
  Peak Memory Usage:       48.5 MB
  Average CPU Usage:       15.2%

Per-Topic Final Stats:
  stability_inprocess/sensor_data:
    Sent:          120000
    Received:      120000
    Lost:          0 (0%)
    Out-of-Order:  0
    Avg Latency:   0.15 ms

Overall Quality Metrics:
  Loss Rate:           0%
  Out-of-Order Count:  0
  Average Latency:     0.16 ms

✅ STABILITY TEST PASSED!
```

### 汇总报告
测试完成后，查看汇总报告：
```bash
cat tests/stability/logs/summary_report.txt
```

---

## 🎯 方案2：简化压力测试

### 运行
```bash
cd /home/fz296w/workspace/polaris_rpc_qnx/librpc/tests/stability
chmod +x run_simple_stability_test.sh
./run_simple_stability_test.sh [duration_seconds]
```

**示例**：
```bash
# 10分钟测试（默认）
./run_simple_stability_test.sh

# 30分钟测试
./run_simple_stability_test.sh 1800
```

### 输出示例
```
[14:30:15] Iteration 50/300 | Elapsed: 100s | Remaining: 500s
  ✅ PASSED (Messages: 20000)
  Memory: 42 MB
```

---

## 📊 评估指标

### 1. **完整性（Completeness）**
- ✅ **丢包率 < 0.01%**：几乎无消息丢失
- ✅ **乱序数 = 0**：同topic消息严格有序

### 2. **稳定性（Stability）**
- ✅ **内存增长 < 10MB**：无明显内存泄漏
- ✅ **CPU稳定**：无异常峰值

### 3. **性能（Performance）**
- ✅ **平均延迟 < 1ms**：低延迟通信
- ✅ **吞吐量稳定**：200msg/s × 10进程 × 6topic = 12000msg/s

### 4. **可靠性（Reliability）**
- ✅ **无进程崩溃**：所有进程正常运行完成
- ✅ **无死锁**：所有线程正常退出

---

## 🔍 故障排查

### 问题1：进程启动失败
**现象**：某些进程无法创建节点
**原因**：共享内存残留
**解决**：
```bash
# 清理共享内存
rm -f /dev/shm/librpc_*
rm -f /dev/shm/nexus_*
```

### 问题2：高丢包率
**现象**：Loss Rate > 1%
**可能原因**：
1. 系统负载过高
2. 队列溢出
3. 网络/共享内存问题

**排查**：
```bash
# 检查系统负载
top

# 检查共享内存
ipcs -m

# 增加队列大小（修改 Config.cpp）
max_queue_size = 20000  # 默认10000
```

### 问题3：内存增长
**现象**：Memory Growth > 50MB
**排查**：
```bash
# 使用 valgrind 检测内存泄漏
valgrind --leak-check=full ./test_long_term_stability 0 60
```

### 问题4：进程僵死
**现象**：进程无响应
**排查**：
```bash
# 查看进程状态
ps aux | grep test_long_term_stability

# 查看线程状态
pstack <PID>

# 强制停止
pkill -9 test_long_term_stability
```

---

## 📈 性能调优建议

### 1. 发布间隔调整
修改 `TestConfig::publish_interval_ms`：
- **5ms**：高压力（默认）
- **10ms**：中压力
- **50ms**：轻压力

### 2. 进程数量调整
修改 `TestConfig::total_processes`：
- **10进程**：默认配置
- **20进程**：高并发测试
- **5进程**：轻量级测试

### 3. Topic数量调整
修改 `inprocess_topics` 和 `cross_topics`：
```cpp
std::vector<std::string> inprocess_topics = {
    "t1", "t2", "t3", "t4", "t5"  // 增加到5个
};
```

---

## ✅ 测试通过标准

所有进程必须满足：
1. ✅ Loss Rate < 0.01%
2. ✅ Out-of-Order Count = 0
3. ✅ Memory Growth < 10MB
4. ✅ No crashes
5. ✅ 平均延迟 < 1ms

---

## 📝 日志分析

### 查看特定时间段统计
```bash
grep "Stats at" tests/stability/logs/process_0.log
```

### 提取延迟数据
```bash
grep "AvgLat=" tests/stability/logs/process_*.log | awk -F'AvgLat=' '{print $2}' | sort -n
```

### 提取内存使用
```bash
grep "Memory:" tests/stability/logs/process_*.log | awk -F'Memory: ' '{print $2}' | sort -n
```

---

## 🎉 测试建议

### 短期测试（1-5分钟）
- **目的**：快速验证功能
- **命令**：`./run_stability_test.sh 300`

### 中期测试（10-30分钟）
- **目的**：稳定性初步验证
- **命令**：`./run_stability_test.sh 1800`

### 长期测试（1-24小时）
- **目的**：生产环境可靠性验证
- **命令**：`./run_stability_test.sh 86400`
- **建议**：使用 `nohup` 或 `screen` 运行

```bash
# 使用nohup后台运行
nohup ./run_stability_test.sh 86400 > stability_test.log 2>&1 &

# 使用screen（推荐）
screen -S stability_test
./run_stability_test.sh 86400
# 按 Ctrl+A, D 退出screen
# 恢复：screen -r stability_test
```

---

## 📧 结果提交

测试完成后，提交以下文件：
1. `tests/stability/logs/summary_report.txt` - 汇总报告
2. `tests/stability/logs/process_*.log` - 各进程详细日志
3. 测试环境信息（OS, CPU, 内存等）

---

**测试愉快！** 🚀
