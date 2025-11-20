# Development Workflow

## Setup

1. **Build the development image:**
   ```bash
   docker build -f Dockerfile.dev -t media_processing_dev .
   ```

   Or using docker-compose:
   ```bash
   docker-compose -f docker-compose.dev.yml build
   ```

## Development Workflow

### Option 1: Using Docker Compose (Recommended)

```bash
# Start the container
docker-compose -f docker-compose.dev.yml up -d

# Enter the container
docker exec -it media_processing_dev /bin/bash

# Inside the container, build your project
mkdir -p build && cd build
cmake -DCMAKE_PREFIX_PATH=/usr/local -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# Run your application
./processing_service_backup

# When done, stop the container
docker-compose -f docker-compose.dev.yml down
```

### Option 2: Using Docker Run

```bash
# Run the container with volume mount
docker run -it --rm \
  --name media_processing_dev \
  -v "${PWD}:/app" \
  -p 5555:5555 \
  media_processing_dev

# Inside the container, build and run as above
```

## Development Tips

1. **Your source code is mounted** - any changes you make on your host machine are immediately visible in the container
2. **Rebuild when needed** - after changing code, just run `make` in the `/app/build` directory
3. **Generate Avro schemas** - if you modify schemas:
   ```bash
   avrogencpp -i schemas/mediaProcessingSchema.avsc -o schemas/generated/MediaProcessingJob.hh -n xyz::virajdoshi::reelz
   ```

## Production Build

When ready for production, use the optimized `Dockerfile`:
```bash
docker build -t media_processing .
docker run --name mediaProcessingService -it --rm -p 5555:5555 media_processing
```
