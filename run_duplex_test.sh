#!/bin/bash
# 全双工测试启动脚本 - 16节点并发测试

set -e

cd /home/fz296w/workspace/Nexus/Nexus

# 清理环境
echo "清理环境..."
killall -9 test_duplex_v2 2>/dev/null || true
rm -f ./tmp/node*.log

# 设置参数
DURATION=${1:-20}
MSG_SIZE=${2:-256}
RATE=${3:-1000}
NUM_PAIRS=${4:-8}  # 默认8对=16个节点
NUM_NODES=$((NUM_PAIRS * 2))

echo "======================================"
echo "多节点全双工并发通信测试"
echo "======================================"
echo "节点数量: ${NUM_NODES} 个 (${NUM_PAIRS} 对全双工通信)"
echo "持续时间: ${DURATION}秒"
echo "消息大小: ${MSG_SIZE}字节"
echo "发送速率: ${RATE} msg/s (间隔: $((1000000/RATE))μs)"
echo "======================================"
echo ""

# 设置库路径和日志级别
export LD_LIBRARY_PATH=./build:$LD_LIBRARY_PATH
export NEXUS_LOG_LEVEL=DEBUG  # 使用DEBUG级别查看详细连接信息

# 🔧 并发启动所有节点对
# 配对方式: (node0, node1), (node2, node3), ..., (node14, node15)
declare -a PIDS

echo "正在启动 ${NUM_NODES} 个节点..."
for ((i=0; i<NUM_NODES; i+=2)); do
    NODE_A="node$i"
    NODE_B="node$((i+1))"
    
    echo "  启动节点对 [$NODE_A ↔ $NODE_B]"
    
    # 启动偶数节点 (发送给奇数节点)
    ./build/test_duplex_v2 $NODE_A $NODE_B $DURATION $MSG_SIZE $RATE > ./tmp/$NODE_A.log 2>&1 &
    PIDS[$i]=$!
    
    # 启动奇数节点 (发送给偶数节点)
    ./build/test_duplex_v2 $NODE_B $NODE_A $DURATION $MSG_SIZE $RATE > ./tmp/$NODE_B.log 2>&1 &
    PIDS[$((i+1))]=$!
done

echo ""
echo "所有节点已启动，等待测试完成..."
echo "进程PIDs: ${PIDS[@]}"
echo ""

# 等待所有进程完成
for pid in "${PIDS[@]}"; do
    wait $pid
done

echo ""
echo "======================================"
echo "测试完成！统计结果："
echo "======================================"
echo ""

# 统计所有节点的丢包率
success_count=0
fail_count=0
high_loss_nodes=""

for ((i=0; i<NUM_NODES; i++)); do
    NODE_NAME="node$i"
    LOGFILE="./tmp/$NODE_NAME.log"
    
    if [ -f "$LOGFILE" ]; then
        # 提取丢包率
        LOSS=$(grep "丢包率:" "$LOGFILE" | tail -1 | sed 's/.*丢包率: //' | sed 's/%//')
        
        if [ -n "$LOSS" ]; then
            # 格式化输出
            printf "%-10s 丢包率: %10s%%\n" "[$NODE_NAME]" "$LOSS"
            
            # 检查是否高丢包（简单字符串判断，大于10则认为高）
            if [[ "$LOSS" == *[0-9][0-9]* ]] || [[ "$LOSS" == [0-9][0-9].* ]]; then
                high_loss_nodes="$high_loss_nodes $NODE_NAME"
                ((fail_count++))
            else
                ((success_count++))
            fi
        else
            echo "[$NODE_NAME] ❌ 日志异常 - 无丢包率数据"
            ((fail_count++))
        fi
    else
        echo "[$NODE_NAME] ❌ 日志文件不存在"
        ((fail_count++))
    fi
done

echo ""
echo "======================================"
echo "汇总统计"
echo "======================================"
echo "总节点数: ${NUM_NODES}"
echo "正常节点: ${success_count} (丢包率 < 10%)"
echo "异常节点: ${fail_count}"
if [ -n "$high_loss_nodes" ]; then
    echo "高丢包节点:$high_loss_nodes"
fi
echo ""

# 显示详细结果（前4对节点）
echo "======================================"
echo "详细结果 (前4对节点)"
echo "======================================"
for ((i=0; i<8 && i<NUM_NODES; i++)); do
    NODE_NAME="node$i"
    echo ""
    echo "========== $NODE_NAME 详细统计 =========="
    cat ./tmp/$NODE_NAME.log | grep -A 10 "最终统计" || echo "[$NODE_NAME] 日志异常"
done

echo ""
echo "完整日志保存在: ./tmp/node*.log"