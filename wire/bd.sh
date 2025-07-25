#!/usr/bin/env bash

set -e

echo "Building..."
./wire/build-d.sh

echo "Sending build to bot..."
./wire/deploy-d.sh
