#!/bin/bash -e

# PAL composite output via the Pi's 3.5 mm AV jack ("TV service" in Bookworm).
# Both players output PAL SD composite to the WJ-AVE5 (Pi 3/4 only - the Pi 5
# has NO composite output at all).
#
# config.txt must be edited on the HOST side (before boot), because the
# firmware reads it before any of our services exist.
CFG="${ROOTFS_DIR}/boot/firmware/config.txt"
if [ -f "${CFG}" ]; then
	# Enable composite output parameter on vc4-kms-v3d overlay
	sed -i 's/^dtoverlay=vc4-kms-v3d$/dtoverlay=vc4-kms-v3d,composite=1/' "${CFG}"
	sed -i 's/^enable_tvout=.*/enable_tvout=1/' "${CFG}"
	grep -q '^enable_tvout=' "${CFG}" || echo 'enable_tvout=1' >> "${CFG}"
	# Composite defaults to NTSC unless told otherwise; we are PAL here.
	sed -i 's/^sdtv_mode=.*/sdtv_mode=2/' "${CFG}"
	grep -q '^sdtv_mode=' "${CFG}" || echo 'sdtv_mode=2' >> "${CFG}"
	# SDTV aspect ratio: 1 = 4:3 for analog composite video outputs
	sed -i 's/^sdtv_aspect=.*/sdtv_aspect=1/' "${CFG}"
	grep -q '^sdtv_aspect=' "${CFG}" || echo 'sdtv_aspect=1' >> "${CFG}"
	# Disable overscan black borders on analog video signals
	sed -i 's/^disable_overscan=.*/disable_overscan=1/' "${CFG}"
	grep -q '^disable_overscan=' "${CFG}" || echo 'disable_overscan=1' >> "${CFG}"
	echo "config.txt: dtoverlay=vc4-kms-v3d,composite=1, enable_tvout=1, sdtv_mode=2 (PAL), sdtv_aspect=1 (4:3), disable_overscan=1"

fi

CMDLINE="${ROOTFS_DIR}/boot/firmware/cmdline.txt"
if [ -f "${CMDLINE}" ]; then
	grep -q 'video=Composite-1' "${CMDLINE}" || sed -i 's/$/ video=Composite-1:720x576i,vc4.tv_norm=PAL/' "${CMDLINE}"
	echo "cmdline.txt: video=Composite-1:720x576i,vc4.tv_norm=PAL appended"
fi

# Bookworm moved display config into /etc/xdg/lxsession (desktop) or the
# compositor; on Lite with no compositor the legacy wlr-randr/xset paths do
# not apply. Blank console + DPMS handled by cmdline (03-display-config) and
# by mpv keeping the display busy. Enable tvservice-style persistence:
mkdir -p "${ROOTFS_DIR}/var/lib/apparatus"
echo "pal" > "${ROOTFS_DIR}/var/lib/apparatus/tv_standard"
chown 1000:1000 "${ROOTFS_DIR}/var/lib/apparatus/tv_standard"

# Kiosk users get video group membership implicitly (already in base image);
# nothing else to do: mpv autodetects the TV service output when enabled.

cat << 'EOF'

=== The Apparatus: PAL composite enabled (3.5mm jack) ===

EOF