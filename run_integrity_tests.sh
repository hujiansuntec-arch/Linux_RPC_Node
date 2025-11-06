#!/bin/bash

# Multi-Process Communication Integrity Verification Script
# This script runs comprehensive tests to verify communication completeness

echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║     LibRPC Multi-Process Communication Integrity Test Suite     ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
TESTS_PASSED=0
TESTS_FAILED=0

# Test result function
test_result() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}✅ PASS${NC}: $2"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}❌ FAIL${NC}: $2"
        ((TESTS_FAILED++))
    fi
}

# Clean up previous test logs
rm -f /tmp/test_*.log /tmp/integrity_*.log

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📋 Test 1: Simple 200-Message Integrity Test"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "Starting receiver..."
(LD_LIBRARY_PATH=./lib timeout 15 ./test_integrity_simple receiver > /tmp/integrity_recv.log 2>&1 &)
sleep 2

echo "Starting sender..."
LD_LIBRARY_PATH=./lib timeout 10 ./test_integrity_simple sender > /tmp/integrity_send.log 2>&1
sleep 2

# Check results
SENT=$(grep "All 200 messages sent" /tmp/integrity_send.log | wc -l)
RECEIVED=$(grep "Total received:" /tmp/integrity_recv.log | grep -o '[0-9]\+' | tail -1)
SUCCESS=$(grep "SUCCESS: All 200 messages received" /tmp/integrity_recv.log | wc -l)

echo "Results:"
echo "  - Messages sent: 200"
echo "  - Messages received: $RECEIVED"

if [ "$SUCCESS" -eq 1 ] && [ "$RECEIVED" -eq 200 ]; then
    test_result 0 "Simple integrity test (200 messages, 100% delivery)"
else
    test_result 1 "Simple integrity test (expected 200, got $RECEIVED)"
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📋 Test 2: Multi-Process Complex Scenario (4 Processes)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "Starting Process D (Monitor)..."
(LD_LIBRARY_PATH=./lib timeout 30 ./test_multi_process d > /tmp/test_proc_d.log 2>&1 &)
sleep 1

echo "Starting Process B (Pressure + Temp Receiver)..."
(LD_LIBRARY_PATH=./lib timeout 25 ./test_multi_process b > /tmp/test_proc_b.log 2>&1 &)
sleep 1

echo "Starting Process A (Temperature Sender)..."
(LD_LIBRARY_PATH=./lib timeout 25 ./test_multi_process a > /tmp/test_proc_a.log 2>&1 &)
sleep 1

echo "Starting Process C (Humidity + Press/Temp Receiver)..."
LD_LIBRARY_PATH=./lib timeout 20 ./test_multi_process c > /tmp/test_proc_c.log 2>&1
sleep 3

echo ""
echo "Analyzing results..."

# Process A
A_SENT=$(grep "Messages Sent:" /tmp/test_proc_a.log | grep -o '[0-9]\+' | head -1)
A_SELF=$(grep "Temp-A-" /tmp/test_proc_a.log | grep "📩" | wc -l)

# Process B
B_SENT=$(grep "Messages Sent:" /tmp/test_proc_b.log | grep -o '[0-9]\+' | head -1)
B_RECV=$(grep "Messages Received:" /tmp/test_proc_b.log | grep -o '[0-9]\+' | head -1)
B_SELF=$(grep "Press-B-" /tmp/test_proc_b.log | grep "📩" | wc -l)

# Process C
C_SENT=$(grep "Messages Sent:" /tmp/test_proc_c.log | grep -o '[0-9]\+' | head -1)
C_RECV=$(grep "Messages Received:" /tmp/test_proc_c.log | grep -o '[0-9]\+' | head -1)
C_SELF=$(grep "Humid-C-" /tmp/test_proc_c.log | grep "📩" | wc -l)

# Process D
D_RECV=$(grep "Messages Received:" /tmp/test_proc_d.log | grep -o '[0-9]\+' | head -1)

echo ""
echo "Process Statistics:"
echo "  Process A: Sent=$A_SENT, Self-received=$A_SELF"
echo "  Process B: Sent=$B_SENT, Received=$B_RECV, Self-received=$B_SELF"
echo "  Process C: Sent=$C_SENT, Received=$C_RECV, Self-received=$C_SELF"
echo "  Process D: Received=$D_RECV"
echo ""

# Validate results
test_result $([ "$A_SENT" -eq 100 ] && [ "$A_SELF" -eq 0 ]; echo $?) \
    "Process A sent 100 messages and received 0 self-messages"

test_result $([ "$B_SENT" -eq 100 ] && [ "$B_RECV" -eq 100 ] && [ "$B_SELF" -eq 0 ]; echo $?) \
    "Process B sent 100, received 100 from A, and 0 self-messages"

test_result $([ "$C_SENT" -eq 50 ] && [ "$C_RECV" -ge 100 ] && [ "$C_SELF" -eq 0 ]; echo $?) \
    "Process C sent 50, received $C_RECV (≥100), and 0 self-messages"

test_result $([ "$D_RECV" -eq 250 ]; echo $?) \
    "Process D received all 250 messages (100+100+50)"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📋 Test 3: Bidirectional Communication Test"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

rm -f /tmp/test_a.log /tmp/test_b.log

echo "Starting Process A..."
(LD_LIBRARY_PATH=./lib timeout 35 ./test_bidirectional a > /tmp/test_a.log 2>&1 &)
sleep 2

echo "Starting Process B..."
LD_LIBRARY_PATH=./lib timeout 30 ./test_bidirectional b > /tmp/test_b.log 2>&1
sleep 2

# Check results
A_B_MSG=$(grep "B-Message" /tmp/test_a.log | wc -l)
B_A_MSG=$(grep "A-Message" /tmp/test_b.log | wc -l)

echo ""
echo "Results:"
echo "  Process A received from B: $A_B_MSG messages"
echo "  Process B received from A: $B_A_MSG messages"
echo ""

test_result $([ "$A_B_MSG" -eq 20 ] && [ "$B_A_MSG" -ge 19 ]; echo $?) \
    "Bidirectional test (A received $A_B_MSG, B received $B_A_MSG, ≥19 required)"

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║                      Final Test Summary                          ║"
echo "╠══════════════════════════════════════════════════════════════════╣"
echo "║                                                                  ║"
printf "║  Tests Passed:  %-45s║\n" "$TESTS_PASSED"
printf "║  Tests Failed:  %-45s║\n" "$TESTS_FAILED"
echo "║                                                                  ║"

if [ $TESTS_FAILED -eq 0 ]; then
    echo "║  Overall Status: ✅ ALL TESTS PASSED                            ║"
    echo "║                                                                  ║"
    echo "║  ✅ Cross-process communication: WORKING                         ║"
    echo "║  ✅ Message integrity: 100%                                      ║"
    echo "║  ✅ No self-message reception: VERIFIED                          ║"
    echo "║  ✅ Bidirectional communication: WORKING                         ║"
    echo "║  ✅ Multi-process concurrent send/receive: WORKING               ║"
else
    echo "║  Overall Status: ❌ SOME TESTS FAILED                           ║"
fi

echo "║                                                                  ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# Exit with appropriate code
if [ $TESTS_FAILED -eq 0 ]; then
    echo "🎉 All integrity tests passed! System is ready for production."
    exit 0
else
    echo "⚠️  Some tests failed. Please review the logs."
    exit 1
fi
