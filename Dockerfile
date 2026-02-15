# Build stage
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libcurl4-openssl-dev \
    libssl-dev \
    libsqlite3-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON
RUN cmake --build build --parallel

# Runtime stage
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libcurl4 \
    libssl3 \
    libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash ghostclaw
USER ghostclaw
WORKDIR /home/ghostclaw

COPY --from=builder /app/build/ghostclaw /usr/local/bin/ghostclaw

EXPOSE 8080
VOLUME ["/home/ghostclaw/.ghostclaw"]
ENV GHOSTCLAW_CONFIG_DIR=/home/ghostclaw/.ghostclaw

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s \
  CMD ["ghostclaw", "status"]

ENTRYPOINT ["ghostclaw"]
