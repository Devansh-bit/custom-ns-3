# WiFi Network Visualization

Real-time visualization of WiFi network topology from ns-3 simulation via Kafka.

## Features

- **Smooth animations** - Position interpolation using lerp for fluid movement
- **Modern UI** - Dark theme with glassmorphism panels
- **Real-time updates** - WebSocket-based data streaming
- **Hover tooltips** - Detailed metrics on hover (RSSI, throughput, latency, etc.)
- **Channel color coding** - Easy identification of AP channels

## Prerequisites

- Node.js 18+
- Python 3.8+
- Kafka broker running (default: localhost:9092)
- ns-3 simulation with kafka-producer enabled

## Quick Start

```bash
# Terminal 1: Start Kafka (if not running)
docker-compose -f docker-compose-kafka.yml up -d

# Terminal 2: Start the WebSocket backend (port 3000)
cd visualize-data/backend
pip install -r requirements.txt
python server.py

# Terminal 3: Start the Next.js frontend (port 3001)
cd visualize-data/frontend
npm install
npm run dev

# Terminal 4: Run the ns-3 simulation
./ns3 run "basic-simulation --simulationTime=300"

# Browser: Open http://localhost:3001
```

## Configuration

### Backend (`backend/server.py`)
- `KAFKA_BROKER`: Kafka broker address (default: `localhost:9092`)
- `METRICS_TOPIC`: Kafka topic for metrics (default: `ns3-metrics`)
- `SERVER_PORT`: WebSocket server port (default: `3000`)

### Frontend (`frontend/hooks/useWebSocket.ts`)
- `WS_URL`: WebSocket URL (default: `ws://localhost:3000/ws`)

## Architecture

```
ns-3 Simulation → Kafka (ns3-metrics) → Python Backend (port 3000)
                                              ↓ WebSocket
                                        Next.js Frontend (port 3001)
```

## File Structure

```
visualize-data/
├── backend/
│   ├── server.py          # FastAPI WebSocket server
│   └── requirements.txt   # Python dependencies
├── frontend/
│   ├── app/               # Next.js app directory
│   ├── components/        # React components
│   ├── hooks/             # Custom React hooks
│   ├── types/             # TypeScript interfaces
│   └── package.json       # Node dependencies
└── README.md
```
