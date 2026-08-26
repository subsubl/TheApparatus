# Raspberry Pi Images

Both Pis boot straight into playback: no desktop, no login, videolooper-style
media detection. Images are built by GitHub Actions on ARM runners
(`.github/workflows/build-images.yml`, pi-gen branch `bookworm-arm64`,
Raspberry Pi OS Lite 64-bit — runs on Pi 3/4/5).

## Getting an image

Repo → **Actions** → *Raspberry Pi Images* → latest green run → artifacts
`apparatus-pi-A-img` (~590 MB xz) and `apparatus-pi-B-img`. Flash with
Raspberry Pi Imager or balenaEtcher. Tagged releases attach images as assets.

## What each image does at boot

| | apparatus-pi-A | apparatus-pi-B |
|---|---|---|
| Services | player-a | player-b + mpv-daemon |
| Scans `/home/pi/media` for | `layer1_loop*.mp4` (fallback `layer1*`) | `master_L2_L3*.mp4` (fallback `master*`) |
| Output | Mixer CH1 (Layer 1 loop) | Mixer CH2 (master L2/L3, A-B looped to Layer 2) |
| Serial link | — | PL011 UART stable (`disable-bt`, console stripped) |

Extensions: `.mp4 .mkv .mov .avi .ts`. Newest matching file wins.

## Videolooper behavior (both Pis)

`pi/media_autoloader.py` polls the media folder every ~10 s:
- No match yet → **gray placeholder card** ("NO MEDIA") so technicians see a
  live system, not a dead one.
- Match found → launches mpv fullscreen, gapless looping.
- Newer/different matching file appears → graceful swap within ~10 s
  (**hot content updates**: copy the new file over SSH/SFTP/USB, walk away).
- Every respawn (including hot swaps) re-seeds Pi B's A-B loop to Layer 2.

## Video output (PAL composite)

Both players feed the WJ-AVE5 via the Pi's **3.5 mm AV jack as PAL SD
composite**. This is baked into the images: stage `04-pal-composite` sets
`enable_tvout=1` and `sdtv_mode=2` (PAL) in `/boot/firmware/config.txt` at
build time — flash and play, no raspi-config needed.

> **Hardware caveat:** only **Pi 3 / Pi 4** have composite out on the AV jack.
> The **Pi 5 has NO composite output at all** — use Pi 3/4 for both roles.
> HDMI remains live in parallel if you ever need a setup monitor.

## Media folder

Empty in the image by design (`PUT_VIDEOS_HERE.txt` explains conventions).
Login: `pi` / `apparatus` (change before exhibition!), SSH enabled.

## Environment knobs (systemd drop-ins)

`APPARATUS_LAYER3_START_S` (300), `APPARATUS_MASTER_END_S` (600),
`APPARATUS_MPV_VO_ARGS` (Pi 5 frame-drop workaround, see Research-Notes),
`APPARATUS_MEDIA_POLL`, `APPARATUS_PLACEHOLDER_AFTER`.

## CI internals (why the workflow looks like that)

- Custom stages are self-contained: CI copies the playback scripts into
  `image/common-stack/00-install-stack/files/` because pi-gen's container sees
  only stage directories, never the repo checkout.
- Every custom stage has `prerun.sh` with `copy_previous` (rootfs inheritance)
  — omitting it produces an empty rootfs and bizarre chroot failures.
- Chroot commands go through stdin heredocs; helper script files under `/tmp`
  are invisible through pi-gen's mount overlay.
- Timezone must exist in npm `countries-and-timezones` (Europe/Ljubljana does
  not — Europe/Vienna is used; same offset).
- `increase-runner-disk-size` is off: its hardcoded apt-purge list fails on
  ARM runners; disk is freed with plain `rm` instead.
