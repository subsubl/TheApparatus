#!/bin/bash -e

# UART0 stability for the ESP32 serial link (Pi B):
#  * primary DT: no bluetooth takeover of the PL011, console disabled
#  * keep Bluetooth on the mini-UART instead of disabling it entirely
#  * serial_console=off: our daemon owns /dev/serial0 exclusively

cat >> "${ROOTFS_DIR}/boot/firmware/config.txt" << 'EOF'

# --- The Apparatus: stable PL011 UART for ESP32 link ---
dtoverlay=disable-bt
enable_uart=1
EOF

touch "${ROOTFS_DIR}/boot/firmware/cmdline.txt"
sed -i 's/\bconsole=serial[0-9]*,\([0-9]*\)\b//g' "${ROOTFS_DIR}/boot/firmware/cmdline.txt"

echo "=== UART config applied ==="