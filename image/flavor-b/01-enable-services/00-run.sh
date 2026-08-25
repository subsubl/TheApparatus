#!/bin/bash -e

# Pi B flavor: master autoloader + ESP32 serial-link daemon.
# Commands piped via stdin heredoc - canonical pi-gen chroot pattern.
pushd "${ROOTFS_DIR}" > /dev/null
on_chroot << 'EOF'
systemctl enable apparatus-player-b.service apparatus-mpv-daemon.service
systemctl disable getty@tty1.service > /dev/null 2>&1 || true
EOF
popd > /dev/null

echo "=== Pi B services enabled ==="