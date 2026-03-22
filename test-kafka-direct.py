#!/usr/bin/env python3
"""
Direct Kafka test to bypass backend API
Sends Force DFS command directly to simulator-commands topic
"""
import json
import sys

# Check if kafka-python is available
try:
    from kafka import KafkaProducer
    import time
    
    # Configuration
    KAFKA_BROKER = "localhost:9092"
    TOPIC = "simulator-commands"
    
    # Create message
    message = {
        "command": "FORCE_DFS",
        "simulationId": "test-manual",
        "timestamp": int(time.time() * 1000)
    }
    
    print("=" * 60)
    print("DIRECT KAFKA TEST - Force DFS")
    print("=" * 60)
    print(f"Topic: {TOPIC}")
    print(f"Message: {json.dumps(message, indent=2)}")
    print()
    
    # Create producer and send
    producer = KafkaProducer(
        bootstrap_servers=[KAFKA_BROKER],
        value_serializer=lambda v: json.dumps(v).encode('utf-8')
    )
    
    future = producer.send(TOPIC, value=message)
    result = future.get(timeout=5)
    
    print(f"✓ Message sent successfully!")
    print(f"  Partition: {result.partition}")
    print(f"  Offset: {result.offset}")
    print()
    print("Now check simulation logs at: /tmp/force-dfs-test.log")
    print("Look for: [COMMAND] Received: FORCE_DFS")
    
    producer.flush()
    producer.close()
    
except ImportError:
    print("kafka-python not installed. Using kafkacat instead...")
    import subprocess
    message_str = json.dumps({
        "command": "FORCE_DFS",
        "simulationId": "test-manual", 
        "timestamp": int(time.time() * 1000)
    })
    try:
        result = subprocess.run(
            ['/bin/bash', '-c', f'echo \'{message_str}\' | kafkacat -P -b localhost:9092 -t simulator-commands'],
            capture_output=True,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            print("✓ Message sent via kafkacat")
        else:
            print(f"✗ kafkacat failed: {result.stderr}")
    except Exception as e:
        print(f"✗ Error: {e}")
