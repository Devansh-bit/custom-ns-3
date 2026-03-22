#!/bin/bash
#
# Dual Simulation Launcher - TIME SYNCHRONIZED
#
# Runs both basic-simulation and spectrum-shadow-sim in parallel:
# - basic-simulation: Main simulation with YansWifiPhy (roaming, traffic, metrics)
# - spectrum-shadow-sim: Spectrum simulation with SpectrumWifiPhy (PSD data)
#
# Both simulations read the same config.json to ensure consistent setup.
#
# SYNCHRONIZATION:
#   - Both use ns-3 simulation time (not wall-clock time)
#   - Both read same simulationTime from config JSON
#   - Both start at simulation time 0
#   - Timestamps in .tr files are simulation time, enabling correlation
#   - When one sim stops (Ctrl+C or completion), BOTH stop immediately
#   - All .tr files close at the same simulation time with END marker
#
# Usage:
#   ./run-dual-simulation.sh config.json
#   ./run-dual-simulation.sh config.json --verbose
#

# Don't use set -e as it causes issues with background processes
# set -e

# Default values
CONFIG="${1:-config-simulation.json}"
VERBOSE="${2:-}"
SPECTRUM_OUTPUT="/tmp/ns3-spectrum"
LOG_OUTPUT="./spectrum-logs"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================"
echo "   DUAL SIMULATION LAUNCHER"
echo -e "========================================${NC}"
echo ""
echo -e "Config file: ${GREEN}$CONFIG${NC}"
echo -e "Spectrum output: ${GREEN}$SPECTRUM_OUTPUT${NC}"
echo -e "Log output: ${GREEN}$LOG_OUTPUT${NC}"
echo ""

# Check if config file exists
if [ ! -f "$CONFIG" ]; then
    echo -e "${RED}ERROR: Config file not found: $CONFIG${NC}"
    exit 1
fi

# Create output directories
mkdir -p "$SPECTRUM_OUTPUT"
mkdir -p "$LOG_OUTPUT"

# Create shared pipe for spectrum data (non-blocking)
SHARED_PIPE="$SPECTRUM_OUTPUT/spectrum.pipe"
rm -f "$SHARED_PIPE"
mkfifo "$SHARED_PIPE" 2>/dev/null || true

# Initialize sync files for bidirectional time coordination between simulations
# basic-simulation writes to ns3-sync-timestamp.txt
# spectrum-shadow writes to ns3-spectrum-sync-timestamp.txt
echo "0.0" > /tmp/ns3-sync-timestamp.txt
echo "0.0" > /tmp/ns3-spectrum-sync-timestamp.txt

# Build paths
MAIN_SIM="./build/contrib/final-simulation/examples/ns3.46.1-basic-simulation-optimized"
SPECTRUM_SIM="./build/contrib/spectrum-shadow/examples/ns3.46.1-spectrum-shadow-sim-optimized"

# Check if executables exist and build if needed
if [ ! -f "$MAIN_SIM" ]; then
    echo -e "${YELLOW}Building basic-simulation...${NC}"
    ./ns3 build basic-simulation
    if [ $? -ne 0 ]; then
        echo -e "${RED}ERROR: Failed to build basic-simulation${NC}"
        exit 1
    fi
fi

if [ ! -f "$SPECTRUM_SIM" ]; then
    echo -e "${YELLOW}Building spectrum-shadow-sim...${NC}"
    ./ns3 build spectrum-shadow-sim
    if [ $? -ne 0 ]; then
        echo -e "${RED}ERROR: Failed to build spectrum-shadow-sim${NC}"
        exit 1
    fi
fi

# Verify executables exist
if [ ! -f "$MAIN_SIM" ]; then
    echo -e "${RED}ERROR: Main simulation not found: $MAIN_SIM${NC}"
    exit 1
fi

if [ ! -f "$SPECTRUM_SIM" ]; then
    echo -e "${RED}ERROR: Spectrum simulation not found: $SPECTRUM_SIM${NC}"
    exit 1
fi

echo ""
echo -e "${BLUE}Starting simulations...${NC}"
echo ""

# Initialize PIDs
SPECTRUM_PID=""
MAIN_PID=""
SHUTDOWN_INITIATED=false

# Function to send stop signal to both simulations
stop_both() {
    if [ "$SHUTDOWN_INITIATED" = true ]; then
        return
    fi
    SHUTDOWN_INITIATED=true

    echo ""
    echo -e "${CYAN}[SYNC] Stopping both simulations...${NC}"

    # Send SIGTERM to both processes
    if [ -n "$MAIN_PID" ] && kill -0 $MAIN_PID 2>/dev/null; then
        kill -TERM $MAIN_PID 2>/dev/null
    fi
    if [ -n "$SPECTRUM_PID" ] && kill -0 $SPECTRUM_PID 2>/dev/null; then
        kill -TERM $SPECTRUM_PID 2>/dev/null
    fi
}

# Function to cleanup on exit
cleanup() {
    echo ""
    echo -e "${YELLOW}Cleaning up...${NC}"

    stop_both

    # Wait for processes to terminate gracefully (with timeout)
    for i in {1..10}; do
        STILL_RUNNING=false
        if [ -n "$SPECTRUM_PID" ] && kill -0 $SPECTRUM_PID 2>/dev/null; then
            STILL_RUNNING=true
        fi
        if [ -n "$MAIN_PID" ] && kill -0 $MAIN_PID 2>/dev/null; then
            STILL_RUNNING=true
        fi
        if [ "$STILL_RUNNING" = false ]; then
            break
        fi
        sleep 0.5
    done

    # Force kill if still running
    if [ -n "$MAIN_PID" ] && kill -0 $MAIN_PID 2>/dev/null; then
        echo "Force killing main simulation..."
        kill -9 $MAIN_PID 2>/dev/null || true
    fi
    if [ -n "$SPECTRUM_PID" ] && kill -0 $SPECTRUM_PID 2>/dev/null; then
        echo "Force killing spectrum simulation..."
        kill -9 $SPECTRUM_PID 2>/dev/null || true
    fi

    wait 2>/dev/null

    echo -e "${GREEN}Cleanup complete.${NC}"
}

trap cleanup EXIT
trap 'stop_both' INT TERM

# Build spectrum simulation arguments
SPECTRUM_ARGS="--configFile=$CONFIG --pipePath=$SPECTRUM_OUTPUT --logPath=$LOG_OUTPUT --enablePipes=true"
if [ "$VERBOSE" == "--verbose" ]; then
    SPECTRUM_ARGS="$SPECTRUM_ARGS --verbose=true"
fi

# Record start time
START_TIME=$(date +%s.%N)

# ========================================
# START BOTH SIMULATIONS
# ========================================
echo -e "${GREEN}[1/2] Starting spectrum-shadow-sim...${NC}"
$SPECTRUM_SIM $SPECTRUM_ARGS &
SPECTRUM_PID=$!
echo -e "      PID: ${BLUE}$SPECTRUM_PID${NC}"

# Small delay to let spectrum sim initialize pipes
sleep 0.5

echo -e "${GREEN}[2/2] Starting basic-simulation...${NC}"
$MAIN_SIM --config=$CONFIG &
MAIN_PID=$!
echo -e "      PID: ${BLUE}$MAIN_PID${NC}"

echo ""
echo -e "${BLUE}========================================"
echo "   BOTH SIMULATIONS RUNNING"
echo -e "========================================${NC}"
echo ""
echo "Main simulation PID: $MAIN_PID"
echo "Spectrum simulation PID: $SPECTRUM_PID"
echo ""
echo "Shared spectrum pipe:"
echo "  $SPECTRUM_OUTPUT/spectrum.pipe"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop both simulations.${NC}"
echo ""

# Monitor both processes - when either exits, stop both
while true; do
    # Check if main simulation exited
    if ! kill -0 $MAIN_PID 2>/dev/null; then
        wait $MAIN_PID 2>/dev/null
        MAIN_EXIT=$?
        echo ""
        echo -e "${YELLOW}Main simulation exited with code: $MAIN_EXIT${NC}"
        stop_both
        break
    fi

    # Check if spectrum simulation exited
    if ! kill -0 $SPECTRUM_PID 2>/dev/null; then
        wait $SPECTRUM_PID 2>/dev/null
        SPECTRUM_EXIT=$?
        echo ""
        echo -e "${YELLOW}Spectrum simulation exited with code: $SPECTRUM_EXIT${NC}"
        stop_both
        break
    fi

    sleep 0.5
done

# Wait for both to complete
wait $MAIN_PID 2>/dev/null
MAIN_EXIT=${MAIN_EXIT:-$?}
wait $SPECTRUM_PID 2>/dev/null
SPECTRUM_EXIT=${SPECTRUM_EXIT:-$?}

END_TIME=$(date +%s.%N)
DURATION=$(echo "$END_TIME - $START_TIME" | bc 2>/dev/null || echo "N/A")

echo ""
echo -e "${BLUE}========================================"
echo "   SIMULATION RESULTS"
echo -e "========================================${NC}"
echo ""
echo -e "Wall-clock duration: ${GREEN}${DURATION}s${NC}"
echo -e "Main simulation exit code: ${GREEN}$MAIN_EXIT${NC}"
echo -e "Spectrum simulation exit code: ${GREEN}$SPECTRUM_EXIT${NC}"
echo ""
echo "Spectrum outputs:"
echo "  Pipes: $SPECTRUM_OUTPUT/"
echo "  Logs: $LOG_OUTPUT/"
echo "  Annotations: spectrum-annotations.json"
echo ""

# Reset trap
trap - EXIT INT TERM

exit ${MAIN_EXIT:-0}
