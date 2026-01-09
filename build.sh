#!/bin/bash
# Build SpojBoard firmware locally

set -e

echo "Building SpojBoard firmware..."
pio run

if [ -d "dist" ] && [ "$(ls -A dist)" ]; then
    echo ""
    echo "✓ Build complete!"
    echo ""
    echo "Firmware files in dist/:"
    ls -lh dist/
    echo ""
else
    echo "⚠ Warning: No firmware files found in dist/"
fi
