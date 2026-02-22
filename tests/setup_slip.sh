#!/bin/bash
# Setup SLIP connection on macOS/Linux host for testing
#
# Usage: sudo ./setup_slip.sh [serial_device]
#
# Example:
#   sudo ./setup_slip.sh /dev/ttyUSB0      # Linux
#   sudo ./setup_slip.sh /dev/tty.usbserial # macOS

set -e

SERIAL_DEV="${1:-/dev/ttyUSB0}"
HOST_IP="192.168.7.1"
PORTFOLIO_IP="192.168.7.2"
BAUD="9600"

echo "=== Portfolio SLIP Setup ==="
echo "Serial device: $SERIAL_DEV"
echo "Host IP:       $HOST_IP"
echo "Portfolio IP:  $PORTFOLIO_IP"
echo "Baud rate:     $BAUD"
echo ""

# Check if device exists
if [ ! -e "$SERIAL_DEV" ]; then
    echo "Error: $SERIAL_DEV not found"
    echo "Available serial devices:"
    ls -la /dev/tty* 2>/dev/null | grep -E "(USB|usbserial|ACM)" || echo "  (none found)"
    exit 1
fi

# Kill any existing slattach
echo "Stopping any existing SLIP connections..."
pkill slattach 2>/dev/null || true
sleep 1

# Start slattach
echo "Starting SLIP on $SERIAL_DEV..."
slattach -s "$BAUD" -p slip "$SERIAL_DEV" &
SLATTACH_PID=$!
sleep 2

# Check if slattach is running
if ! kill -0 $SLATTACH_PID 2>/dev/null; then
    echo "Error: slattach failed to start"
    exit 1
fi

# Configure IP
echo "Configuring IP addresses..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    ifconfig sl0 "$HOST_IP" "$PORTFOLIO_IP" up
else
    # Linux
    ifconfig sl0 "$HOST_IP" pointopoint "$PORTFOLIO_IP" up
fi

echo ""
echo "=== SLIP link established ==="
echo "slattach PID: $SLATTACH_PID"
echo ""
echo "To test connectivity:"
echo "  ping $PORTFOLIO_IP"
echo ""
echo "To run tests:"
echo "  cd tests && pip install -r requirements.txt && pytest -v"
echo ""
echo "To stop SLIP:"
echo "  sudo pkill slattach"
