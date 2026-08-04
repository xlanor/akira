---
id: features
title: Features & Changes
---

# Features & Changes

Everything chiaki-ng switch could do should still work here (haptics, hardware decoding, login PIN, and the rest), plus:

- **Basic remote play over an external network.** Same caveats as chiaki-ng, PSPlay, PSPortal, and the rest: you have to punch through to PSN for initiation. I didn't test this much because I don't use it much.
- **Select bitrate.**
- **Hidden menu** (think unlocking developer settings on Android) to raise the bitrate cap to 30k. You *request* a bitrate. The PS5 may or may not give it to you. It's not constant-bitrate encoding.
- Dropped the old borealis for [xfangfang's fork](https://github.com/xfangfang/borealis) with the [Yoga](https://github.com/facebook/yoga) layout engine.
- Uses [deko3d](https://github.com/devkitPro/deko3d) instead of OpenGL. No more 3-frame copy buffer. Rendering is fully zero-copy.
- Added a gyro reset event and handled it.
- Pick which stick (left or right) drives gyro. Default is left.
- No need to build switch-dav1d and switch-ffmpeg with hwaccel anymore. Both are in devkitPro now.
- Uses the devkitPro builder Docker image.
- Ripped out mbedTLS. Crypto now runs on libnx and software crypto backed by hardware crypto or micro-ecc depending on the algorithm. See the [forked chiaki-ng crypto README](https://github.com/xlanor/chiaki-ng/blob/5907140e730ff975a01aa8a6eca51ac5c6ca9f41/lib/src/crypto/libnx/README.md).
- Uses libnx from 4.1.0 and a [custom](https://github.com/xlanor/curl) cURL 8.18.0 with the libnx vTLS backend.
- Rumble goes through borealis instead of hitting HID directly.
- Stream debug menu with stats (hold **-** for 3 seconds).
- The stream menu also lets you disconnect cleanly without quitting the app, and disconnect by putting the console to sleep and going back to the akira menu to play another console.
- Userland wireguard support.
- Requests a new I-frame by default on frame corruption.
- Remappable input bindings for everything except the arrow buttons.
- Sleeps the console on exit.
- Slight noise texture for colour debanding, with configurable strength.

:::warning
Don't leave the stats menu open. It degrades performance. It's a development and debug tool, nothing else.
:::
