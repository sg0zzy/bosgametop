#!/usr/bin/env bash
set -euo pipefail

EC_BASE="/sys/class/ec_su_axb35"

# most aggressive considering than on 1-2 the fan is switched off
RAMPUP="30,37,45,53,65"
RAMPDOWN="22,32,42,50,55"

# aggressive and more noisy
#RAMPUP="45,52,58,64,70"
#RAMPDOWN="40,47,53,59,65"

# quieter and less aggressive
#RAMPUP="50,60,70,75,80"
#RAMPDOWN="40,48,55,62,68"

MODE="curve"

if [[ ! -d "$EC_BASE" ]]; then
    echo "ec_su_axb35 sysfs interface not found after module load: $EC_BASE (check dmesg or journalctl -k)" >&2
    exit 1
fi

HWMON_DIR=""
for name_file in /sys/class/hwmon/hwmon*/name; do
    [[ -f "$name_file" ]] || continue
    if [[ $(<"$name_file") == ec_su_axb35 ]]; then
        HWMON_DIR=${name_file%/name}
        break
    fi
done
if [[ -n "$HWMON_DIR" ]]; then
    echo "ec_su_axb35 hwmon device: $HWMON_DIR"
fi

if [[ ! -d "$EC_BASE/fan1" && ! -d "$EC_BASE/fan2" && ! -d "$EC_BASE/fan3" ]]; then
    echo "ec_su_axb35 sysfs interface has no fan devices under $EC_BASE" >&2
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
