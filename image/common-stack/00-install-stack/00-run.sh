#!/bin/bash -e

echo "=== The Apparatus: installing playback stack ==="

# Locate the repository root (marker: pi/media_autoloader.py).
# Primary: this stage lives in <repo>/image/common-stack/00-install-stack,
# so the repo root is three levels up from this script's directory.
# Fallbacks cover pi-gen copying/symlinking stages elsewhere.
SRC_ROOT=""
for c in \
    "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)" \
    "${PWD}" \
    /home/runner/work/*/*/ ; do
    if [ -f "${c%/}/pi/media_autoloader.py" ]; then
        SRC_ROOT="${c%/}"
        break
    fi
done
if [ -z "${SRC_ROOT}" ]; then
    echo "FATAL: could not locate repository root (pi/media_autoloader.py not found)"
    exit 1
fi
echo "Repository root: ${SRC_ROOT}"

install -v -d -m 2755 "${ROOTFS_DIR}/home/pi/media"
install -v -d -m 2755 "${ROOTFS_DIR}/home/pi/apparatus"

for f in media_autoloader.py player_a.py player_b.py mpv_daemon.py test_mpv_daemon.py; do
    install -v -m 755 "${SRC_ROOT}/pi/${f}" "${ROOTFS_DIR}/home/pi/apparatus/${f}"
done

for u in apparatus-player-a.service apparatus-player-b.service apparatus-mpv-daemon.service; do
    install -v -m 644 "${SRC_ROOT}/pi/systemd/${u}" "${ROOTFS_DIR}/etc/systemd/system/${u}"
done

cat > "${ROOTFS_DIR}/home/pi/media/PUT_VIDEOS_HERE.txt" << 'EOF'
The Apparatus - media folder (videolooper style)
================================================
Pi A scans this folder for:   layer1_loop*.mp4  (fallback: layer1*.mp4)
Pi B scans this folder for:   master_L2_L3*.mp4 (fallback: master*.mp4)

- Newest matching file wins. Replace the file while running and the
  player hot-swaps to it within ~10 seconds.
- Also supported: .mkv .mov .avi .ts
- Until a match is found, a gray placeholder card is displayed.
EOF

# Ownership fixup inside the chroot (canonical pi-gen pattern: helper in ROOTFS/tmp)
cat > "${ROOTFS_DIR}/tmp/apparatus_chown.sh" << 'EOF'
#!/bin/bash -e
chown -R pi:pi /home/pi/media /home/pi/apparatus
EOF
chmod +x "${ROOTFS_DIR}/tmp/apparatus_chown.sh"
pushd "${ROOTFS_DIR}" > /dev/null
on_chroot /tmp/apparatus_chown.sh
popd > /dev/null
rm -f "${ROOTFS_DIR}/tmp/apparatus_chown.sh"

echo "=== Apparatus stack installed ==="