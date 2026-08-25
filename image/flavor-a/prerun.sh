#!/bin/bash -e

# Inherit the rootfs built by the previous stage (canonical pi-gen pattern).
if [ ! -d "${ROOTFS_DIR}" ]; then
	copy_previous
fi