#!/usr/bin/env python3
import json
import time
from kafka import KafkaProducer

# Configuration
KAFKA_BROKER = "localhost:9092"
TOPIC = "simulator-commands"
SIMULATION_ID = "test-force-dfs"

# Create message
message = {
    "command": "FORCE_DFS",
    "simulationId": SIMULATION_ID,
    "timestamp": int(time.time() * 1000)
}

print("=" * 50)
print("Sending Force DFS Command to Kafka")
print("=" * 50)
print(f"Broker: {KAFKA_BROKER}")
print(f"Topic: {TOPIC}")
print(f"Message: {json.dumps(message, indent=2)}")
print()

try:
    # Create producer
    producer = KafkaProducer(
        bootstrap_servers=[KAFKA_BROKER],
        value_serializer=lambda v: json.dumps(v).encode('utf-8')
    )
    
    # Send message
    future = producer.send(TOPIC, key=SIMULATION_ID.encode('utf-8'), value=message)
    result = future.get(timeout=5)
    
    print(f"✓ Message sent successfully!")
    print(f"  Partition: {result.partition}")
    print(f"  Offset: {result.offset}")
    
    producer.flush()
    producer.close()
    
except Exception as e:
    print(f"✗ Error sending message: {e}")
    exit(1)
