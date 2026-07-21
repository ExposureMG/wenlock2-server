# Stage 1: Build
FROM ubuntu:24.04 AS builder

# Install build dependencies
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Build the application
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --config Release -j8

# Stage 2: Runtime
FROM ubuntu:24.04

WORKDIR /app
COPY --from=builder /app/build/wenlock2-server /app/wenlock2-server
COPY --from=builder /app/www /app/www

EXPOSE 8080

CMD ["/app/wenlock2-server"]
