# kindle-doom

Run Doom on a stock Kindle Paperwhite 6 (12th gen, 2024) via the Véra jailbreak,
with REA doing the firmware-level reverse engineering.

## Goal

Native Doom on the PW6 e-ink panel: `doomgeneric` ported to the Kindle Linux
framebuffer + e-ink partial refresh, touch input, launched as a modern-firmware
scriptlet. Shareware `doom1.wad` (freely redistributable, MD5 verified).

## Current state

- REA installed on Workhorse: CLI 6.8.0, MCP registered with OpenCode
  (`~/.config/opencode/opencode.json`), skill at `~/.agents/skills/`.
- Device: Kindle Paperwhite 6 (2024), model `PW6-2024`, firmware package
  `com.lab126.eink.sangria.os`, version **5.20.1.0.2**, build `4926840002`
  (recorded 2026-10-21 on FTVDB). Serial family `GR733X…`, USB `1949:9981`, MTP.
- Jailbreak method selected: **Véra** (per kindlemodding.org `jailbreaks.json`:
  PW6 supported, firmware range 5.17.1–5.20.1.0.2 — our exact version is the
  tested maximum). KUAL is obsolete on this stack; post-jailbreak tooling is
  **KPM** (search-bar package manager) + **scriptlets** (`.sh` in `documents/`
  shown as books, executed when opened).
- Shareware WAD acquired and verified:
  `assets/doom1.wad` MD5 `f0cefca49926d00903cf57551d901abe` (= catalog value
  for Doom shareware v1.9).
- OTA-blocker filler files staged in `staging/` (MTP Kindles need manual copy;
  script-based fillers do not work on 11th-gen+ devices).
- Amazon-signed firmware image for our exact build downloading to `firmware/`
  (source: FTVDB record → CloudFront; expected MD5
  `da1dc95282e2077a18d7df604e1d09b5`).

## Plan

1. **Jailbreak (Véra)** — fill storage with filler files (blocks OTA while
   Wi-Fi is needed), then follow https://kindlemodding.org/jailbreaking/Vera/
   (browser → kindlemodding.org/vera → download 2 books → font-settings dance
   → open Véra book → GUI restarts). Airplane mode off, Wi-Fi required
   throughout; safe to power-button reboot if stuck.
2. **Firmware analysis (REA)** — extract the update BIN (binwalk), confirm:
   arch (armhf expected per KindleModding FAQ), kernel/display stack, e-ink
   refresh interface (mxcfb ioctls or successor), framebuffer geometry/format,
   input devices. Evidence-first: every port decision gets tied to a finding.
3. **Port doomgeneric** — implement `doomgeneric_kindle.c`:
   `/dev/fb0` mmap, e-ink partial refresh per frame, grayscale/dithered
   render modes (reference: k4-doom's `doomgeneric_kfb.c`, kindle-touch-doom's
   framebuffer+touch code), touch zones for movement/turn/fire/use.
   Cross-compile armhf (kindlehf-class toolchain; Docker on Workhorse).
4. **Package & deploy** — scriptlet launcher + payload on userstore, deploy
   over MTP, test on device. Iterate: refresh strategy, dithering, controls.
5. **Optional polish** — SSH over USB for dev loop, KPM packaging, settings.

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
