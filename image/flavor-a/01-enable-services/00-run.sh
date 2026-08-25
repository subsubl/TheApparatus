#!/bin/bash -e

# Pi A flavor: only the Layer 1 autoloader service.
# Commands piped via stdin heredoc - canonical pi-gen chroot pattern.
pushd "${ROOTFS_DIR}" > /dev/null
on_chroot << 'EOF'
systemctl enable apparatus-player-a.service
systemctl disable getty@tty1.service > /dev/null 2>&1 || true
EOF
popd > /dev/null

echo "=== Pi A services enabled ==="