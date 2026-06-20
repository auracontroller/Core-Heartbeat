#!/bin/bash

# Kill any previous run
pkill -f CoreRelay
pkill -f Heartbeat
pkill -f Dummy

cd build

./CoreRelay > relay.log 2>&1 &
RELAY_PID=$!
sleep 1

./Heartbeat > heartbeat.log 2>&1 &
HEARTBEAT_PID=$!
sleep 1

./Dummy 1 > dummy1.log 2>&1 &
D1_PID=$!
./Dummy 2 > dummy2.log 2>&1 &
D2_PID=$!
./Dummy 3 > dummy3.log 2>&1 &
D3_PID=$!

echo "System running. Waiting 15 seconds to accumulate logs..."
sleep 30

# Now test the Kill Switch
echo "Triggering Kill Switch (Killing Heartbeat)..."
kill -9 $HEARTBEAT_PID

# Wait a second for cascade to happen
sleep 2

# Check if Core is still running (it should be)
if ps -p $RELAY_PID > /dev/null; then
    echo "CoreRelay is still running (Success)"
else
    echo "CoreRelay crashed unexpectedly!"
fi

# Check if Dummies exited
if ps -p $D1_PID > /dev/null || ps -p $D2_PID > /dev/null || ps -p $D3_PID > /dev/null; then
    echo "Some Dummies did NOT exit when Core closed their connection!"
else
    echo "All Dummies exited successfully (Success)"
fi

# Cleanup
kill -9 $RELAY_PID
pkill -f Dummy
pkill -f Heartbeat
pkill -f CoreRelay

echo ""
echo "--- heartbeat.log ---"
cat heartbeat.log
echo "--- relay.log ---"
cat relay.log
echo "--- dummy1.log ---"
cat dummy1.log
echo "--- dummy2.log ---"
cat dummy2.log
echo "--- dummy3.log ---"
cat dummy3.log
