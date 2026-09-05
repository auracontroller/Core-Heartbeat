#!/bin/bash

# Cleanup any existing processes
pkill -f CoreRelay
pkill -f Heartbeat
pkill -f Sister
pkill -f Dummy

# Ensure we're in the build directory
cd build

echo "Starting CoreRelay, Heartbeat, and Sister..."

./CoreRelay > relay_test.log 2>&1 &
RELAY_PID=$!
sleep 1

./Heartbeat > heartbeat_test.log 2>&1 &
HEARTBEAT_PID=$!
sleep 1

./Sister > sister_test.log 2>&1 &
SISTER_PID=$!
sleep 1

echo "==================================="
echo "Running TestDrops: Simulating abrupt drops"
./TestDrops > test_drops.log 2>&1
sleep 2

echo "==================================="
echo "Running TestParsing: Sending malformed packets"
./TestParsing > test_parsing.log 2>&1
sleep 2

# Check if CoreRelay crashed from the malformed packets
if ps -p $RELAY_PID > /dev/null; then
    echo "[PASS] CoreRelay survived TestParsing and TestDrops."
else
    echo "[FAIL] CoreRelay crashed!"
fi

echo "==================================="
echo "Running TestPacts: Establishing multiple dynamic pacts"
./TestPacts > test_pacts.log 2>&1
sleep 5

echo "==================================="
echo "Running TestMutex: Spamming Heartbeat to trigger Freeze Cascade"
./TestMutex > test_mutex.log 2>&1
sleep 2

# Check if Heartbeat correctly cascaded to SUSPEND_ALL due to lock
if grep -q "SUSPEND_ALL" heartbeat_test.log; then
    echo "[PASS] Heartbeat successfully triggered SUSPEND_ALL from the freeze cascade."
else
    echo "[FAIL] Heartbeat failed to trigger SUSPEND_ALL!"
fi

echo "==================================="
echo "Testing Developer Console ADMIN_CLOSE and Total Amnesia"
./Dummy 2 > dummy2_console.log 2>&1 &
DUMMY2_PID=$!
sleep 1

# Extract assigned Dock ID
DOCK_ID=$(grep -o "Assigned Dock ID: [0-9]*" dummy2_console.log | cut -d' ' -f4)

if [ -n "$DOCK_ID" ]; then
    echo "Dummy 2 connected on Dock $DOCK_ID. Sending ADMIN_CLOSE via Console..."
    # using echo directly to send standard input commands to Console executable
    echo -e "ADMIN_CLOSE\n$DOCK_ID\nquit\n" | ./Console > console_test.log 2>&1
    sleep 2
    if ps -p $DUMMY2_PID > /dev/null; then
        echo "[FAIL] Dummy 2 is still running after ADMIN_CLOSE."
    else
        echo "[PASS] Dummy 2 dropped after ADMIN_CLOSE."
        if grep -q "Total Amnesia enacted for Dock $DOCK_ID" heartbeat_test.log; then
             echo "[PASS] Heartbeat correctly enacted Total Amnesia."
        else
             echo "[FAIL] Heartbeat did not enact Total Amnesia!"
        fi
    fi
else
    echo "[ERROR] Could not determine Dummy 2 Dock ID."
fi

echo "==================================="
echo "Testing Sister Fail-Safe (Killing Heartbeat)"
kill -9 $HEARTBEAT_PID
sleep 2

if grep -q "SUSPEND_ALL" sister_test.log; then
    echo "[PASS] Sister correctly broadcast SUSPEND_ALL when Heartbeat died."
else
    echo "[FAIL] Sister failed to broadcast SUSPEND_ALL!"
fi

echo "==================================="
echo "Cleaning up..."
kill -9 $RELAY_PID
pkill -f Sister
pkill -f Dummy
pkill -f Console

echo "Test logs are saved in build/ (*.log)."
