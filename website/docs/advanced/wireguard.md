---
id: wireguard
title: Connecting over WireGuard
---

# Connecting over WireGuard

:::danger
Power users only. If you have to ask what WireGuard is, don't use this.
:::

<video controls width="100%" src="https://github.com/user-attachments/assets/5068a259-2f07-46a3-8d65-bfe9866c28b6" />

I had to compress this video to fit GitHub's 10 MB limit.

WireGuard is a VPN. Read about it [here](https://www.wireguard.com/).

It usually ships in-kernel and is lightweight. That's what routers use for built-in VPN servers. MikroTik calls it [Back to Home](https://mikrotik.com/bth). Other vendors call it other things (UniFi has its own gateway offering). The only requirement is that it's WireGuard, not OpenVPN.

HorizonOS's kernel doesn't have WireGuard, so akira does it in userland. It bundles a basic userland WireGuard implementation with TCP and UDP relays over [lwIP](https://www.nongnu.org/lwip/2_1_x/index.html).

:::note
You'll have to stream at a noticeably lower bitrate for this to be usable.
:::

With WireGuard and a dynamic or static IP, you don't need to expose the usual ports (9295 to 9297, 9302, 987) to the internet. As long as your WireGuard server is reachable, you can connect.

## Configuration

Put your WireGuard config at `/switch/akira/wg0.conf`.

<img width="1286" alt="wg0.conf location" src="https://github.com/user-attachments/assets/03039e2f-0b3c-480e-8b30-03ba84bb5944" />

A typical `wg0.conf` (all values will differ):

```ini
[Interface]
ListenPort = <BLA>
PrivateKey = <YOUR PRIVKEY HERE>
Address = 192.168.216.3/32, fc00:0:0:216::3/128
DNS = 192.168.216.1

[Peer]
PublicKey = <YOUR PUBKEY HERE>
AllowedIPs = 0.0.0.0/0, ::/0
Endpoint = <YOUR WG ENDPOINT HERE>:57820
PersistentKeepalive = 30
```

:::warning
You have to be externally reachable and not behind CGNAT.
:::

Your mileage will vary. This is an early release and still jitters, so it's capped to 720p at a max of 15k bitrate. In my tests the actual bitrate negotiated with the PS5 never got past 6k.
