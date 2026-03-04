FROM ubuntu:18.04

# Avoid prompts during apt installations
ENV DEBIAN_FRONTEND=noninteractive

# Update and install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libmysqlclient-dev \
    libboost-system-dev \
    libboost-iostreams-dev \
    libboost-filesystem-dev \
    libpugixml-dev \
    libcrypto++-dev \
    libgmp3-dev \
    libluajit-5.1-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /server

# Copy all source files
COPY . .

# Compile the server
RUN mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

# Copy the generated binary to the main directory
RUN cp build/tfs .

# Expose ports
EXPOSE 7171 7172 7173

# Set the command to run the server
CMD ["./tfs"]
