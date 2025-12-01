#!/bin/bash

# ============================================================================
# 简化版长时间稳定性测试启动脚本
# 基于现有的 test_inprocess 程序进行10分钟压力测试
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../../build"
TEST_BIN="$BUILD_DIR/test_inprocess"
LOG_DIR="$SCRIPT_DIR/logs"

# 测试参数
DURATION_SECONDS=600  # 10分钟
ITERATIONS=$((DURATION_SECONDS / 2))  # 每2秒一次迭代

# 解析命令行参数
if [ "$1" != "" ]; then
    DURATION_SECONDS=$1
    ITERATIONS=$((DURATION_SECONDS / 2))
fi

echo "╔══════════════════════════════════════════════════════════╗"
echo "║     Nexus IPC - Long-Term Stability Test                ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "Configuration:"
echo "  Duration:      $DURATION_SECONDS seconds ($((DURATION_SECONDS/60)) minutes)"
echo "  Iterations:    $ITERATIONS"
echo "  Test Binary:   $TEST_BIN"
echo "  Log Directory: $LOG_DIR"
echo ""

# 检查可执行文件
if [ ! -f "$TEST_BIN" ]; then
    echo "❌ Error: Test binary not found: $TEST_BIN"
    echo "   Please build the project first"
    exit 1
fi

# 创建日志目录
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*.log

# 清理之前的测试残留
echo "Cleaning up previous test artifacts..."
pkill -9 test_inprocess 2>/dev/null
sleep 1
rm -f /dev/shm/librpc_* 2>/dev/null
rm -f /dev/shm/nexus_* 2>/dev/null

echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║  Starting $ITERATIONS iterations of stress test...              ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

START_TIME=$(date +%s)
PASSED=0
FAILED=0
TOTAL_MESSAGES=0

# 运行测试循环
for i in $(seq 1 $ITERATIONS); do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    REMAINING=$((DURATION_SECONDS - ELAPSED))
    
    printf "[%s] Iteration %d/%d | Elapsed: %ds | Remaining: %ds\n" \
           "$(date +%H:%M:%S)" $i $ITERATIONS $ELAPSED $REMAINING
    
    # 运行测试
    LOG_FILE="$LOG_DIR/iteration_${i}.log"
    $TEST_BIN stress > "$LOG_FILE" 2>&1
    EXIT_CODE=$?
    
    if [ $EXIT_CODE -eq 0 ]; then
        # 检查是否通过
        if grep -q "✅ 压力测试通过" "$LOG_FILE"; then
            PASSED=$((PASSED + 1))
            # 提取消息数
            MSGS=$(grep "总计:" "$LOG_FILE" | awk '{print $2}' | cut -d'/' -f1)
            if [ "$MSGS" != "" ]; then
                TOTAL_MESSAGES=$((TOTAL_MESSAGES + MSGS))
            fi
            printf "  ✅ PASSED (Messages: %s)\n" "$MSGS"
        else
            FAILED=$((FAILED + 1))
            printf "  ❌ FAILED (test completed but assertion failed)\n"
        fi
    else
        FAILED=$((FAILED + 1))
        printf "  ❌ FAILED (exit code: %d)\n" $EXIT_CODE
    fi
    
    # 检查内存使用
    MEM_KB=$(ps aux | grep test_inprocess | grep -v grep | awk '{sum+=$6} END {print sum}')
    if [ "$MEM_KB" != "" ]; then
        MEM_MB=$((MEM_KB / 1024))
        printf "  Memory: %d MB\n" $MEM_MB
    fi
    
    # 如果超时则退出
    if [ $ELAPSED -ge $DURATION_SECONDS ]; then
        echo ""
        echo "Duration limit reached, stopping test..."
        break
    fi
    
    # 短暂休息
    sleep 1
done

END_TIME=$(date +%s)
TOTAL_ELAPSED=$((END_TIME - START_TIME))

echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║  Test Completed - Generating Report...                  ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# ============================================================================
# 生成报告
# ============================================================================
REPORT_FILE="$LOG_DIR/summary_report.txt"

{
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║        Long-Term Stability Test - Summary Report        ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo ""
    echo "Test Configuration:"
    echo "  Target Duration:     $DURATION_SECONDS seconds ($((DURATION_SECONDS/60)) minutes)"
    echo "  Actual Duration:     $TOTAL_ELAPSED seconds ($((TOTAL_ELAPSED/60)) minutes)"
    echo "  Total Iterations:    $i"
    echo "  Test Completed:      $(date)"
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo ""
    echo "Results Summary:"
    echo "  ✅ Passed:           $PASSED iterations"
    echo "  ❌ Failed:           $FAILED iterations"
    echo "  Success Rate:       $(awk "BEGIN {printf \"%.2f\", ($PASSED*100.0/$i)}")%"
    echo "  Total Messages:     $TOTAL_MESSAGES"
    echo "  Avg Msg/Iteration:  $((TOTAL_MESSAGES / (PASSED > 0 ? PASSED : 1)))"
    echo ""
    
    # 检查内存泄漏
    echo "Memory Analysis:"
    FIRST_MEM=$(grep "Memory:" "$LOG_DIR/iteration_1.log" 2>/dev/null | tail -1 | awk '{print $2}')
    LAST_MEM=$(grep "Memory:" "$LOG_DIR/iteration_${i}.log" 2>/dev/null | tail -1 | awk '{print $2}')
    
    if [ "$FIRST_MEM" != "" ] && [ "$LAST_MEM" != "" ]; then
        MEM_DIFF=$((LAST_MEM - FIRST_MEM))
        echo "  First Iteration:    ${FIRST_MEM} MB"
        echo "  Last Iteration:     ${LAST_MEM} MB"
        echo "  Memory Growth:      ${MEM_DIFF} MB"
        
        if [ $MEM_DIFF -lt 10 ]; then
            echo "  Memory Status:      ✅ No significant leak detected"
        else
            echo "  Memory Status:      ⚠️  Memory growth detected!"
        fi
    else
        echo "  (Memory data not available)"
    fi
    
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo ""
    
    # 最终判定
    SUCCESS_RATE=$(awk "BEGIN {print ($PASSED*100.0/$i)}")
    if (( $(echo "$SUCCESS_RATE >= 99.0" | bc -l) )); then
        echo "╔══════════════════════════════════════════════════════════╗"
        echo "║  ✅  STABILITY TEST PASSED!                              ║"
        echo "╚══════════════════════════════════════════════════════════╝"
        FINAL_RESULT=0
    else
        echo "╔══════════════════════════════════════════════════════════╗"
        echo "║  ❌  STABILITY TEST FAILED!                              ║"
        echo "╚══════════════════════════════════════════════════════════╝"
        echo ""
        echo "Reasons:"
        echo "  - Success rate ($SUCCESS_RATE%) below 99%"
        FINAL_RESULT=1
    fi
    
    echo ""
    echo "Detailed logs available in: $LOG_DIR/"
    echo ""
    
} | tee "$REPORT_FILE"

echo ""
echo "Summary report saved to: $REPORT_FILE"
echo ""

exit $FINAL_RESULT
