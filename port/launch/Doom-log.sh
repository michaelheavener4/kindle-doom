#!/bin/sh
echo "kindle-doom log"
echo
if [ -f /mnt/us/doom/doom.log ]; then
	tail -n 35 /mnt/us/doom/doom.log
else
	echo "No log yet. Eject USB, then open the Doom book."
fi
sleep 12
