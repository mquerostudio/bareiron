#!/usr/bin/env bash
# Build and start the server
set -e
./build.sh "$@"
./bareiron
