#!/bin/bash

# Check if the build directory exists
if [ ! -d "build" ]; then
    echo "Build directory not found. Running build script first..."
    ./build.sh
else
    # Check if the executable exists
    if [ ! -f "build/ez-fuzzy" ]; then
        echo "Executable not found. Running build script first..."
        ./build.sh
    fi
fi

# Run the application
cd build
./ez-fuzzy 