---
id: features
title: Features & Changes
---

# Features & Changes from chiaki-ng / chiaki 

Everything chiaki-ng switch could do should still work here (haptics, hardware decoding, login PIN, and the rest).

Many of these features are now present in other NX homebrew apps but were pioneered in Akira.

### Major features
- **Basic remote play over an external network.** Same caveats as chiaki-ng, PSPlay, PSPortal, and the rest: you have to punch through to PSN for initiation. 
- **PSCloud Streaming**
- **Multiple PSN profiles**
- **Select bitrate.**
- **Hidden menu** (think unlocking developer settings on Android) to raise the bitrate cap to 30k. You *request* a bitrate. The PS5 may or may not give it to you. It's not constant-bitrate encoding.
- Gyro support and reset including the ability to pick which controller drives gyro (default left)
- After many nights debugging with kkwong, input latency is comparable with running android on your switch.
- Switch away from mbedTLS to libnx crypto
- Stream debug menu with stats
- Stream disconnection ability
- **Fully supported in-app wireguard.** (No tailscale support will be forthcoming, stop asking)
- **Fully Remappble input bindings**
- Ability to sleep the console on exit
- **FSR 1.0** with configurable filters
- Trophy page support
- Auto-registration over PSN, requires holepunching.
- Cross-subnet discovery (swept by unicast)
- AIA chain repair (when encountering a PSN node with only leaf TLS certs)

And for v0.6.0