# Bayesian Optimizer Package for WiFi Network Optimization

**NOTE: This package structure represents a modular refactoring. The current active implementation is the standalone script `optuna_clean.py` in the project root.**

## Current Active Implementation

The production Bayesian optimizer is located at:
```
../optuna_clean.py
```

### Features in Current Implementation

- **Doubly-Robust Safety Gate**: Statistical confidence-based deployment with 80% CI and minimum 2% improvement threshold
- **EWMA Metrics Processing**: Per-client exponential weighted moving average (α=0.3) for metric stability
- **Per-AP Percentile Aggregation**: Calculates p50 throughput, p95 retry/loss/RTT per AP, then averages globally
- **Simulation Time Constraints**: Optimization window based on simulation clock time (6-9 hour window with 10X timescale)
- **Kafka Integration**: Publishes configurations to `optimization-commands` topic
- **REST API Integration**: Reports improvements to `/rrm` endpoint with DR metrics and per-AP statistics
- **Config Churn Analysis**: Tracks naive vs DR-gated deployment decisions
- **Propensity Score Estimation**: Uses trial history similarity instead of tiny TPE probabilities
- **Automatic Logging**: Tee logger writes to both console and timestamped output files

## Future: Modular Package Architecture

This BO_Package directory contains a cleaner, modular refactoring with:

- **Separation of Concerns**: Dedicated modules for optimization, metrics, safety, and integrations
- **Type-Safe Configuration**: Pydantic models for validation
- **Professional Logging**: Centralized logging setup
- **Testability**: Isolated components for unit testing

## Installation & Usage (Current Implementation)

### Prerequisites

- Python 3.8+
- Kafka broker running at `localhost:9092`
- NS-3 simulation writing logs to `output_logs/simulation_logs_*.json`
- API server at `http://localhost:8000` with `/rrm` endpoint (optional)

### Install Dependencies

```bash
pip install optuna numpy pandas kafka-python requests
```

### Running the Current Optimizer

```bash
# Basic usage with defaults (30 min study time, 5s evaluation window)
python3 optuna_clean.py

# Custom study time and evaluation window
python3 optuna_clean.py --study-time 60 --evaluation-window 10

# Custom API URL
python3 optuna_clean.py --api-url http://192.168.1.100:8000
```

### Current Implementation Parameters

- `--study-time`: Study duration in minutes (default: 30)
- `--evaluation-window`: Evaluation window in simulation seconds (default: 5)
- `--api-url`: Base URL for API calls (default: http://localhost:8000)

### How It Works

1. **Trial 0**: Runs baseline configuration from `config-simulation.json`
2. **Subsequent Trials**: Optuna suggests new configurations using TPE sampler
3. **Metric Collection**: Collects logs for evaluation window, applies per-client EWMA
4. **Objective Calculation**: Computes weighted objective from p50 throughput, p95 retry/loss/RTT
5. **DR Safety Gate**: Checks if improvement passes 80% confidence threshold (≥2% improvement)
6. **Deployment**: Only configs passing DR gate are reported to API and deployed to canary
7. **Time Constraint**: Optimization stops when simulation clock reaches END_TIME_HOUR (9:00)

### Configuration File

The optimizer reads `config-simulation.json` to extract:
- Number of APs (dynamically determined)
- Initial channels, TX power, OBSS-PD values
- Available scanning channels
- Simulation start time (clock-time)

### Key Implementation Details

**Objective Function Formula:**
```
objective = 0.35 × p50_throughput + 0.10 × (1 - p95_retry) +
            0.35 × (1 - p95_loss) + 0.20 × (1 - p95_rtt)
```

**Doubly-Robust Estimator:**
```
V_DR(a*) = G(s, a*) + (1/π(a*|s)) × (Y - G(s, a*))

Where:
- G(s, a*) = Outcome model (historical mean)
- π(a*|s) = Propensity score (trial similarity: 0.1 + 0.9 × avg_similarity)
- Y = Actual observed outcome
```

**Parameter Search Space:**
- Channels: Available 5GHz channels from config (e.g., 36, 40, 44, 48, ...)
- TX Power: 13.0 to 25.0 dBm (continuous)
- OBSS-PD: -80.0 to -60.0 dBm (continuous)

**Network Settling Time:**
- Trial 0: 60 seconds (network initialization)
- Subsequent trials: 18 seconds (channel switch recovery + STA reassociation)

---

## BO_Package: Modular Refactoring (Future)

### Installation

```bash
pip install -r BO_Package/requirements.txt
```

### Usage

```bash
# Run with 6 APs (number must match config file)
python BO_Package/main.py --num-aps 6

# Custom settings
python BO_Package/main.py \
    --num-aps 6 \
    --study-time 10000 \
    --evaluation-window 5 \
    --settling-time 60 \
    --config-file config-simulation.json \
    --kafka-broker localhost:9092 \
    --api-url http://localhost:8000 \
    --log-level INFO
```

### Command Line Arguments

#### Required

- `--num-aps`: Number of APs in config file (defines search space size)

#### Optional

- `--study-time`: Study duration in minutes (default: 10000)
- `--evaluation-window`: Evaluation window in sim seconds (default: 5)
- `--settling-time`: Network settling time for Trial 1 in real seconds (default: 60)
- `--config-file`: Path to simulation config (default: config-simulation.json)
- `--log-dir`: Directory for logs (default: output_logs)
- `--kafka-broker`: Kafka broker address (default: localhost:9092)
- `--api-url`: API base URL (default: http://localhost:8000)
- `--optuna-seed`: Optuna random seed (default: 42)
- `--log-level`: Logging level (default: INFO)

## BO_Package Structure (Modular Refactoring)

```
BO_Package/
├── __init__.py                 # Package initialization
├── main.py                     # CLI entry point
├── README.md                   # This file
├── requirements.txt            # Python dependencies
│
├── core/                       # Core optimization logic
│   ├── __init__.py
│   ├── optimizer.py            # Main NetworkOptimizer orchestrator
│   └── objective.py            # Objective function calculation
│
├── config/                     # Configuration management
│   ├── __init__.py
│   ├── manager.py              # Config loading & validation
│   └── models.py               # Config data models (Pydantic)
│
├── metrics/                    # Metrics collection & processing
│   ├── __init__.py
│   ├── collector.py            # Log collection & parsing
│   ├── processor.py            # EWMA & percentile calculations
│   └── models.py               # Metric data models
│
├── safety/                     # Doubly-robust safety gate
│   ├── __init__.py
│   ├── propensity.py           # Propensity score calculation
│   └── doubly_robust.py        # DR estimation & safety gate
│
├── integrations/               # External system integrations
│   ├── __init__.py
│   ├── kafka_client.py         # Kafka producer wrapper
│   └── api_client.py           # REST API client
│
└── utils/                      # Utility functions
    ├── __init__.py
    ├── logger.py               # Centralized logging setup
    └── time_utils.py           # Simulation time conversions
```

---

## System Architecture

### Data Flow

```
NS-3 Simulation
    ↓
bayesian_optimiser_consumer.py  (Kafka consumer)
    ↓ writes metrics
output_logs/simulation_logs_*.json
    ↓ reads
optuna_clean.py  (Current optimizer)
    ↓ publishes configs
Kafka Topic: optimization-commands
    ↓ consumes
NS-3 Simulation  (applies new AP parameters)
    ↓ reports improvements
API Server (/rrm endpoint)
```

### Current Implementation vs BO_Package

| Aspect | optuna_clean.py | BO_Package |
|--------|-----------------|------------|
| **Architecture** | Monolithic single file | Modular package structure |
| **Lines of Code** | ~1750 lines | Distributed across modules |
| **Configuration** | Hardcoded constants | Pydantic models with validation |
| **Logging** | TeeLogger class | Professional logging module |
| **Testability** | Difficult (monolithic) | Easy (isolated modules) |
| **Maintainability** | Lower (all in one file) | Higher (separation of concerns) |
| **Status** | **Active/Production** | Future/Refactoring |

---

## Key Algorithms & Methodology

### 1. EWMA State Tracking

Per-client EWMA smoothing with α=0.3:
```python
state[metric] = α × current_value + (1 - α) × state[metric]
```

Applied to: throughput_uplink, throughput_downlink, packet_loss_rate, retry_rate, tcp_rtt

### 2. Per-AP Percentile Aggregation

1. Group clients by AP based on current BSSID
2. Extract final EWMA state for each client
3. Calculate percentiles per AP:
   - p50 throughput
   - p95 retry rate
   - p95 loss rate
   - p95 RTT
4. Average percentiles across all APs for global metrics

### 3. Doubly-Robust Safety Gate

**Propensity Score (π):**
```python
similarity = (channel_sim + tx_sim + obss_sim) / 3.0
propensity = 0.1 + 0.9 × avg_similarity_to_history
```

**DR Estimator:**
```python
G_prediction = mean(historical_objectives)
importance_weight = 1.0 / propensity
prediction_error = Y_actual - G_prediction
dr_estimate = G_prediction + importance_weight × prediction_error
```

**Confidence Bounds (80% CI):**
```python
adjusted_std = hist_std × importance_weight
lower_bound = dr_estimate - 1.28 × adjusted_std
```

**Deployment Decision:**
```python
should_deploy = (lower_bound >= 0.02)  # 2% minimum improvement
```

### 4. Time Constraint Checking

Simulation clock time calculation (10X timescale):
```python
clock_seconds = START_TIME_HOUR × 3600 + (sim_time × 10)
clock_hour = (clock_seconds // 3600) % 24
optimization_active = START_TIME_HOUR ≤ clock_hour < END_TIME_HOUR
```

---

## Troubleshooting

### Common Issues

**No logs collected:**
- Check that `bayesian_optimiser_consumer.py` is running
- Verify NS-3 simulation is publishing metrics to Kafka
- Check `output_logs/` directory for `simulation_logs_*.json`

**Kafka connection error:**
- Verify Kafka broker is running: `sudo systemctl status kafka`
- Check broker address matches (default: `localhost:9092`)
- Test Kafka topics: `kafka-topics --list --bootstrap-server localhost:9092`

**API connection error:**
- Check API server is running at specified URL
- Verify `/rrm` endpoint exists and accepts POST requests
- API errors are non-fatal - optimizer continues without API reporting

**Time constraint exceeded immediately:**
- Check simulation clock time in logs
- Verify `START_TIME_HOUR` and `END_TIME_HOUR` match your simulation schedule
- Adjust time window in optuna_clean.py if needed (lines 48-49)

**DR gate blocks all improvements:**
- Lower `dr_min_improvement` threshold (line 76: currently 0.02 = 2%)
- Increase `dr_confidence_threshold` (line 74: currently 0.95 = 95%)
- Check propensity scores - very low values indicate novel configurations

---

## Metrics Reference

### Objective Function Weights

| Component | Weight | Metric | Range |
|-----------|--------|--------|-------|
| Throughput | 0.35 | p50 total throughput | 0-100 Mbps |
| Retry Rate | 0.10 | p95 MAC retry rate | 0-5 (normalized) |
| Loss Rate | 0.35 | p95 packet loss rate | 0-1 (0-100%) |
| RTT | 0.20 | p95 TCP RTT | 0-750 ms |

### Log File Outputs

**Optimization logs:**
- Location: `output_logs/optuna_optimization_06h_to_09h_YYYYMMDD_HHMMSS.txt`
- Contains: Trial-by-trial progress, DR gate decisions, final summary

**Simulation logs:**
- Location: `output_logs/simulation_logs_*.json`
- Format: One JSON object per line (newline-delimited)
- Contains: Per-client metrics, AP-level stats, simulation time

---

## Migration Path: optuna_clean.py → BO_Package

To migrate from the current monolithic implementation to the modular package:

1. **Phase 1 - Extraction:**
   - Move NetworkOptimizer class methods to appropriate modules
   - Extract configuration loading to config/manager.py
   - Move EWMA/percentile logic to metrics/processor.py
   - Move DR estimator to safety/doubly_robust.py

2. **Phase 2 - Refactoring:**
   - Replace hardcoded constants with Pydantic config models
   - Replace TeeLogger with professional logging module
   - Add unit tests for each module

3. **Phase 3 - Validation:**
   - Run parallel testing (both implementations)
   - Compare objective values and DR decisions
   - Verify identical Kafka/API behavior

4. **Phase 4 - Cutover:**
   - Update documentation and scripts
   - Archive optuna_clean.py
   - Deploy BO_Package as primary implementation

---

## Contributing

When adding features to either implementation:

1. Update this README with new parameters/features
2. Add docstrings following Google style
3. Test with actual NS-3 simulation before committing
4. Update version tracking in improvement reports

## License

Internal research project - see project documentation for licensing details.
