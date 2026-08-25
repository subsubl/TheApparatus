#!/bin/bash -e

echo "=== The Apparatus: installing playback stack ==="

# This stage is fully self-contained: the workflow bundles the playback
# stack into ./files/ before pi-gen runs, because the build container
# sees ONLY the stage directories - never the repository checkout.
STAGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FILES_DIR="${STAGE_DIR}/files"

install -v -d -m 2755 "${ROOTFS_DIR}/home/pi/media"
install -v -d -m 2755 "${ROOTFS_DIR}/home/pi/apparatus"

for f in media_autoloader.py player_a.py player_b.py mpv_daemon.py test_mpv_daemon.py; do
    if [ ! -f "${FILES_DIR}/${f}" ]; then
        echo "FATAL: ${FILES_DIR}/${f} missing (bundle step did not run?)"
        exit 1
    fi
    install -v -m 755 "${FILES_DIR}/${f}" "${ROOTFS_DIR}/home/pi/apparatus/${f}"
done

for u in apparatus-player-a.service apparatus-player-b.service apparatus-mpv-daemon.service; do
    install -v -m 644 "${FILES_DIR}/${u}" "${ROOTFS_DIR}/etc/systemd/system/${u}"
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

# Ownership fixup inside the chroot (stdin heredoc - canonical pi-gen pattern)
pushd "${ROOTFS_DIR}" > /dev/null
on_chroot << EOF
chown -R pi:pi /home/pi/media /home/pi/apparatus
EOF
popd > /dev/null

echo "=== Apparatus stack installed ==="