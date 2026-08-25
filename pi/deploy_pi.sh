#!/usr/bin/env bash
# The Apparatus - Pi deployment helper
# Copies playback stack + systemd units to a Pi over SSH.
#
# Usage:
#   ./deploy_pi.sh pi@192.168.1.50 b     # deploy Pi B (master + trigger watcher)
#   ./deploy_pi.sh pi@192.168.1.51 a     # deploy Pi A (layer 1 loop)
set -euo pipefail

TARGET=${1:?usage: ./deploy_pi.sh <user@host> <a|b>}
ROLE=${2:?usage: ./deploy_pi.sh <user@host> <a|b>}

APP_DIR="/home/$(basename "$TARGET" | cut -d@ -f1)/apparatus"
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)/pi"

echo "==> Deploying role '$ROLE' to $TARGET:$APP_DIR"
ssh "$TARGET" "mkdir -p $APP_DIR"

scp "$SCRIPT_DIR/player_common.py" "$SCRIPT_DIR/player_${ROLE}.py" "$TARGET:$APP_DIR/" 2>/dev/null || \
scp "$SCRIPT_DIR/player_${ROLE}.py" "$TARGET:$APP_DIR/"

if [ "$ROLE" = "b" ]; then
    scp "$SCRIPT_DIR/trigger_watcher.py" "$TARGET:$APP_DIR/"
    scp "$SCRIPT_DIR/systemd/apparatus-player-b.service" \
        "$SCRIPT_DIR/systemd/apparatus-trigger-watcher.service" "$TARGET:/tmp/"
    ssh -t "$TARGET" 'sudo mv /tmp/apparatus-*.service /etc/systemd/system/ && \
                      sudo systemctl daemon-reload && \
                      sudo systemctl enable --now apparatus-player-b.service apparatus-trigger-watcher.service'
else
    scp "$SCRIPT_DIR/systemd/apparatus-player-a.service" "$TARGET:/tmp/"
    ssh -t "$TARGET" 'sudo mv /tmp/apparatus-player-a.service /etc/systemd/system/ && \
                      sudo systemctl daemon-reload && \
                      sudo systemctl enable --now apparatus-player-a.service'
fi

echo "==> Done. Check with: ssh $TARGET systemctl status apparatus-*"