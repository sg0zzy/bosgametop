# bgtop

A lightweight TUI hardware and load monitor tailored for BOSGAME systems based on the AMD Ryzen AI Max+ 395 / AXB35 platform.

`bgtop` reads hardware information directly from Linux `/proc` and `/sys`, with minimal overhead and no heavy monitoring framework.

It is designed primarily for AI workloads, where standard tools such as `htop` or `btop` may not provide a clear view of GPU load, AMD unified memory allocation, and the BOSGAME embedded-controller fan state.

## Features

Currently displays:

* Total CPU utilization
* CPU temperature (`k10temp`)
* AMD GPU utilization
* AMD GPU temperature
* System memory usage
* GPU VRAM usage
* GPU GTT usage
* NVMe temperature
* Fan RPM
* Fan level
* Fan operating mode
* Fan ramp-up curve
* Fan ramp-down curve

The interface updates in real time using `ncurses`.

## Example

```text
BOSGAME AI395 MONITOR

CPU   [######################        ]   73.4%   81.2 C
GPU   [##############################]   99%     83.0 C

MEMORY
  System :   8.4 / 31.2 GiB
  VRAM   :  74.2 / 96.0 GiB
  GTT    :   2.1 / 15.5 GiB

TEMPERATURES
  CPU Tctl : 81.2 C
  GPU edge : 83.0 C
  NVMe     : 54.9 C

FANS
          FAN1                FAN2                FAN3
RPM       2782                2982                1451
Level     5                   5                   5
Mode      curve               curve              curve
Up        45,52,58,64,70      45,52,58,64,70     45,52,58,64,70
Down      40,47,53,59,65      40,47,53,59,65     40,47,53,59,65
```

## Requirements

### Build requirements

A C compiler and ncurses development headers are required.

On Debian / Ubuntu:

```bash
sudo apt install build-essential libncurses-dev
```

### Runtime requirements

The main monitoring functions rely on standard Linux kernel interfaces.

Required or recommended kernel support:

* Linux `/proc` and `/sys`
* AMDGPU kernel driver
* `k10temp` for CPU temperature
* AMDGPU hwmon support for GPU temperature
* DRM/sysfs AMD GPU statistics

GPU statistics are read from interfaces such as:

```text
/sys/class/drm/card*/device/gpu_busy_percent
/sys/class/drm/card*/device/mem_info_vram_used
/sys/class/drm/card*/device/mem_info_vram_total
/sys/class/drm/card*/device/mem_info_gtt_used
/sys/class/drm/card*/device/mem_info_gtt_total
```

## BOSGAME / AXB35 fan monitoring

Fan monitoring requires the `ec_su_axb35` kernel module.

This driver exposes the Sixunited AXB35-02 embedded controller through:

```text
/sys/class/ec_su_axb35/
```

and provides fan information such as:

```text
fan1/rpm
fan1/level
fan1/mode
fan1/rampup_curve
fan1/rampdown_curve
```

with equivalent entries for the other fans.

Check whether the module is available:

```bash
modinfo ec_su_axb35
```

Check whether it is loaded:

```bash
lsmod | grep ec_su_axb35
```

Load it manually:

```bash
sudo modprobe ec_su_axb35
```

To load it automatically at boot:

```bash
echo ec_su_axb35 | sudo tee /etc/modules-load.d/ec_su_axb35.conf
```

The rest of `bgtop` can still operate without `ec_su_axb35`; fan-related information will simply be unavailable.

## Kernel module / DKMS

The `ec_su_axb35` driver is an out-of-tree module. A manual build is installed for one kernel only; DKMS registers its source and rebuilds it automatically when a new kernel is installed.

On Debian, install the build dependencies and headers for the running kernel:

```bash
sudo apt install dkms build-essential "linux-headers-$(uname -r)"
```

The upstream driver source is a separate checkout. From this repository, install it with:

```bash
sudo ./scripts/install-ec-su-dkms.sh /path/to/ec-su_axb35-linux
```

If the path is omitted, the installer uses `~/code/ec-su_axb35-linux` for the user who invoked `sudo`. It copies the source and this repository's `dkms.conf` to `/usr/src/ec_su_axb35-1.0`, registers DKMS, builds and installs the module for the running kernel, then loads it. The installer can be run again after changing the driver source. Keep this repository available when rerunning it.

This installer only manages the kernel module. Run `scripts/apply-fan-profile.sh` to apply the fan profile once, or `scripts/update_systemd_service.sh` to install or update the service that applies it at boot (see below).

Verify the installation:

```bash
dkms status
modinfo ec_su_axb35
lsmod | grep ec_su_axb35
```

After a kernel update, DKMS should build the module for the new kernel automatically. Before rebooting, check `dkms status` and ensure headers for the new kernel are installed.

If the module is missing after an update, check the status and build log, then rebuild for the running kernel:

```bash
dkms status
sudo cat /var/lib/dkms/ec_su_axb35/1.0/build/make.log
sudo dkms autoinstall -k "$(uname -r)"
sudo modprobe ec_su_axb35
```

If `modprobe` fails, inspect `journalctl -k -b` or `dmesg` for driver errors. If it loads but fan data is absent, check `/sys/class/ec_su_axb35` and find any matching hwmon device by its `name` rather than assuming a fixed `hwmonX` number:

```bash
for name_file in /sys/class/hwmon/hwmon*/name; do
    [ -f "$name_file" ] || continue
    [ "$(cat "$name_file")" = ec_su_axb35 ] && echo "${name_file%/name}"
done
```

The fan-profile service runs `modprobe` before applying its curve. If the module cannot load or its sysfs fan interface is absent, the service fails with a diagnostic in `journalctl -u bosgametop-fan-profile.service -b`.

## Building

Clone the repository and run:

```bash
make
```

Then start the monitor with:

```bash
./bgtop
```

Press `q` to quit.

## Installation

Install system-wide:

```bash
sudo make install
```

The executable will be installed by default as:

```text
/usr/local/bin/bgtop
```

You can then run it from anywhere:

```bash
bgtop
```

To uninstall:

```bash
sudo make uninstall
```

## Manual build

You can also build without the Makefile:

```bash
gcc -O2 -Wall -Wextra bgtop.c -o bosgametop -lncurses
```

## Optional fan profile

The repository also contains an optional fan profile for systems using the `ec_su_axb35` driver.

This is intentionally kept separate from the normal `bosgametop` installation.

Running:

```bash
sudo make install
```

does **not** modify fan settings.

The optional profile currently uses:

```text
Ramp-up:   45,52,58,64,70
Ramp-down: 40,47,53,59,65
Mode:      curve
```

for all three fans.

These values are intended for the tested BOSGAME / AXB35 platform and should not be assumed to be appropriate for every system.

### Test the profile manually

Before installing the systemd service, the profile can be tested directly from the repository:

```bash
sudo ./scripts/apply-fan-profile.sh
```

You can verify the resulting configuration with:

```bash
for f in /sys/class/ec_su_axb35/fan*; do
    echo "=== $(basename "$f") ==="
    echo "RPM:       $(cat "$f/rpm")"
    echo "Level:     $(cat "$f/level")"
    echo "Mode:      $(cat "$f/mode")"
    echo "Ramp-up:   $(cat "$f/rampup_curve")"
    echo "Ramp-down: $(cat "$f/rampdown_curve")"
done
```

### Install or update the fan-profile service

Run the separate systemd setup script:

```bash
sudo ./scripts/update_systemd_service.sh
```

It installs the current `scripts/apply-fan-profile.sh` to `/usr/local/lib/bosgametop/` and copies `systemd/bosgametop-fan-profile.service` to `/etc/systemd/system/`. It then reloads systemd, enables the service at boot, and restarts it to apply the profile immediately. The kernel module must already be installed.

Rerun this script after editing the profile or unit file. Restarting reapplies the profile even when the oneshot service is already active.

The underlying `make install-fan-profile` target only installs the files; it does not reload, enable, or start the service. `make install` only installs `bgtop`.

Check its status:

```bash
systemctl status bosgametop-fan-profile.service
```

### Remove the fan profile

Disable the service:

```bash
sudo systemctl disable --now bosgametop-fan-profile.service
```

Remove the installed files:

```bash
sudo make uninstall-fan-profile
sudo systemctl daemon-reload
```

Removing the service does not automatically restore previous firmware fan curves. Rebooting or manually configuring the embedded controller may be required depending on the platform and driver behavior.

## Repository layout

```text
bosgametop/
├── bosgametop.c
├── Makefile
├── README.md
├── .gitignore
├── packaging/dkms/ec_su_axb35/dkms.conf
├── scripts/
│   ├── apply-fan-profile.sh
│   ├── install-ec-su-dkms.sh
│   └── update_systemd_service.sh
└── systemd/
    └── bosgametop-fan-profile.service
```

## Design goals

`bosgametop` is intentionally small.

The goal is not to replace general-purpose tools such as `btop`, but to provide a focused monitor for the unusual memory and hardware layout of AMD Ryzen AI Max+ 395 systems, especially when running local AI workloads.

The program reads kernel interfaces directly rather than depending on ROCm or a monitoring daemon.

This keeps runtime overhead extremely low and makes it suitable for monitoring systems while large language models are running.

## Current platform

Development and testing are currently focused on:

* BOSGAME AI Mini PC
* AMD Ryzen AI Max+ 395
* AMD Strix Halo integrated GPU
* Sixunited AXB35-02 embedded controller
* Debian 13

Support for other compatible systems may work where the same Linux sysfs interfaces are available, but has not yet been extensively tested.

## License

See the repository license for details.
