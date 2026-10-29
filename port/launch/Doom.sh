#!/bin/sh
# Scriptlet. Eject USB, then tap this book.
# The runner shows our stdout, so this file stays quiet and detaches.
mkdir -p /mnt/us/doom
if command -v setsid >/dev/null 2>&1; then
	setsid sh /mnt/us/doom/run.sh </dev/null >>/mnt/us/doom/doom.log 2>&1 &
elif command -v nohup >/dev/null 2>&1; then
	nohup sh /mnt/us/doom/run.sh </dev/null >>/mnt/us/doom/doom.log 2>&1 &
else
	sh /mnt/us/doom/run.sh </dev/null >>/mnt/us/doom/doom.log 2>&1 &
fi
exit 0
