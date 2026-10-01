#!/usr/bin/env bash
set -euo pipefail

MODULE=ec_su_axb35
VERSION=1.0
KERNEL=$(uname -r)
REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
CONFIG="$REPO_ROOT/packaging/dkms/$MODULE/dkms.conf"
DEST="/usr/src/$MODULE-$VERSION"

die() { echo "Error: $*" >&2; exit 1; }

(( EUID == 0 )) || die "Run this installer as root, for example: sudo $0 [source-directory]"
(( $# <= 1 )) || die "Usage: sudo $0 [path-to-ec-su_axb35-linux]"

# With sudo, prefer the invoking user's checkout over root's home directory.
SOURCE_OWNER=${SUDO_USER:-root}
SOURCE_HOME=$(getent passwd "$SOURCE_OWNER" | cut -d: -f6)
DEFAULT_SOURCE="$SOURCE_HOME/code/ec-su_axb35-linux"
SOURCE=$(realpath -e -- "${1:-$DEFAULT_SOURCE}") || die "Module source not found. Pass its directory as the first argument."
[[ -d "$SOURCE" ]] || die "Module source is not a directory: $SOURCE"
[[ -f "$SOURCE/Makefile" ]] || die "Module source has no Makefile: $SOURCE"
[[ "$SOURCE" != "$DEST" ]] || die "Source and DKMS destination must differ: $DEST"

command -v dkms >/dev/null || die "dkms is missing. Install it with: apt install dkms"
command -v make >/dev/null || die "make is missing. Install build-essential with: apt install build-essential"
command -v gcc >/dev/null || die "gcc is missing. Install build-essential with: apt install build-essential"
dpkg-query -W -f='${Status}' build-essential 2>/dev/null | grep -qx 'install ok installed' || die "build-essential is missing. Install it with: apt install build-essential"
dpkg-query -W -f='${Status}' "linux-headers-$KERNEL" 2>/dev/null | grep -qx 'install ok installed' || die "Kernel headers are missing. Install: apt install linux-headers-$KERNEL"
[[ -f "/lib/modules/$KERNEL/build/Makefile" ]] || die "Kernel build tree is missing at /lib/modules/$KERNEL/build. Reinstall linux-headers-$KERNEL."
[[ -f "$CONFIG" ]] || die "DKMS configuration missing: $CONFIG"

echo "Installing $MODULE $VERSION from $SOURCE for kernel $KERNEL"
if [[ -n $(dkms status -m "$MODULE" -v "$VERSION") ]]; then
    dkms remove -m "$MODULE" -v "$VERSION" --all || die "Could not remove the previous DKMS registration for $MODULE $VERSION"
fi

rm -rf -- "$DEST"
install -d -- "$DEST"
cp -a -- "$SOURCE/." "$DEST/"
install -m 644 -- "$CONFIG" "$DEST/dkms.conf"

dkms add -m "$MODULE" -v "$VERSION" || die "DKMS could not register $MODULE $VERSION"
dkms build -m "$MODULE" -v "$VERSION" -k "$KERNEL" || die "DKMS build failed for kernel $KERNEL. Check /var/lib/dkms/$MODULE/$VERSION/build/make.log"
dkms install -m "$MODULE" -v "$VERSION" -k "$KERNEL" || die "DKMS install failed for kernel $KERNEL"
depmod -a "$KERNEL" || die "depmod failed for kernel $KERNEL"
modprobe "$MODULE" || die "modprobe $MODULE failed. Check dmesg and journalctl -k."
[[ -d /sys/module/$MODULE ]] || die "$MODULE is not loaded after modprobe"

if [[ ! -d /sys/class/ec_su_axb35 ]]; then
    die "$MODULE is loaded, but /sys/class/ec_su_axb35 is missing. Check dmesg and journalctl -k."
fi

HWMON_DIR=
for name_file in /sys/class/hwmon/hwmon*/name; do
    [[ -f "$name_file" ]] || continue
    if [[ $(<"$name_file") == "$MODULE" ]]; then
        HWMON_DIR=${name_file%/name}
        break
    fi
done
if [[ -n "$HWMON_DIR" ]]; then
    echo "Module hwmon device: $HWMON_DIR"
else
    echo "No $MODULE hwmon device found; fan controls are available at /sys/class/ec_su_axb35"
fi

echo "DKMS status:"
dkms status -m "$MODULE"
echo "Module information:"
modinfo "$MODULE" || die "modinfo $MODULE failed after installation"
echo "$MODULE is loaded (see /sys/module/$MODULE)"
