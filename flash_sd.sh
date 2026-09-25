#!/bin/bash
# Flash Ambika firmware to SD card
# Usage: ./flash_sd.sh [mount_point]

set -e

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

find_card() {
    lsblk -rno NAME,FSTYPE | grep vfat | grep mmc | head -1 | awk '{print $1}'
}

if [ -n "$1" ]; then
    SDCARD="$1"
else
    # Prefer a card the desktop already mounted (udisks: /media/$USER/LABEL).
    DEVICE=$(find_card)
    if [ -n "$DEVICE" ]; then
        SDCARD=$(lsblk -rno MOUNTPOINT "/dev/$DEVICE" | head -1)
    fi
    SDCARD="${SDCARD:-/mnt/sdcard}"
fi

# Mount it ourselves if it isn't mounted yet
if ! mountpoint -q "$SDCARD" 2>/dev/null; then
    DEVICE=$(find_card)
    if [ -z "$DEVICE" ]; then
        echo "Error: No SD card found"
        exit 1
    fi
    echo "Found SD card at /dev/$DEVICE"
    if command -v udisksctl >/dev/null 2>&1; then
        SDCARD=$(udisksctl mount -b "/dev/$DEVICE" | sed 's/.* at //' | tr -d '.')
    else
        sudo mkdir -p "$SDCARD"
        sudo mount "/dev/$DEVICE" "$SDCARD"
    fi
    echo "Mounted at $SDCARD"
fi

# Only escalate if the card isn't already writable as us
if [ -w "$SDCARD" ]; then
    SUDO=""
else
    SUDO="sudo"
fi

echo "Copying firmware to $SDCARD..."
$SUDO cp "$CONTROLLER_BIN" "$SDCARD/AMBIKA.BIN"
echo "  AMBIKA.BIN  (controller: $(stat -c%s "$CONTROLLER_BIN") bytes)"

for i in 1 2 3 4 5 6; do
    $SUDO cp "$VOICECARD_BIN" "$SDCARD/VOICE${i}.BIN"
done
echo "  VOICE1-6.BIN (voicecard: $(stat -c%s "$VOICECARD_BIN") bytes)"

# Generate and copy factory patches
echo ""
echo "Generating factory patches..."
PATCH_STAGING=$(mktemp -d)
python3 make_patches.py "$PATCH_STAGING" > /dev/null

# Copy every bank the generator produced, leaving other banks on the card alone
for BANK_DIR in "$PATCH_STAGING"/PATCH/BANK/*/; do
    BANK=$(basename "$BANK_DIR")
    $SUDO mkdir -p "$SDCARD/PATCH/BANK/$BANK"
    $SUDO cp "$BANK_DIR"*.PAT "$SDCARD/PATCH/BANK/$BANK/"
    echo "  Bank $BANK: $(ls -1 "$BANK_DIR"*.PAT | wc -l) patches"
done
rm -rf "$PATCH_STAGING"

$SUDO sync
echo ""
echo "Done. Safe to eject SD card."
echo ""
echo "To flash on Ambika:"
echo "  1. Insert SD card"
echo "  2. Hold S8 during power-on to flash controller"
echo "  3. Flash each voicecard with S4 from OS info page"
