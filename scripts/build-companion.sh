#!/bin/bash
set -e

cd "$(dirname "$0")/../apps/akira-companion"

echo "Building akira-companion..."
go build -o akira-companion .

echo ""
echo "Running akira-companion..."
exec ./akira-companion
