#!/bin/sh
# kindle-doom system probe — verifies scriptlet execution and captures live device facts
# for the Doom port (kernel arch, framebuffer geometry/bit depth, input nodes, MTK e-ink paths).
# Results are written to doom-probe.txt at the root of the Kindle's USB storage.

OUT=""
for d in /mnt/us /storage /udisk; do
    [ -d "$d" ] && OUT="$d/doom-probe.txt" && break
done
[ -z "$OUT" ] && OUT="/tmp/doom-probe.txt"

{
echo "=== kindle-doom probe $(date) ==="
echo "--- id ---"
id
echo "--- uname ---"
uname -a
echo "--- cpuinfo (head) ---"
head -12 /proc/cpuinfo
echo "--- cmdline ---"
cat /proc/cmdline
echo "--- /proc/fb ---"
cat /proc/fb
echo "--- fb0 sysfs entries ---"
ls -l /sys/class/graphics/fb0/ 2>&1
echo "--- fb0 facts ---"
for f in bits_per_pixel rotate mode name virtual_size stride; do
    printf "%s: " "$f"
    cat "/sys/class/graphics/fb0/$f" 2>/dev/null || echo "?"
done
echo "--- input devices ---"
cat /proc/bus/input/devices
echo "--- /dev/input ---"
ls -la /dev/input/ 2>&1
echo "--- MTK eink /proc/hwtcon ---"
ls -la /proc/hwtcon/ 2>&1
echo "--- /sys/class/graphics ---"
ls /sys/class/graphics/ 2>&1
echo "--- storage ---"
df -h 2>&1 | head -12
echo "--- interesting mounts ---"
grep -E "mmc|/us|ext4|vfat|overlay" /proc/mounts | head -12
echo "--- process list (head) ---"
ps w 2>&1 | head -15
echo "=== end probe ==="
} > "$OUT" 2>&1

sync
echo "Probe complete. Results written to $OUT"
