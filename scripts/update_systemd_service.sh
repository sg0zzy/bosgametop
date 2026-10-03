#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SERVICE=bosgametop-fan-profile.service

die() { echo "Error: $*" >&2; exit 1; }

(( EUID == 0 )) || die "Run this script as root: sudo $0"
(( $# == 0 )) || die "Usage: sudo $0"
command -v make >/dev/null || die "make is missing. Install it before updating the service."
command -v systemctl >/dev/null || die "systemctl is missing. This script requires systemd."

echo "Installing the fan-profile helper and systemd unit"
make -C "$REPO_ROOT" install-fan-profile

systemctl daemon-reload
systemctl enable "$SERVICE"
# A oneshot service with RemainAfterExit=yes must be restarted to reapply updates.
systemctl restart "$SERVICE" || die "Could not apply the fan profile. Check journalctl -u $SERVICE -b."

echo "$SERVICE updated, enabled at boot, and applied"
