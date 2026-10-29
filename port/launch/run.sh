#!/bin/sh
# Started detached by the Doom scriptlet. Stops the Kindle UI, runs Doom, then brings the UI back.
trap '' TERM HUP INT
echo "=== kindle-doom $(date) ==="
sleep 2
lipc-set-prop com.lab126.powerd preventScreenSaver 1 || true
echo "stopping framework"
stop framework || true
sleep 1
cd /mnt/us/doom || exit 1
if [ ! -f ./kdoom ]; then
	echo "kdoom missing"
	lipc-set-prop com.lab126.powerd preventScreenSaver 0 || true
	start framework || true
	exit 1
fi
chmod 755 ./kdoom
./kdoom -iwad ./doom1.wad
echo "kdoom exit $?"
lipc-set-prop com.lab126.powerd preventScreenSaver 0 || true
echo "starting framework"
start framework || true
