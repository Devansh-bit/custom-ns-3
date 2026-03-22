#!/bin/bash
#
# Kafka-based Spectrum + CNN Pipeline
#
# This replaces file-based cnn_from_trace.py with real-time Kafka streaming:
#   ns-3 simulation --> Kafka (spectrum-data) --> CNN consumer --> Kafka (cnn-predictions)
#
# Usage:
#   ./run-kafka-pipeline.sh [config.json]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Configuration
CONFIG_FILE="${1:-config-simulation.json}"
KAFKA_BROKERS="${KAFKA_BROKERS:-localhost:9092}"
SPECTRUM_TOPIC="spectrum-data"
CNN_TOPIC="cnn-predictions"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║           KAFKA-BASED SPECTRUM + CNN PIPELINE                ║"
echo "╠══════════════════════════════════════════════════════════════╣"
echo "║  Config: $CONFIG_FILE"
echo "║  Kafka:  $KAFKA_BROKERS"
echo "║  Topics: $SPECTRUM_TOPIC -> $CNN_TOPIC"
echo "╚══════════════════════════════════════════════════════════════╝"

# Check if Kafka is running
echo "[1/5] Checking Kafka..."
if ! nc -z localhost 9092 2>/dev/null; then
    echo "ERROR: Kafka not running on localhost:9092"
    echo "Start Kafka with: docker-compose -f docker-compose-kafka.yml up -d"
    exit 1
fi
echo "  ✓ Kafka is running"

# Create topics if they don't exist
echo "[2/5] Creating Kafka topics..."
docker exec -i kafka kafka-topics --create --if-not-exists \
    --bootstrap-server localhost:9092 \
    --topic "$SPECTRUM_TOPIC" \
    --partitions 3 \
    --replication-factor 1 2>/dev/null || true
docker exec -i kafka kafka-topics --create --if-not-exists \
    --bootstrap-server localhost:9092 \
    --topic "$CNN_TOPIC" \
    --partitions 3 \
    --replication-factor 1 2>/dev/null || true
echo "  ✓ Topics created"

# Python environment (assumes ns3-ml is already activated)
echo "[3/5] Checking Python environment..."
echo "  ✓ Using Python: $(which python3)"

# Clean old logs
echo "[4/5] Cleaning old logs..."
rm -rf curr_cnn_logs/*.json curr_cnn_logs/*.log 2>/dev/null || true
mkdir -p curr_cnn_logs spectrum-logs
echo "  ✓ Logs cleaned"

# Start CNN Kafka consumer in background
echo "[5/5] Starting CNN Kafka consumer..."
python3 RL/cnn_kafka_consumer.py \
    --brokers "$KAFKA_BROKERS" \
    --input-topic "$SPECTRUM_TOPIC" \
    --output-topic "$CNN_TOPIC" \
    --group-id "cnn-consumer-$(date +%s)" &
CNN_PID=$!
echo "  ✓ CNN consumer started (PID: $CNN_PID)"

# Give consumer time to initialize
sleep 2

# Run ns-3 simulation with Kafka streaming enabled
echo ""
echo "Starting ns-3 simulation with Kafka streaming..."
echo "═══════════════════════════════════════════════════════════════"

# Run spectrum-shadow simulation with Kafka enabled
./ns3 run "spectrum-shadow-sim --configFile=$CONFIG_FILE --enableKafka=true --kafkaBrokers=$KAFKA_BROKERS"

# Cleanup
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "Simulation complete. Stopping CNN consumer..."
kill $CNN_PID 2>/dev/null || true

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║                    PIPELINE COMPLETE                         ║"
echo "╠══════════════════════════════════════════════════════════════╣"
echo "║  CNN results saved to: curr_cnn_logs/CNN_*.json              ║"
echo "║  Also published to Kafka topic: $CNN_TOPIC"
echo "╚══════════════════════════════════════════════════════════════╝"

