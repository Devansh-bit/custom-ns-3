# Kafka Producer Module for NS-3

## Overview

The **kafka-producer** module enables NS-3 WiFi simulations to stream performance metrics to Apache Kafka in real-time. This allows external systems (like Bayesian Optimizers) to consume simulation data and make optimization decisions.

## Architecture

```
NS-3 Simulator → KafkaProducer → Kafka Topic → External Consumer (BO)
                                 (simulator-metrics)
```

### Message Format

- **Topic**: `simulator-metrics`
- **Key**: Simulation ID (e.g., `sim-001`)
- **Value**: JSON containing AP and STA metrics
- **Update Rate**: 1 second (configurable)

## Prerequisites

### 1. Install librdkafka

The module requires librdkafka C/C++ client library:

```bash
sudo apt-get update
sudo apt-get install -y librdkafka-dev
```

Verify installation:
```bash
pkg-config --modversion rdkafka
```

### 2. Setup Kafka

Run the provided setup script:

```bash
bash kafka-setup.sh
```

This will:
- Start Zookeeper
- Start Kafka broker
- Create `simulator-metrics` topic
- Verify the setup

## Building the Module

After installing librdkafka:

```bash
./ns3 configure --enable-examples
./ns3 build
```

The kafka-producer module should compile successfully.

## Usage

### Basic Example

```cpp
#include "ns3/kafka-producer-helper.h"
#include "ns3/kafka-producer.h"

// Create node for Kafka producer
NodeContainer nodes;
nodes.Create(1);

// Install KafkaProducer
KafkaProducerHelper kafkaHelper("localhost:9092", "simulator-metrics", "sim-001");
kafkaHelper.SetUpdateInterval(Seconds(1.0));
ApplicationContainer apps = kafkaHelper.Install(nodes.Get(0));
apps.Start(Seconds(0.0));
apps.Stop(Seconds(100.0));

// Get producer instance
Ptr<KafkaProducer> producer = kafkaHelper.GetKafkaProducer(nodes.Get(0));

// Update AP metrics
ApMetrics apMetrics;
apMetrics.nodeId = 0;
apMetrics.bssid = Mac48Address("00:00:00:00:00:01");
apMetrics.channel = 36;
apMetrics.band = WIFI_PHY_BAND_5GHZ;
apMetrics.channelUtilization = 0.15;
apMetrics.throughputMbps = 54.3;
apMetrics.associatedClients = 3;
// ... set other fields

producer->UpdateApMetrics(apMetrics.bssid, apMetrics);

// Update STA metrics
StaMetrics staMetrics;
staMetrics.nodeId = 10;
staMetrics.macAddress = Mac48Address("aa:bb:cc:dd:ee:01");
staMetrics.currentBssid = Mac48Address("00:00:00:00:00:01");
staMetrics.isAssociated = true;
staMetrics.currentRssi = -65.0;
staMetrics.meanLatency = 12.5;
// ... set other fields

producer->UpdateStaMetrics(staMetrics.macAddress, staMetrics);
```

### Running the Example

```bash
./ns3 run kafka-producer-example
```

With custom parameters:
```bash
./ns3 run "kafka-producer-example --brokers=localhost:9092 --topic=simulator-metrics --simId=sim-001 --simTime=20.0"
```

## Testing with Python Consumer

### Install Python dependencies:

```bash
pip install kafka-python
```

### Run the consumer:

```bash
python3 kafka-consumer-test.py
```

The consumer will display metrics in real-time:

```
==================================================================
Message #1 | Simulation ID: sim-001
Timestamp: 1730739600 | Sim Time: 42.5s
==================================================================

📡 Access Points (2):
  AP 00:00:00:00:00:01:
    Node ID: 0
    Channel: 36 (BAND_5GHZ)
    Channel Utilization: 15.00%
    Associated Clients: 2
    Throughput: 54.3 Mbps

📱 Stations (3):
  STA aa:bb:cc:dd:ee:01:
    Node ID: 10
    Associated: True
    Current BSSID: 00:00:00:00:00:01
    RSSI: -65.0 dBm
    Latency: 12.5 ms
    Throughput: 45.2 Mbps
```

## Data Structures

### ApMetrics (Access Point)

**Flat Fields:**
- `nodeId`, `bssid`, `channel`, `band`
- `phyIdleTime`, `phyTxTime`, `phyRxTime`, `phyCcaBusyTime`
- `channelUtilization`
- `associatedClients`, `clientList`
- `bytesSent`, `bytesReceived`, `throughputMbps`

**Nested Objects:**
- `connectionMetrics`: Map of connection ID → ConnectionMetrics
- `clientLinkQuality`: Map of MAC → ClientLinkQuality

### StaMetrics (Station)

**Flat Fields:**
- `nodeId`, `macAddress`
- `currentBssid`, `currentChannel`, `currentBand`, `isAssociated`
- `currentRssi`, `currentSnr`, `retries`
- `meanLatency`, `jitter`, `packetCount`, `throughputMbps`

**Nested Objects:**
- `connectionMetrics`: Map of connection ID → ConnectionMetrics
- `discoveredNeighbors`: Array of NeighborInfo
- `scanResults`: Map of BSSID → RSSI
- `roamingHistory`: Array of RoamingEvent

See [FINAL_KAFKA_JSON.md](../../FINAL_KAFKA_JSON.md) for complete JSON structure.

## Integration with Existing Modules

The KafkaProducer is designed to work with your existing monitoring modules:

1. **wifi-cca-monitor** → Provides channel utilization, throughput
2. **latency-jitter-monitor** → Provides latency, jitter, connection metrics
3. **link-protocol-11k** → Provides link quality (RSSI, SNR, RCPI, RSNI)
4. **beacon-neighbor-protocol-11k** → Provides neighbor discovery
5. **wifi-rrm-roaming** → Provides scan results, roaming history

### Integration Pattern

```cpp
// In your simulation script
Ptr<KafkaProducer> kafkaProducer = /* get from helper */;

// Periodically collect and send metrics
Simulator::Schedule(Seconds(1.0), [&]() {
    // Collect from your monitoring modules
    ApMetrics apMetrics = CollectApMetricsFromMonitors();
    StaMetrics staMetrics = CollectStaMetricsFromMonitors();

    // Send to Kafka
    kafkaProducer->UpdateApMetrics(bssid, apMetrics);
    kafkaProducer->UpdateStaMetrics(macAddr, staMetrics);
});
```

## Configuration Attributes

### KafkaProducer Attributes

- **Brokers** (string): Kafka broker addresses
  - Default: `"localhost:9092"`
  - Example: `"broker1:9092,broker2:9092"`

- **Topic** (string): Kafka topic name
  - Default: `"simulator-metrics"`

- **SimulationId** (string): Simulation identifier (used as message key)
  - Default: `"sim-001"`

- **UpdateInterval** (Time): Interval between metric updates
  - Default: `1.0s`

## Troubleshooting

### Module doesn't compile

**Error**: `kafka-producer` in "Modules that cannot be built"

**Solution**: Install librdkafka-dev:
```bash
sudo apt-get install librdkafka-dev
./ns3 configure --enable-examples
```

### Cannot connect to Kafka

**Error**: Failed to create Kafka producer

**Solution**: Ensure Kafka is running:
```bash
pgrep -f kafka
pgrep -f zookeeper

# If not running:
bash kafka-setup.sh
```

### Messages not appearing in consumer

**Solution**: Check topic exists:
```bash
$KAFKA_HOME/bin/kafka-topics.sh --list --bootstrap-server localhost:9092
```

Verify messages are being sent:
```bash
$KAFKA_HOME/bin/kafka-console-consumer.sh --topic simulator-metrics --bootstrap-server localhost:9092 --from-beginning
```

## Performance

- **Message Size**: ~4.5 KB per message (2 APs, 3 STAs)
- **Update Rate**: 1 Hz → 4.5 KB/s → 270 KB/min → ~16 MB/hour
- **Overhead**: Minimal impact on simulation performance

## Next Steps (Phase 2)

1. Implement lever command consumer (BO → Simulator)
2. Add real-time optimization loop
3. Create dashboard for visualization
4. Add support for multiple simulation instances

## References

- [FINAL_KAFKA_JSON.md](../../FINAL_KAFKA_JSON.md) - Complete JSON specification
- [KAFKA_METRICS_STRUCTURE.md](../../KAFKA_METRICS_STRUCTURE.md) - Data structures
- [kafka-setup.sh](../../kafka-setup.sh) - Kafka initialization script
- [INSTALL_LIBRDKAFKA.md](../../INSTALL_LIBRDKAFKA.md) - Installation guide

## License

GNU General Public License v2
