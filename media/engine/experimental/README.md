# Experimental, not shipped
`mpv-hdr-overlay.patch` is an **unfinished** change against the pinned mpv source (`sources.lock.json`). It is not applied by `build.py`, is in no signed package and must not be installed as is.

Purpose: with `vo=gpu` and `gpu-hwdec-interop=drmprime-overlay` nothing tells the display what the zero-copy video plane carries (only `vo=gpu-next` sends a colour hint, and it has no overlay path). The patch sets the video plane's `COLOR_ENCODING`/`COLOR_RANGE`, passes static HDR metadata to the DRM output (`Colorspace`, `HDR_OUTPUT_METADATA`) and raises `max bpc` for HDR.

Trial result on the CM5 with an LG TV, 1920x1080@24, hardware HEVC, 2026-11-15 (see `../../validation/cm5-20261115-hdr-trial.json`):
- 12 bpc: TV showed "no signal" (twice), although its EDID advertises 36-bit deep colour.
- 10 bpc: TV locked and reported HDR, picture black, with either the BT.2020 or the BT.709 plane matrix.
- 8 bpc: started, result not observed.

Open questions before this can ship: why the picture is black once HDR is signalled, whether the reconfiguration that follows the first HDR frame is involved, and what depth the router's HDMI output can really deliver. Plane colour properties persist after the player exits, so every trial build must set them explicitly.
