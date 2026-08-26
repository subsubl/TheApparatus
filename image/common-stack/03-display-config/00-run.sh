#!/bin/bash -e

# Kiosk display hardening (both Pis):
# - consoleblank=0 : never let the kernel blank the text console (Lite image
#   default blanks after 10 min; visible if mpv ever restarts mid-swap).
# Resolution stays auto-negotiated via EDID; if a venue display misbehaves,
# force it with e.g. `video=HDMI-A-1:1920x1080@60D` appended here manually
# (see wiki/Research-Notes.md -> Display blanking).
CMDLINE="${ROOTFS_DIR}/boot/firmware/cmdline.txt"
if [ -f "${CMDLINE}" ]; then
	sed -i 's/$/ consoleblank=0/' "${CMDLINE}"
	echo "cmdline.txt: consoleblank=0 appended"
else
	echo "WARNING: ${CMDLINE} not found; skipping consoleblank tweak"
fi
