---
id: intro
title: Akira
slug: /
---

# Akira

Akira wraps [chiaki-ng](https://github.com/streetpea/chiaki-ng) in a GUI so you can use the PlayStation Remote Play protocol on the Nintendo Switch. It turns the Switch into a PlayStation client.

Start here: **[Registering and Connecting](./setup/registering.mdx)**.

This used to live in-tree. There were too many changes to keep it there without breaking the `.nro` build for everyone, so it's out of tree now. I don't own a PS4, so I can't tell you if it works on one. Let me know.

Other custom libraries: a [forked cURL](https://github.com/xlanor/curl) with websockets enabled, and the libnx vTLS work by [yellows8](https://github.com/devkitPro/curl/commits/libnx-backend/) so cryptography runs on libnx where possible.

## Network requirements

Your network matters more than anything else here.

:::warning
- **You need a good wifi network.** A 5 GHz network on its own isn't enough. You need a strong signal to it.
- **The access point should have a wired backhaul.**
- **Your router should not be dropping packets under load.**
- **Wire your PS4/PS5 with ethernet.** Wifi streaming works, I've done it, but only if everything above is true.
- **Kill any rogue sysmodules pinning core 3 at 100%.** This matters.
- **Your network must be stable and not dropping packets or under heavy load.**
:::

Here's a bad network config that didn't have VLAN filtering enabled and was killing the access points by flooding them with bad packets, spiking them to 100% CPU. The game was almost unplayable.

![Bad network config saturating the access point](https://github.com/user-attachments/assets/d16aecfd-bf3c-4810-993b-b34317e7c2a8)

On a clean emuMMC, I've seen rogue sysmodules drive core 3 to 100%. The Tesla system-status overlay homebrew will show you.

:::tip
Use a clean emuNAND from [switch.hacks.guide](https://switch.hacks.guide/).
:::

Good luck.
