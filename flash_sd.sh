#!/bin/bash
# Flash Ambika firmware to SD card
# Usage: ./flash_sd.sh [mount_point]

set -e

SDCARD="${1:-/mnt/sdcard}"
BUILD_DIR="build"
CONTROLLER_BIN="$BUILD_DIR/ambika_controller/ambika_controller.bin"
VOICECARD_BIN="$BUILD_DIR/ambika_voicecard/ambika_voicecard.bin"

# Check firmware files exist
if [ ! -f "$CONTROLLER_BIN" ]; then
    echo "Error: Controller firmware not found at $CONTROLLER_BIN"
    echo "Run 'make -f controller/makefile && make -f controller/makefile bin' first"
    exit 1
fi

if [ ! -f "$VOICECARD_BIN" ]; then
    echo "Error: Voicecard firmware not found at $VOICECARD_BIN"
    echo "Run 'make all && make bin' first"
    exit 1
fi

# Find SD card if not mounted
if [ ! -d "$SDCARD" ]; then
    echo "Mount point $SDCARD does not exist."
    # Try to find and mount the SD card
    DEVICE=$(lsblk -rno NAME,FSTYPE | grep vfat | grep mmc | head -1 | awk '{print $1}')
    if [ -z "$DEVICE" ]; then
        echo "Error: No SD card found"
        exit 1
    fi
    echo "Found SD card at /dev/$DEVICE"
    sudo mkdir -p "$SDCARD"
    sudo mount "/dev/$DEVICE" "$SDCARD"
    echo "Mounted at $SDCARD"
fi

# Check it's actually mounted
if ! mountpoint -q "$SDCARD" 2>/dev/null; then
    DEVICE=$(lsblk -rno NAME,FSTYPE | grep vfat | grep mmc | head -1 | awk '{print $1}')
    if [ -z "$DEVICE" ]; then
        echo "Error: No SD card found"
        exit 1
    fi
    sudo mount "/dev/$DEVICE" "$SDCARD"
    echo "Mounted /dev/$DEVICE at $SDCARD"
fi

echo "Copying firmware to SD card..."
sudo cp "$CONTROLLER_BIN" "$SDCARD/AMBIKA.BIN"
echo "  AMBIKA.BIN  (controller: $(stat -c%s "$CONTROLLER_BIN") bytes)"

for i in 1 2 3 4 5 6; do
    sudo cp "$VOICECARD_BIN" "$SDCARD/VOICE${i}.BIN"
done
echo "  VOICE1-6.BIN (voicecard: $(stat -c%s "$VOICECARD_BIN") bytes)"

# Generate and copy factory patches
echo ""
echo "Generating factory patches..."
PATCH_STAGING=$(mktemp -d)
python3 tools/make_patches.py "$PATCH_STAGING"
sudo mkdir -p "$SDCARD/PATCH/BANK/C"
sudo cp "$PATCH_STAGING/PATCH/BANK/C/"*.PAT "$SDCARD/PATCH/BANK/C/" 2>/dev/null || true
rm -rf "$PATCH_STAGING"

sudo sync
echo ""
echo "Done. Safe to eject SD card."
echo ""
echo "To flash on Ambika:"
echo "  1. Insert SD card"
echo "  2. Hold S8 during power-on to flash controller"
echo "  3. Flash each voicecard with S4 from OS info page"
