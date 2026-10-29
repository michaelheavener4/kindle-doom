# kindle-doom

Run Doom on a stock Kindle Paperwhite 6 (12th gen, 2024) via the Véra jailbreak,
with REA doing the firmware-level reverse engineering.

## Goal

Native Doom on the PW6 e-ink panel: `doomgeneric` ported to the Kindle Linux
framebuffer + e-ink partial refresh, touch input, launched as a modern-firmware
scriptlet. Shareware `doom1.wad` (freely redistributable, MD5 verified).

## Current state

- Device confirmed over MTP: Kindle Paperwhite 6, serial `REDACTED_DEVICE_SERIAL`,
  firmware **5.20.1.0.2** (`002-juno_20010002_sangria_bellatrix4-492684`).
  `documents/JAILBROKEN.txt` says `jb.sh v2.3.0`. Userstore is `/mnt/us`.
  `libkh/bin/fbink` is ARM hard-float and knows Bellatrix4 / `MXCFB_SEND_UPDATE_MTK`.
- The on-device fbink encodes that ioctl with a 96-byte payload. `port/`
  uses that struct. Panel geometry is read at runtime from `/dev/fb0`
  (the published PW6 panel is 1264×1680, portrait).
- Shareware WAD verified: `assets/doom1.wad` MD5
  `f0cefca49926d00903cf57551d901abe`.
- Port: `port/doomgeneric_kindle.c`, built with the vendored Zig as a static
  `arm-linux-musleabihf` binary (`make -C port`). Launcher scriptlet stops
  the framework (trapping SIGTERM the way KOReader does), runs Doom, then
  `start framework`.
- OTA filler files are still on the Kindle under `filler/`. Leave them.

## Play

Eject the Kindle (it will not open a scriptlet while USB is mounted), then
tap the **Doom** book. Hold it landscape. The bar along the bottom is the
control strip. Left half of the picture is move (up/down/strafe). Right half
is turn; a tap there fires. Power button, or QUIT, returns to the library.

Bar buttons, left to right: FIRE, USE, GUN (cycles weapons 1–7), MAP, MENU,
OK, the waveform name (A2 / DU / GC), FLASH, DAY/INV, R# (rotate), T# (touch
calibration), QUIT.

If the bar is on the wrong edge, tap R# until it sits at the bottom. If the
left side does not walk forward, tap T# (swap, then mirror x, then mirror y).
Both are written to `/mnt/us/doom/kindle.cfg`. If Doom bounces straight back
to the library, tap **Doom-log**.

A2 is the fast 1-bit mode and flashes a full GC16 wipe every 40 frames to
clear ghosting. DU is mid gray. GC is slow and clean. Contrast defaults to
160. Sound is not wired; this panel cannot keep up with audio anyway.

## References

- kindlemodding.org — jailbreaks, `jailbreaks.json` support matrix, What's
  Next (KPM/scriptlets), filler files (Crosunt223/Kindle-Filler-Disk MTP set)
- FTVDB — PW6 firmware history and the public source URL for our build
- `refs/doomgeneric`, `refs/kindle-touch-doom`, `refs/k4-doom`, `refs/kdoom`
  (cloned, gitignored)
- REA docs: firmware-analysis, native-investigation, cli (in the rea-agents
  package / morluto/repo docs)

## Facts vs. unknowns

Verified:
- PW6 + 5.20.1.0.2 → Véra (kindlemodding.org JSON support matrix)
- Modern Kindle firmware ≥5.16.3 is hard-float ARM 32-bit (KindleModding FAQ)
- KUAL dead on modern firmware; scriptlets + KPM are the supported mechanism
- doom1.wad v1.9 integrity (MD5 vs Wad Archive catalog)
- Firmware image URL/MD5 (FTVDB record for build 4926840002)

To verify during firmware analysis (assumptions until then):
- PW6 panel geometry (expected ~1264×1680, 300 ppi) and fb pixel format
- mxcfb ioctl compatibility on the sangria platform (bellatrix4 codename)
- Input device nodes/protocol for the touchscreen
- Scriptlet execution context (cwd, privileges, env)
