#!/bin/bash

# ============================================================================
# 长时间稳定性测试启动脚本
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../../build"
TEST_BIN="$BUILD_DIR/test_long_term_stability"

# 测试参数
NUM_PROCESSES=10
DURATION_SECONDS=600  # 10分钟，可以通过参数修改
LOG_DIR="$SCRIPT_DIR/logs"
ENABLE_STATS=""       # 🔧 详细统计开关

# 解析命令行参数
if [ "$1" != "" ]; then
    DURATION_SECONDS=$1
fi

# 🔧 检查是否启用详细统计
if [ "$2" == "--enable-stats" ] || [ "$3" == "--enable-stats" ]; then
    ENABLE_STATS="--enable-stats"
fi

echo "╔══════════════════════════════════════════════════════════╗"
echo "║     Nexus IPC - Long-Term Stability Test                ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "Configuration:"
echo "  Processes:     $NUM_PROCESSES"
echo "  Duration:      $DURATION_SECONDS seconds ($((DURATION_SECONDS/60)) minutes)"
echo "  Detailed Stats:$([ -n "$ENABLE_STATS" ] && echo " Enabled" || echo " Disabled (performance mode)")"
echo "  Log Directory: $LOG_DIR"
echo ""

# 检查可执行文件
if [ ! -f "$TEST_BIN" ]; then
    echo "❌ Error: Test binary not found: $TEST_BIN"
    echo "   Please build the project first:"
    echo "   cd $BUILD_DIR && make test_long_term_stability"
    exit 1
fi

# 创建日志目录
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*.log

# 清理之前的测试残留
echo "Cleaning up previous test artifacts..."
pkill -9 test_long_term_stability 2>/dev/null
sleep 1

# 清理共享内存和注册表
rm -f /dev/shm/librpc_* 2>/dev/null
rm -f /dev/shm/nexus_* 2>/dev/null

echo ""
echo "Starting $NUM_PROCESSES processes..."
echo ""

# 启动所有进程
PIDS=()
for i in $(seq 0 $((NUM_PROCESSES-1))); do
    LOG_FILE="$LOG_DIR/process_$i.log"
    echo "  Starting Process $i... (log: $LOG_FILE)"
    $TEST_BIN $i $DURATION_SECONDS $ENABLE_STATS > "$LOG_FILE" 2>&1 &
    PIDS[$i]=$!
    sleep 0.2  # 稍微错开启动时间
done

echo ""
echo "✓ All processes started!"
echo ""
echo "Process IDs:"
for i in $(seq 0 $((NUM_PROCESSES-1))); do
    echo "  Process $i: PID ${PIDS[$i]}"
done

echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║  Test is running... (Duration: $DURATION_SECONDS seconds)        ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "You can:"
echo "  - Monitor logs:  tail -f $LOG_DIR/process_*.log"
echo "  - Stop early:    kill ${PIDS[@]}"
echo "  - Press Ctrl+C to stop all processes"
echo ""

# 等待所有进程完成或捕获Ctrl+C
trap "echo ''; echo 'Stopping all processes...'; kill ${PIDS[@]} 2>/dev/null; exit 0" SIGINT SIGTERM

# 监控进程状态
START_TIME=$(date +%s)
while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    
    # 检查是否所有进程都还在运行
    RUNNING_COUNT=0
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            RUNNING_COUNT=$((RUNNING_COUNT + 1))
        fi
    done
    
    if [ $RUNNING_COUNT -eq 0 ]; then
        echo ""
        echo "All processes completed!"
        break
    fi
    
    # 显示进度
    REMAINING=$((DURATION_SECONDS - ELAPSED))
    if [ $REMAINING -ge 0 ]; then
        printf "\r[%s] Running: %d/%d processes | Elapsed: %ds | Remaining: %ds   " \
               "$(date +%H:%M:%S)" $RUNNING_COUNT $NUM_PROCESSES $ELAPSED $REMAINING
    fi
    
    sleep 1
done

echo ""
echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║  Collecting Results...                                   ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# 等待所有进程完全结束
sleep 2

# ============================================================================
# 汇总统计
# ============================================================================
REPORT_FILE="$LOG_DIR/summary_report.txt"
echo "Generating summary report: $REPORT_FILE"
echo ""

{
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║        Long-Term Stability Test - Summary Report        ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo ""
    echo "Test Configuration:"
    echo "  Number of Processes: $NUM_PROCESSES"
    echo "  Test Duration:       $DURATION_SECONDS seconds ($((DURATION_SECONDS/60)) minutes)"
    echo "  Test Completed:      $(date)"
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo ""
    
    TOTAL_SENT=0
    TOTAL_RECV=0
    TOTAL_LOST=0
    TOTAL_OOO=0
    PASSED_COUNT=0
    FAILED_COUNT=0
    
    for i in $(seq 0 $((NUM_PROCESSES-1))); do
        LOG_FILE="$LOG_DIR/process_$i.log"
        
        echo "Process $i Results:"
        echo "────────────────────────────────────────────────────────────"
        
        if [ -f "$LOG_FILE" ]; then
            # 提取最终报告部分
            if grep -q "Final Report" "$LOG_FILE"; then
                sed -n '/Final Report/,/STABILITY TEST/p' "$LOG_FILE" | \
                    grep -v "═" | grep -v "║" | sed 's/^/  /'
                
                # 提取关键指标
                SENT=$(grep "Total Messages Sent:" "$LOG_FILE" | tail -1 | awk '{print $4}')
                RECV=$(grep "Total Messages Received:" "$LOG_FILE" | tail -1 | awk '{print $4}')
                
                if [ "$SENT" != "" ]; then
                    TOTAL_SENT=$((TOTAL_SENT + SENT))
                fi
                if [ "$RECV" != "" ]; then
                    TOTAL_RECV=$((TOTAL_RECV + RECV))
                fi
                
                # 检查测试结果
                if grep -q "PASSED" "$LOG_FILE"; then
                    PASSED_COUNT=$((PASSED_COUNT + 1))
                else
                    FAILED_COUNT=$((FAILED_COUNT + 1))
                fi
            else
                echo "  ⚠️  No final report found (process may have crashed)"
                FAILED_COUNT=$((FAILED_COUNT + 1))
            fi
        else
            echo "  ❌ Log file not found"
            FAILED_COUNT=$((FAILED_COUNT + 1))
        fi
        
        echo ""
    done
    
    echo "════════════════════════════════════════════════════════════"
    echo ""
    echo "Overall Summary:"
    echo "  Total Messages Sent (all processes):     $TOTAL_SENT"
    echo "  Total Messages Received (all processes): $TOTAL_RECV"
    echo "  Processes Passed:                        $PASSED_COUNT / $NUM_PROCESSES"
    echo "  Processes Failed:                        $FAILED_COUNT / $NUM_PROCESSES"
    echo ""
    
    if [ $FAILED_COUNT -eq 0 ]; then
        echo "╔══════════════════════════════════════════════════════════╗"
        echo "║  ✅  ALL PROCESSES PASSED - TEST SUCCESSFUL!             ║"
        echo "╚══════════════════════════════════════════════════════════╝"
    else
        echo "╔══════════════════════════════════════════════════════════╗"
        echo "║  ❌  SOME PROCESSES FAILED - TEST FAILED!                ║"
        echo "╚══════════════════════════════════════════════════════════╝"
    fi
    echo ""
    
    echo "Detailed logs available in: $LOG_DIR/"
    echo ""
    
} | tee "$REPORT_FILE"

echo ""
echo "Summary report saved to: $REPORT_FILE"
echo ""

# 返回测试结果
if [ $FAILED_COUNT -eq 0 ]; then
    exit 0
else
    exit 1
fi
