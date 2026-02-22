#!/bin/bash

set -e

echo "Compiling DOS executable using OpenWatcom..."
docker-compose run --rm openwatcom make

echo "Build complete!"
