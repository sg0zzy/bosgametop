#!/usr/bin/env bash
set -euo pipefail

EC_BASE="/sys/class/ec_su_axb35"

RAMPUP="50,60,70,78,85"
RAMPDOWN="43,50,57,64,70"
MODE="curve"

if [[ ! -d "$EC_BASE" ]]; then
    echo "ec_su_axb35 interface not found: $EC_BASE" >&2
    exit 1
fi

for fan in fan1 fan2 fan3; do
    FAN="$EC_BASE/$fan"

    if [[ ! -d "$FAN" ]]; then
        echo "$fan not found, skipping"
        continue
    fi

    echo "$RAMPUP"   > "$FAN/rampup_curve"
    echo "$RAMPDOWN" > "$FAN/rampdown_curve"
    echo "$MODE"     > "$FAN/mode"

    echo "$fan:"
    echo "  ramp-up:   $(cat "$FAN/rampup_curve")"
    echo "  ramp-down: $(cat "$FAN/rampdown_curve")"
    echo "  mode:      $(cat "$FAN/mode")"
done