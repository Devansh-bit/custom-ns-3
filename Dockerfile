# Use Ubuntu 22.04 as the base image
FROM ubuntu:24.04

# Set environment variables to prevent interactive prompts during installation
ENV DEBIAN_FRONTEND=noninteractive

# Install essential dependencies for ns-3 and librdkafka
RUN apt-get update
RUN apt-get install -y \
    g++ \
    python3 \
    python3-pip \
    cmake \
    ninja-build \
    git \
    librdkafka-dev \
    libsqlite3-dev \
    pkg-config \
    rapidjson-dev \
    && rm -rf /var/lib/apt/lists/*

# Create a non-root user
RUN useradd -ms /bin/bash ns3-user
USER ns3-user
WORKDIR /home/ns3-user

# Copy the ns-3 project files
COPY --chown=ns3-user:ns3-user . /home/ns3-user/ns3
WORKDIR /home/ns3-user/ns3

# Build the config-simulation module
RUN ./ns3 configure --build-profile=optimized --enable-examples --enable-tests --disable-python && \
    ./ns3 build basic-simulation

# Set the entrypoint to run the simulation
ENTRYPOINT ["./build/contrib/final-simulation/examples/ns3.46.1-basic-simulation-optimized"]
