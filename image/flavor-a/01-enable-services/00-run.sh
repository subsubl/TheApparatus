#!/bin/bash -e

# Pi A flavor: only the Layer 1 autoloader service.
# Helper written into the TARGET /tmp so on_chroot can execute it.

cat > "${ROOTFS_DIR}/tmp/apparatus_enable.sh" << 'EOF'
#!/bin/bash -e
systemctl enable apparatus-player-a.service
systemctl disable getty@tty1.service > /dev/null 2>&1 || true
EOF
chmod +x "${ROOTFS_DIR}/tmp/apparatus_enable.sh"

pushd "${ROOTFS_DIR}" > /dev/null
on_chroot /tmp/apparatus_enable.sh
popd > /dev/null
rm -f "${ROOTFS_DIR}/tmp/apparatus_enable.sh"

echo "=== Pi A services enabled ==="