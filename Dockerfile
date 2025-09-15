# A multi-stage build allows us to keep the final image small
FROM drogonframework/drogon AS build

# Install dependencies and build tools. libserdes is used for avro
RUN apt-get update && apt-get install -y \
libboost-all-dev \
build-essential \
pkg-config \
libssl-dev \
librdkafka-dev \
ffmpeg \
libjansson-dev \
libcurl4-openssl-dev \
libavcodec-dev \
libavformat-dev \
libavfilter-dev \
libavdevice-dev \
libswscale-dev \
libtool \
autoconf \
automake \
wget \
flex \
bison \
git \
cmake \
&& rm -rf /var/lib/apt/lists/*

# Download and extract Avro C++ source
WORKDIR /tmp
RUN wget https://downloads.apache.org/avro/avro-1.12.0/c/avro-c-1.12.0.tar.gz \
&& tar -xzf avro-c-1.12.0.tar.gz \
&& cd avro-c-1.12.0 \
&& mkdir build \
&& cd build \
&& cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local \
&& make -j$(nproc) \
&& make install \
&& ldconfig \
&& rm -rf /tmp/avro-c-1.12.0 /tmp/avro-c-1.12.0.tar.gz

WORKDIR /tmp
RUN wget https://downloads.apache.org/avro/avro-1.12.0/cpp/avro-cpp-1.12.0.tar.gz \
&& tar -xzf avro-cpp-1.12.0.tar.gz \
&& cd avro-cpp-1.12.0 \
&& ./build.sh install
# && mkdir build \
# && cd build \
# && cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release \
# -DBUILD_SHARED_LIBS=ON \
# -DAVRO_BUILD_STATIC=OFF \
# -DAVRO_BUILD_SHARED=ON \
# -DAVRO_ENABLE_CPP=ON \
# -DAVRO_ENABLE_EXAMPLES=OFF \
# && make -j$(nproc) \
# && make install \
# && ldconfig \
# && rm -rf /tmp/avro-cpp-1.12.0 /tmp/avro-cpp-1.12.0.tar.gz

# Build and install Avro C++ (needed for proper CMake integration)
# RUN cd /tmp && \
# git clone --depth 1 https://github.com/apache/avro.git && \
# # Build Avro C first
# cd avro/lang/c && \
# mkdir build && cd build && \
# cmake .. -DCMAKE_BUILD_TYPE=Release && \
# make -j$(nproc) && \
# make install && \
# ldconfig && \

# # Build Avro C++
# cd ../../c++ && \
# mkdir build && cd build && \
# cmake .. -DCMAKE_BUILD_TYPE=Release && \
# make -j$(nproc) && \
# make install && \
# ldconfig

# Clone cppkafka and build/install it
RUN cd /tmp && \
git clone https://github.com/mfontanini/cppkafka.git && \
cd cppkafka && mkdir build && cd build && \
cmake .. && \
make && \
make install && \
ldconfig \
&& rm -rf /tmp/cppkafka /tmp/cppkafka.tar.gz

# Clone aws-sdk-cpp and build/install it (we only need s3)
# make -j$(nproc) is used to speed up the build process by using all available CPUs
RUN cd /tmp && \
git clone --recurse-submodules --depth 1 https://github.com/aws/aws-sdk-cpp && \
cd aws-sdk-cpp && \
cmake . -DBUILD_ONLY="s3" \
        -DCMAKE_BUILD_TYPE=Release \
        -DMINIMIZE_SIZE=ON && \
make -j$(nproc) && \
make install && \
ldconfig \
&& rm -rf /tmp/aws-sdk-cpp /tmp/aws-sdk-cpp.tar.gz

# RUN cd /tmp && \
# git clone https://github.com/confluentinc/libserdes.git && \
# cd libserdes && \
# ./configure && \
# make && \
# make install && \
# ldconfig

# Set work directory to /app for your project code
WORKDIR /app
COPY . .
RUN mkdir -p schemas/generated
RUN avrogencpp -i schemas/mediaProcessingSchema.avsc -o schemas/generated/MediaProcessingJob.hh -n xyz::virajdoshi::reelz

# # Run cmake and make in separate steps
# RUN cmake -DCMAKE_PREFIX_PATH=/usr/local .
# RUN make

# Build your Drogon application
RUN mkdir -p build && cd build && \
cmake -DCMAKE_PREFIX_PATH=/usr/local -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)

# Stage 2: Final (runnable) stage
FROM drogonframework/drogon

# Copy the build artifacts from the build stage
COPY --from=build /usr/local /usr/local
# COPY --from=build /app/build/processing_service_backup /app/processing_service_backup

WORKDIR /app
RUN mkdir -p /app/processing_service_backup
WORKDIR /app/processing_service_backup
CMD ["bash"]
# CMD ["./processing_service_backup"]   #Start the server

# # Expose the port for Drogon server
# EXPOSE 5555

# git clone this repository and run
## docker build -t processing_service_image .
# change processing_service_image to whatever you want your image to be called

# Then run:
## docker run --name my_processing_service -p 5555:5555 processing_service_image
## change my_processing_service to whatever you want your container to be called