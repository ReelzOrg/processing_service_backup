# Stage 1: Base for builders (contains common build tools)
# We use the drogon image to ensure compatibility with the framework
FROM drogonframework/drogon AS builder_base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    git \
    cmake \
    build-essential \
    wget \
    pkg-config \
    flex \
    bison \
    libtool \
    autoconf \
    automake \
    libboost-all-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Stage 2: Build Avro (Parallel)
FROM builder_base AS avro_build
WORKDIR /tmp
RUN wget https://downloads.apache.org/avro/avro-1.12.0/cpp/avro-cpp-1.12.0.tar.gz \
    && tar -xzf avro-cpp-1.12.0.tar.gz \
    && cd avro-cpp-1.12.0 \
    && ./build.sh install

# Stage 3: Build CppKafka (Parallel)
FROM builder_base AS cppkafka_build
RUN apt-get update && apt-get install -y librdkafka-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /tmp
RUN git clone https://github.com/mfontanini/cppkafka.git \
    && cd cppkafka && mkdir build && cd build \
    && cmake .. \
    && make -j$(nproc) \
    && make install

# Stage 4: Build AWS SDK (Parallel - S3 only)
FROM builder_base AS aws_build
WORKDIR /tmp
RUN git clone --recurse-submodules --depth 1 https://github.com/aws/aws-sdk-cpp \
    && cd aws-sdk-cpp \
    && cmake . -DBUILD_ONLY="core;s3" \
              -DCMAKE_BUILD_TYPE=Release \
              -DMINIMIZE_SIZE=ON \
              -DCMAKE_INSTALL_PREFIX=/usr/local \
              -DENABLE_TESTING=OFF \
    && make -j$(nproc) \
    && make install \
    && ldconfig \
    # Uncomment the code below to check if AWS headers are installed
#     && echo "=== Verifying AWS SDK installation ===" \
#     && ls -la /usr/local/include/aws/ \
#     && ls -la /usr/local/include/aws/core/utils/stream/ || echo "Stream headers not found!"

# Stage 5: Final Builder (Compiles the App)
FROM builder_base AS app_builder

# Install app-specific build dependencies
RUN apt-get update && apt-get install -y \
    libavcodec-dev \
    libavformat-dev \
    libavfilter-dev \
    libavdevice-dev \
    libswscale-dev \
    libfmt-dev \
    librdkafka-dev \
    libjansson-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy compiled artifacts from parallel builders
# Copy each to a temporary location first to avoid overwriting
COPY --from=avro_build /usr/local/lib /usr/local/lib
COPY --from=avro_build /usr/local/include /usr/local/include
COPY --from=avro_build /usr/local/bin /usr/local/bin

COPY --from=cppkafka_build /usr/local/lib /usr/local/lib
COPY --from=cppkafka_build /usr/local/include /usr/local/include
COPY --from=cppkafka_build /usr/local/bin /usr/local/bin

COPY --from=aws_build /usr/local/lib /usr/local/lib
COPY --from=aws_build /usr/local/include /usr/local/include
COPY --from=aws_build /usr/local/bin /usr/local/bin

RUN ldconfig

WORKDIR /app
COPY . .
RUN mkdir -p schemas/generated
RUN avrogencpp -i schemas/mediaProcessingSchema.avsc -o schemas/generated/MediaProcessingJob.hh -n xyz::virajdoshi::reelz

RUN mkdir -p build && cd build && \
    cmake -DCMAKE_PREFIX_PATH=/usr/local -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)

# Stage 6: Runtime
# We use the same base image as the builder to ensure glibc and shared library compatibility (Boost, JsonCpp, etc.)
FROM drogonframework/drogon AS runtime

# Install runtime dependencies that are NOT in the base image
RUN apt-get update && apt-get install -y \
    librdkafka1 \
    ffmpeg \
    libc-ares2 \
    libcurl4 \
    libjansson4 \
    libboost-filesystem1.74.0 \
    libboost-program-options1.74.0 \
    libboost-system1.74.0 \
    libboost-iostreams1.74.0 \
    libboost-thread1.74.0 \
    libboost-regex1.74.0 \
    && rm -rf /var/lib/apt/lists/*

# Copy all shared libraries from the builder's /usr/local/lib
# This includes Avro, AWS, CppKafka (Drogon is already in the base)
COPY --from=app_builder /usr/local/lib /usr/local/lib

# Copy the application binary
COPY --from=app_builder /app/build/processing_service_backup /usr/local/bin/processing_service_backup

# Copy configuration files for production
COPY --from=app_builder /app/config.jsonc /app/config.jsonc
COPY --from=app_builder /app/config.yaml /app/config.yaml
COPY --from=app_builder /app/.env /app/.env

RUN ldconfig
WORKDIR /app

# Expose port if needed
EXPOSE 5555

CMD ["processing_service_backup"]


## Windows (PowerShell)
# $env:DOCKER_BUILDKIT=1
# docker build -t media_processing .

# Run (Development)
# docker run --name mediaProcessingService -it --rm -v "${PWD}:/app" -p 5555:5555 media_processing

# Run (Development)
# docker run --name mediaProcessingService -it --rm -p 5555:5555 media_processing

