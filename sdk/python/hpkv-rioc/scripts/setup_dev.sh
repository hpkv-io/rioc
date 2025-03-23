#!/bin/bash
set -e

# Copy native libraries
./scripts/copy_native_libs.sh

# Create virtual environment if it doesn't exist
if [ ! -d ".venv" ]; then
    python -m venv .venv
fi

# Activate virtual environment
source .venv/bin/activate

# Install dependencies
pip install -r requirements-dev.txt

# Install package in development mode
pip install -e .

echo "Development environment setup complete!"
echo "To activate the virtual environment, run: source venv/bin/activate"
echo ""
echo "Example usage:"
echo "1. Basic example:"
echo "   python examples/basic.py"
echo ""
echo "2. Batch operations example:"
echo "   python examples/batch.py"
echo ""
echo "3. mTLS example (uses default certs from api/rioc/certs):"
echo "   python examples/mtls.py"
echo ""
echo "   Or with custom certificates:"
echo "   python examples/mtls.py --client-cert /path/to/client.crt --client-key /path/to/client.key --ca-cert /path/to/ca.crt" 