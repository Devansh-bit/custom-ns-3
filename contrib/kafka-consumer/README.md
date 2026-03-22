# Kafka Consumer Module for NS-3

This module enables NS-3 simulations to consume optimization commands from Apache Kafka in real-time. It's designed to work with Bayesian Optimizers or other external controllers that send WiFi parameter updates via Kafka.

## Features

- Real-time consumption of optimization commands from Kafka
- Plug-and-play integration with any NS-3 simulation
- Configurable polling interval for low-latency updates
- Callback-based parameter delivery
- Automatic JSON parsing and validation
- Simulation ID filtering for multi-simulation environments

## Dependencies

- **librdkafka**: Apache Kafka C/C++ client library
- **nlohmann/json**: JSON parsing library (header-only)

### Install Dependencies

```bash
# Ubuntu/Debian
sudo apt-get install librdkafka-dev nlohmann-json3-dev

# Fedora/RHEL
sudo dnf install librdkafka-devel json-devel
```

## Building

The module is built automatically with NS-3:

```bash
./ns3 configure --enable-examples
./ns3 build
```

## Usage

### 1. Basic Installation

```cpp
#include "ns3/kafka-consumer-helper.h"

// Create a node for the consumer
NodeContainer consumerNode;
consumerNode.Create(1);

// Install Kafka consumer
KafkaConsumerHelper consumerHelper("localhost:9092", "optimization-commands");
consumerHelper.SetPollInterval(MilliSeconds(100));
ApplicationContainer apps = consumerHelper.Install(consumerNode.Get(0));
apps.Start(Seconds(0.0));
apps.Stop(Seconds(100.0));
```

### 2. Register Callback

```cpp
// Define callback function
void OnParametersReceived(std::string bssid, ApParameters params) {
    std::cout << "Received parameters for AP " << bssid << std::endl;
    std::cout << "  TxPower: " << params.txPowerStartDbm << " dBm" << std::endl;
    std::cout << "  Channel: " << +params.channelNumber << std::endl;
    // Apply parameters to simulation...
}

// Get consumer and register callback
Ptr<KafkaConsumer> consumer = consumerHelper.GetKafkaConsumer(consumerNode.Get(0));
consumer->SetParameterCallback(MakeCallback(&OnParametersReceived));
```

### 3. Integration with LeverApi

```cpp
// In your callback function
void ApplyParametersToLeverApi(std::string bssid, ApParameters params) {
    // Find AP node by BSSID
    Ptr<Node> apNode = FindApNodeByBssid(bssid);
    Ptr<LeverApi> leverApi = apNode->GetApplication(0)->GetObject<LeverApi>();

    // Apply parameters
    Ptr<LeverConfig> config = leverApi->GetConfig();
    config->SetTxPowerStart(params.txPowerStartDbm);
    config->SetTxPowerEnd(params.txPowerEndDbm);
    config->SetCcaEdThreshold(params.ccaEdThresholdDbm);
    config->SetRxSensitivity(params.rxSensitivityDbm);
    config->SetChannelNumber(params.channelNumber);
    config->SetChannelWidth(params.channelWidthMhz);
    config->SetBand(StringToBand(params.band));
    config->SetPrimary20Index(params.primary20Index);
}
```

## JSON Message Format

The module expects JSON messages in the following format:

```json
{
  "timestamp_unix": 1730739615,
  "simulation_id": "sim-001",
  "command_type": "UPDATE_AP_PARAMETERS",
  "ap_parameters": {
    "00:00:00:00:00:01": {
      "tx_power_start_dbm": 18.0,
      "tx_power_end_dbm": 18.0,
      "cca_ed_threshold_dbm": -80.0,
      "rx_sensitivity_dbm": -91.0,
      "channel_number": 36,
      "channel_width_mhz": 80,
      "band": "BAND_5GHZ",
      "primary_20_index": 0
    }
  }
}
```

### AP Parameters

| Parameter | Type | Description | Valid Values |
|-----------|------|-------------|--------------|
| `tx_power_start_dbm` | double | Minimum transmission power | 0.0 - 30.0 dBm |
| `tx_power_end_dbm` | double | Maximum transmission power | 0.0 - 30.0 dBm |
| `cca_ed_threshold_dbm` | double | CCA Energy Detection threshold | -100.0 to -60.0 dBm |
| `rx_sensitivity_dbm` | double | Receiver sensitivity | -100.0 to -80.0 dBm |
| `channel_number` | uint8_t | WiFi channel number | 1-14 (2.4GHz), 36-165 (5GHz) |
| `channel_width_mhz` | uint16_t | Channel width | 20, 40, 80, 160 |
| `band` | string | WiFi band | "BAND_2_4GHZ", "BAND_5GHZ", "BAND_6GHZ" |
| `primary_20_index` | uint8_t | Primary 20MHz index | 0-7 (depends on width) |

## Running the Example

### 1. Start Kafka Broker

```bash
docker-compose -f docker-compose-kafka.yml up -d
```

### 2. Run NS-3 Simulation

```bash
./ns3 run kafka-consumer-example
```

### 3. Send Parameters from Python

```bash
python3 test_producer.py --scenario single-ap
```

You should see the simulation receive and display the parameters.

## Configuration Options

### KafkaConsumerHelper Options

```cpp
// Default constructor
KafkaConsumerHelper helper();

// Custom configuration
KafkaConsumerHelper helper(
    "localhost:9092",           // Kafka brokers
    "optimization-commands",    // Topic name
    "ns3-consumer",             // Consumer group ID
    "sim-001"                   // Simulation ID
);

// Set polling interval (default: 100ms)
helper.SetPollInterval(MilliSeconds(50));
```

### KafkaConsumer Options

```cpp
consumer->SetBrokers("localhost:9092");
consumer->SetTopic("optimization-commands");
consumer->SetGroupId("ns3-consumer");
consumer->SetSimulationId("sim-001");
consumer->SetPollInterval(MilliSeconds(100));
```

## Architecture

```
Python Producer (Bayesian Optimizer)
    ↓
Kafka Broker (localhost:9092)
    ↓
KafkaConsumer (NS-3 Application)
    ↓ polls every 100ms
Parse JSON → OptimizationCommand
    ↓
Invoke user callback for each AP
    ↓
Apply parameters via LeverApi
    ↓
WiFi PHY/MAC parameters updated
```

## Troubleshooting

### Consumer not receiving messages

1. Check Kafka is running:
```bash
docker ps | grep kafka
```

2. Check topic exists:
```bash
docker exec ns3-kafka kafka-topics.sh --list --bootstrap-server localhost:9092
```

3. Check messages in topic:
```bash
docker exec ns3-kafka kafka-console-consumer.sh --bootstrap-server localhost:9092 --topic optimization-commands --from-beginning
```

### Build errors

1. Ensure librdkafka is installed:
```bash
pkg-config --modversion rdkafka++
```

2. Ensure nlohmann-json is installed:
```bash
ls /usr/include/nlohmann/json.hpp
```

## Related Modules

- **kafka-producer**: Sends simulation metrics to Kafka
- **lever-api**: Applies WiFi parameters to simulation nodes

## License

GNU General Public License version 2