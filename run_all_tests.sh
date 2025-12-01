#!/bin/bash

echo "========================================"
echo "  Nexus IPC Framework - 完整测试套件"
echo "========================================"
echo ""

PASS=0
FAIL=0

run_test() {
    local name=$1
    local cmd=$2
    
    echo "========================================="
    echo "测试: $name"
    echo "========================================="
    
    if eval "$cmd"; then
        echo "✅ $name - 通过"
        ((PASS++))
    else
        echo "❌ $name - 失败"
        ((FAIL++))
    fi
    echo ""
}

# 1. 进程内通信测试
run_test "进程内通信" "timeout 15 ./build/test_inprocess 10 10 | tail -5"

# 2. 全双工通信测试  
run_test "全双工通信" "timeout 15 bash run_duplex_test.sh 10 256 1000 | tail -10"

# 3. 服务发现测试
run_test "服务发现" "timeout 10 ./build/test_service_discovery"

# 4. 服务清理测试
run_test "服务清理" "timeout 10 ./build/test_service_cleanup"

# 5. 跨进程节点事件
run_test "跨进程节点事件" "timeout 15 bash run_cross_process_node_events.sh | tail -10"

# 6. 跨进程服务发现
run_test "跨进程服务发现" "timeout 15 bash run_cross_process_discovery.sh | tail -10"

# 7. 大数据通道（快速测试）
run_test "大数据通道" "timeout 20 bash run_large_data_test.sh 20 512 | grep -A 5 '测试1:'"

echo "========================================"
echo "  测试结果汇总"
echo "========================================"
echo "✅ 通过: $PASS"
echo "❌ 失败: $FAIL"
echo "总计: $((PASS + FAIL))"
echo ""

if [ $FAIL -eq 0 ]; then
    echo "🎉 所有测试通过！"
    exit 0
else
    echo "⚠️  有测试失败，请检查"
    exit 1
fi
