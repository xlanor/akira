# Akira
Akira is a hombrew application built with xfangfang's fork of [borealis](https://github.com/xfangfang/borealis) that utilises a forked variant of [chiaki-ng](https://github.com/streetpea/chiaki-ng) on the Nintendo Switch.

This is still considered unstable/beta, bugs are expected. However I am personally happy with the stream quality at the current state and am playing on it daily.

## Disclaimer
This project is not endorsed or certified by Sony Interactive Entertainment LLC. This project is free and open-source, and licensed under the same license as the core library it uses, chiaki-ng.

## Read the Wiki
Please, [read the wiki](https://github.com/xlanor/akira/wiki).

## Issues

I am present on chiaki-ng discord's switch-support channel, where StreetPea has graciously allowed me to seed akira amongst existing chiaki-ng users.

I will not respond to direct pings, please try to use the search button and/or [read the wiki](https://github.com/xlanor/akira/wiki) first and then post a message with details of what you've tried. 

I'd also love to hear if you're using this application and it works well for you.

If you find a bug or have a feature request, please help out and open a PR with the fix/implemented feature. 

I do not support anything but the latest version, please ensure you are always on the latest version avaliable in releases.

<p align="center" width="100%">
  <img src="readme/akira_hosts_020.jpg"
</p>
<p align="center">
  <img src="readme/akira_remote_conn_holepunch.jpg">
  <img src="readme/akira_bitrate.jpg">
</p>
<p align="center">
  <img src="readme/akira_ingame_debug_2.jpg" >
  <img src="readme/akira_ingame_debug.jpg" >
</p>

<p align="center">
And some horribly compressed encodes to fit < 10mb:
</p>
<p align="center" width="100%">
<video src="https://github.com/user-attachments/assets/1ae8a3e3-9123-43cf-ae2e-f038383ef87d" width="80%" controls></video>
</p>

<p align="center" width="100%">
<video src="https://github.com/user-attachments/assets/13bff761-42a8-43d6-901c-4aca7dbc26f0" width="80%" controls></video>
</p>


## Blurb
I initially started working on this when FW 21 broke chiaki-ng.The changes I was going to be making was very invasive, and so I took it out of tree first, switching to a homebrew nro that wraps chiaki-ng as a dependency. I may upstream this back into chiaki-ng eventually. 

## Changes from in-tree
[Read this](https://github.com/xlanor/akira/wiki/Additional-changes)

The biggest feature change is the addition of PSN remote play. Thanks to the hard work by Streetpea on chiaki-ng and grill2010 for reverse engineering the PSN api, I just call whatever he has built. You should see two screens come up if this is successful.

The first screen is going to be for the CTRL holepunching, and the second screen for DATA holepunching after the session has been initalised. Rather than show an empty screen I decided to just stream the logs.

Other features that are listed there but I'll reproduce here include

- Remappable input buttons/touchscreen
- Initial wireguard support
- deko3d over OpenGL
- Selectable gyro source. This allows you to select left/right joycon as the actual gyro source when playing in detached.

At this point, I think I've tackled most of the major feature asks that I've seen across gbatemp/github and it's time to enjoy playing my backlog of multiple AC games + Ghost of Tsushima that I picked up specifically to celebrate this.

## How to get started
[Read this for local](https://github.com/xlanor/akira/wiki/Registering-and-Connecting-(Local-Network))

[Read this for remote](https://github.com/xlanor/akira/wiki/Registering-and-Connecting-(Remote-Network))

Actually, just read the whole wiki.

---

This software was built with reference/code from:

- [Streetpea](https://github.com/streetpea/chiaki-ng) the original chiaki-ng code
- [moonlight-switch](https://github.com/XITRIX/Moonlight-Switch) XITRIX's deko3d renderer for moonlight for the deko3d bits
- [switchfin](https://github.com/dragonflylee/switchfin/blob/bbcf9037fc3b11a78f5e0b7489d9e776fff2d99c/scripts/switch/mpv/deko3d.patch#L371) The patches used by dragonflylee in switchfin
- [wiliwili](github.com/xfangfang/wiliwili) WiliWili for how to get started with this new borealis api.


## Credits
- [PS5 icons by Zacksly, please support him here:](https://zacksly.itch.io/ps5-button-icons-and-controls)
- [Switch icons by zacksly, please support him here:](https://itch.io/queue/c/1334295/designs-by-zacksly?game_id=885118&password=)
- [Florian Grill (PXPlay dev](https://streamingdv.github.io/psplay/index.html) for the reverse engineering of the PS Remote Play API
- [Streetpea](https://github.com/streetpea/chiaki-ng) for chiaki-ng and your tireless effort in maintaining this library.
- [moonlight-switch](https://github.com/XITRIX/Moonlight-Switch) for the deko3d rendering code that I based it off with some changes 
- [thestr4ng3r](https://git.sr.ht/~thestr4ng3r/chiaki) for the original chiaki
- [devkitpro](https://github.com/devkitPro) for the associated homebrew packages
- [yellows8](https://github.com/devkitPro/curl/commits/libnx-backend/) for the work on the libnx backend which I used and updated for curl 8.18.0
- [xfangfang](https://github.com/xfangfang), [dragonflylee](https://github.com/dragonflylee), and [XITRIX](https://github.com/XITRIX) for all the work on borealis, moonlight, wiliwili, and switchfin which have made developing homebrew a much smoother experience due to all the examples avaliable.
- [kkwong](https://git.sr.ht/~kkwong/chiaki) for the initial hwacel and rumble patches
- H0neyBadger for the initial switch port
- [micro-ecc](https://github.com/kmackay/micro-ecc) for the ECDH implementation that was vendored in.
- [vecteezy](https://www.vecteezy.com/vector-art/67445984-adorable-capybara-illustration-enjoying-a-drink) for the capybara logo.




