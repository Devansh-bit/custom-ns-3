#!/bin/bash
# Force DFS Test Script
# Tests the entire pipeline: Kafka message → ns-3 consumer → DFS trigger → logs

set -e

echo "=========================================="
echo "Force DFS Integration Test"
echo "=========================================="
echo ""

# Configuration
KAFKA_BROKER="localhost:9092"
TOPIC="simulator-commands"
SIMULATION_ID="test-force-dfs"

echo "1. Testing Kafka Producer (simulating backend API)"
echo "--------------------------------------------------"

# Create test message
TEST_MESSAGE='{
  "command": "FORCE_DFS",
  "simulationId": "'$SIMULATION_ID'",
  "timestamp": '$(date +%s)'000'
}'

echo "Test message:"
echo "$TEST_MESSAGE" | jq '.'
echo ""

# Send to Kafka
echo "Sending to Kafka topic: $TOPIC"
echo "$TEST_MESSAGE" | kafka-console-producer \
  --broker-list $KAFKA_BROKER \
  --topic $TOPIC \
  --property "parse.key=true" \
  --property "key.separator=:" \
  <<< "$SIMULATION_ID:$TEST_MESSAGE"

if [ $? -eq 0 ]; then
    echo "✅ Message sent successfully to Kafka"
else
    echo "❌ Failed to send message to Kafka"
    exit 1
fi

echo ""
echo "2. Verification Steps"
echo "---------------------"
echo "Now you should:"
echo "  1. Start basic-simulation with simulation_id='$SIMULATION_ID'"
echo "  2. Watch for these logs:"
echo "     - [COMMAND] Received: FORCE_DFS at t=XXX.Xs"
echo "     - [FORCE-DFS] ⚡ Radar FORCED ON at t=XXX.Xs (5s burst)"
echo "     - [DFS-RADAR] Radar detected on channel XX"
echo "     - [DFS-SWITCH] Node X switched to Ch YY"
echo "     - [FORCE-DFS] ✓ Radar burst complete at t=XXX.Xs"
echo ""
echo "  3. Check simulation log file for DFS events"
echo "  4. Verify Kafka metrics show channel changes"
echo ""
echo "=========================================="
echo "Test message sent! Monitor simulation logs"
echo "=========================================="
