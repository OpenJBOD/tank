# OpenJBOD Tank

Tank is a piece of embedded software for the [OpenJBOD RP2040](https://github.com/OpenJBOD/rp2040) controller. The software allows the user to:
- Remotely switch the JBOD power supply on or off
- View the chassis environment status (Fan speeds, temperature, etc)
- Configure an adjustable fan curve
- Create new users for remote management
- Configure custom hostnames and static network addresses

Tank supports both HTTP and HTTPS (albeit HTTPS is a bit slower, at the time of writing, but generally stable.)

The name **Tank** is a reference to Oracle/Sun documentation for ZFS naming example storage pools 'tank'.

## Features not yet implemented

At this time, the following features still need to be implemented:
- [ ] External DS18B20 Temperature Sensor
- [ ] UART over USB for debugging/logging

## Development Environment Setup

The Tank firmware is built on the Zephyr RTOS. The steps below describe how to create a local development environment on an Ubuntu/Debian host. Equivalent packages exist for other Linux distributions; Windows and macOS users can adapt the instructions using Zephyr's official host setup guide. This assumes you do not have any pre-requisites installed.

If you already have West installed and are familiar with Zephyr, you can adjust this to match your existing workflow.

### 1. Install Host Prerequisites

```bash
sudo apt update
sudo apt install --no-install-recommends \
  build-essential cmake ninja-build gperf ccache dfu-util device-tree-compiler \
  wget git python3-dev python3-tk xz-utils file make gcc \ 
  gcc-multilib g++-multilib libsdl2-dev libmagic1
```

(If working on ARM64/AArch64, omit `gcc-multilib` and `g++-multilib`.)

### 2. Install West, West SDK, and clone Tank

```bash
# Create a Python venv
python3 -m venv venv
source venv/bin/activate

# Install West
pip install west

# Clone Tank and establish a Zephyr workspace
west init -m https://github.com/openjbod/tank --mr main tank-workspace

# Fetch dependencies and install the West SDK
cd tank-workspace
west update
west packages pip --install
west sdk install -t arm-zephyr-eabi
```

### 3. Build Tank

```bash
west build -s tank -b rpi_pico
```

The resulting firmware image is produced in `build/zephyr/zephyr.uf2`. Drag-and-drop the UF2 onto a mounted RP2040 boot volume, flash it using `picotool`, or use any other preferred workflow.

### 4. (Optional) Clean and Rebuild

```bash
west build -t pristine
west build -s tank -b rpi_pico
```