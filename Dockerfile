# A multi-stage build allows us to keep the final image small
FROM drogonframework/drogon as build

# Install dependencies and build tools
RUN apt-get update && apt-get install -y \
libboost-all-dev \
build-essential \
pkg-config \
libssl-dev \
librdkafka-dev \
ffmpeg \
libavcodec-dev \
libavformat-dev \
libavfilter-dev \
libavdevice-dev \
libswscale-dev \
git \
cmake \
&& rm -rf /var/lib/apt/lists/*

# Clone cppkafka and build/install it
RUN cd /tmp && \
git clone https://github.com/mfontanini/cppkafka.git && \
cd cppkafka && mkdir build && cd build && \
cmake .. && \
make && \
make install && \
ldconfig

RUN cd /tmp && \
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp && \
cd aws-sdk-cpp && \
cmake . -DBUILD_ONLY="s3" && \
make && \
make install && \
ldconfig

# Set work directory to /app for your project code
WORKDIR /app
COPY . .

# Build your Drogon application
RUN cmake . && make

# Stage 2: Final (runnable) stage
FROM drogonframework/drogon

# Copy the build artifacts from the build stage
COPY --from=build /usr/local /usr/local
COPY --from=build /app/build/processing_service_backup /app/processing_service_backup

WORKDIR /app
CMD ["./processing_service_backup"]

# Expose the port for Drogon server
EXPOSE 5555

# git clone this repository and run
## docker build -t processing_service_image .
# change processing_service_image to whatever you want your image to be called

# Then run:
## docker run --name my_processing_service -p 5555:5555 processing_service_image
## change my_processing_service to whatever you want your container to be called