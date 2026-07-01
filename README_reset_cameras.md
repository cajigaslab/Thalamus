# reset-cameras — one-command Device Reset for all Vimba USB cameras

## Why this exists

The Allied Vision infrared (USB) cameras periodically produce corrupted frames:
the image breaks into thirds with column artifacts that distort the view. Frames
keep rendering, just wrong. The manual fix was to open **VimbaViewer**, select
every detected camera, and for each one run **Device Control → Device Reset**
(factory reset). Tedious when it happens often.

`reset-cameras` does the same thing to every connected camera in one command, no
GUI. "Device Reset" is the standard GenICam command feature `DeviceReset`, so the
script just enumerates all cameras and runs that feature on each.

## What was set up

| Piece | Location | Purpose |
|-------|----------|---------|
| Worker script | `<SDK>/Tools/reset_cameras.py` | Enumerates cameras, runs `DeviceReset` on each, waits for USB re-enumeration, reports which came back. |
| Global launcher | `/usr/local/bin/reset-cameras` | Tiny bash wrapper that sets the SDK paths and calls the worker so you can run it from anywhere. |

where `<SDK>` = `/home/bijanadmin/Downloads/Vimba64_v6.0_Linux/Vimba_6_0`.

The worker runs **VimbaPython v1.2.1 directly from the SDK source**
(`<SDK>/VimbaPython/Source`) — no `pip install`, no root, works on the system
Python 3.12. The USB transport layer (`VimbaUSBTL.cti`) is already on
`GENICAM_GENTL64_PATH`, so cameras enumerate as your normal user.

## Usage

```bash
reset-cameras            # reset all cameras, wait ~5s, confirm they came back
reset-cameras --no-verify   # fire-and-exit (fastest; skip the re-scan)
reset-cameras --wait 10     # wait longer before re-scanning (slow re-enumeration)
```

Exit code is `0` when every camera was reset and (unless `--no-verify`) came back
online; non-zero otherwise — so it can be dropped into other scripts.

### Typical output

```
Found 2 camera(s):
  - Mako U-130B [671089705]
  - Mako U-130B [671092800]

  [OK]     reset issued -> Mako U-130B [671089705]
  [OK]     reset issued -> Mako U-130B [671092800]

Waiting 5.0s for cameras to re-enumerate...
  [BACK]    Mako U-130B [671089705]
  [BACK]    Mako U-130B [671092800]

All 2 camera(s) reset and back online.
```

## When to run it

Run `reset-cameras` whenever you see the corrupted / banded frames. After it
reports the cameras are back online, restart your capture app (or re-open the
stream) and the artifact should be gone.

## Troubleshooting

- **`[FAILED] ... camera busy/unavailable`** — another application (VimbaViewer,
  your capture app) is holding the camera open. Close it, then re-run.
- **`[MISSING]`** — the camera hasn't re-appeared in the wait window. It usually
  just needs longer; run `reset-cameras --wait 10` (or higher).
- **`No cameras detected`** — nothing is plugged in, or a stale handle is held by
  another app. Check connections and close any app using the cameras.
- **`could not import VimbaPython`** — the SDK moved. Fix the `SDK=` path in
  `/usr/local/bin/reset-cameras` and the paths in `reset_cameras.py`.

## Re-installing the launcher

The worker script is permanent inside the SDK tree. If `/usr/local/bin/reset-cameras`
is ever lost, recreate it:

```bash
sudo tee /usr/local/bin/reset-cameras >/dev/null <<'EOF'
#!/usr/bin/env bash
SDK=/home/bijanadmin/Downloads/Vimba64_v6.0_Linux/Vimba_6_0
export GENICAM_GENTL64_PATH="$SDK/VimbaUSBTL/CTI/x86_64bit${GENICAM_GENTL64_PATH:+:$GENICAM_GENTL64_PATH}"
exec python3 "$SDK/Tools/reset_cameras.py" "$@"
EOF
sudo chmod 0755 /usr/local/bin/reset-cameras
```

You can also run the worker directly without the launcher:

```bash
python3 /home/bijanadmin/Downloads/Vimba64_v6.0_Linux/Vimba_6_0/Tools/reset_cameras.py
```

## Possible future addition

A watchdog that auto-detects the corrupted-frame pattern and resets without you
having to notice/run it. Not built yet — currently this is a manual command.
