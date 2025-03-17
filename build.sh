#!/bin/bash

# Exit on error
set -e

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Configure and build
cmake ..
make -j$(nproc)

echo "Build completed successfully!"
echo "Run the application with: ./ez-fuzzy" 