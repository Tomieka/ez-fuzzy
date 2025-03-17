#!/bin/bash

echo "Cleaning build directory..."

# Check if the build directory exists
if [ -d "build" ]; then
    rm -rf build
    echo "Build directory removed."
else
    echo "Build directory not found. Nothing to clean."
fi 