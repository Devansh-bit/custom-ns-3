# Running the WiFi Optimization Simulation Platform

For a complete overview of all modules and their functionality, see [`MODULE_REFERENCE.md`](MODULE_REFERENCE.md).

## Prerequisites

- **Docker & Docker Compose** - For running Kafka
- **Python 3.10+** - For RL agent
- **ns-3 built** - Run `./ns3 configure && ./ns3 build` if not done
- **Node.js 18+** - For visualization frontend (optional)
- **librdkafka** - C++ Kafka library for ns-3 modules

---

## Quick Start

### Step 0: Install Dependencies

See [`requirements.txt`](requirements.txt) for complete dependency list including:
- System dependencies (librdkafka, pkg-config)
- Python packages (torch, kafka-python, fastapi, etc.)

**Quick install:**
```bash
# System deps (Ubuntu/Debian)
sudo apt-get install -y librdkafka-dev pkg-config rapidjson-dev

# Python deps
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

---

### Step 1: Start Kafka(in terminal 1)

```bash
docker-compose -f docker-compose-kafka.yml up -d
```
or 

```bash
docker compose -f docker-compose-kafka.yml up -d
```
Wait ~10 seconds for Kafka to initialize.

**Verify Kafka is running:**
```bash
docker ps | grep kafka
```

**Optional:** Access Kafka UI at http://localhost:8080

---

### Step 2: Run RL Agent(in terminal 2)

```bash
source venv/bin/activate
python RL/main.py
```

The RL agent will:
- Connect to Kafka broker at `localhost:9092`
- Listen for metrics from simulation
- Send optimization commands back

---

### Step 3: Run ns-3 Simulation(in terminal 3)

```bash
./ns3 run "basic-simulation --simTime=120 --enableKafka=true"
```

**Available Parameters:**
| Parameter | Default | Description |
|-----------|---------|-------------|
| `--simTime` | 120 | Simulation duration (seconds) |
| `--enableKafka` | true | Enable Kafka communication |
| `--nAps` | 4 | Number of Access Points |
| `--nStas` | 30 | Number of Stations |

---

## Optional: Visualization

### Start Backend Server

```bash
cd visualize-data/backend
source ../venv/bin/activate  # or use main venv
pip install -r requirements.txt
python server.py
```

Backend runs on http://localhost:3002

### Start Frontend

```bash
cd visualize-data/frontend
npm install
npm run dev
```

Frontend runs on http://localhost:3001

---

## Stopping Everything

```bash
# Stop Kafka
docker-compose -f docker-compose-kafka.yml down

# Stop Python processes
pkill -f "RL/main.py"
pkill -f "server.py"

# Stop simulation (if running)
pkill -f "basic-simulation"
```

---

## Ports Reference

| Service | Port | URL |
|---------|------|-----|
| Kafka Broker | 9092 | localhost:9092 |
| Kafka UI | 8080 | http://localhost:8080 |
| Visualization Backend | 3002 | http://localhost:3002 |
| Visualization Frontend | 3001 | http://localhost:3001 |

---

## Troubleshooting

### Kafka Connection Refused
```
Error: NoBrokersAvailable
```
**Solution:** Ensure Kafka is running: `docker-compose -f docker-compose-kafka.yml up -d`

### Module Not Found (torch, etc.)
```
ModuleNotFoundError: No module named 'torch'
```
**Solution:** Activate venv and install dependencies:
```bash
source venv/bin/activate
pip install -r requirements.txt
```

### Simulation Build Error
**Solution:** Rebuild ns-3:
```bash
./ns3 configure --enable-examples
./ns3 build
```

### kafka-producer/consumer module not built
```
Warning: librdkafka++ not found - kafka-producer module will not be built
```
**Solution:** Install librdkafka system library:
```bash
# Ubuntu/Debian
sudo apt-get install -y librdkafka-dev pkg-config

# Arch Linux
sudo pacman -S librdkafka

# macOS
brew install librdkafka pkg-config
```
Then rebuild ns-3: `./ns3 build`
