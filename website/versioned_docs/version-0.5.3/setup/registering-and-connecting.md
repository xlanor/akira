---
id: registering-and-connecting
title: Registering and Connecting
---

# Registering and Connecting

:::info
`psn.flipscreen.games` is dead, and people kept complaining that entering your account ID doesn't work. So everything is unified around the companion app now.
:::

## 1. Set up your PS5/PS4

Read this and get the base settings right first: [Registering your PlayStation](https://streetpea.github.io/chiaki-ng/setup/configuration/#registering-your-playstation).

## 2. Log in to PSN

Log in to PSN in your web browser before you do anything else.

## 3. Set up the companion app

You need the **companion app** that ships with the release. It runs on a computer on the **same network as your switch**.

:::warning
If you can't connect to the companion app, it's almost always network connectivity between the switch and the computer. Make sure the companion server is reachable with `curl`.
:::

On macOS/Linux, download it, `chmod +x` it, then run `./akira-companion-linux-amd64` (or whatever the binary is called).

On macOS, the OS blocks unknown publishers by default. Allow it in System Settings.

![macOS allowing an unknown publisher](https://github.com/user-attachments/assets/7a9df9ff-580d-45f2-b72c-e51ab3e58863)

![Companion app main screen](https://github.com/user-attachments/assets/5c629d22-067b-43fb-83f5-a4b44d66ad5c)

Run the companion app, generate a device ID, then press **n**.

## 4. Fetch the npsso token

![npsso prompt in the companion app](https://github.com/user-attachments/assets/ce55003d-9f46-4486-856f-ad58f3b8a778)

Go to the URL in the companion app **after** you've logged in.

![Browser showing the npsso JSON](https://github.com/user-attachments/assets/6af3342b-43b0-4b1d-b945-058427940cf3)

Copy the value under the `npsso` key (blocked out in red) without the `"` around it.

![Copying the npsso value](https://github.com/user-attachments/assets/6344a057-1997-4c81-be0c-7c29e667d0e7)

Paste it in and hit enter. It should authenticate.

![Successful authentication](https://github.com/user-attachments/assets/2e50d522-b806-40dc-9c3f-10f06cf75547)

Press **n** for the next step.

## 5. Start the HTTP server

<img width="778" alt="Companion server screen" src="https://github.com/user-attachments/assets/c6d9b85d-cb52-405e-bfb8-a4a8a1d13dfc" />

You land on the "server" screen. Press **s** to start. This runs an HTTP server your switch can query.

<img width="704" alt="Server started" src="https://github.com/user-attachments/assets/b2143d69-0c2c-46ce-86e8-46d883c7d8f8" />

## 6. Fetch the token from akira

Now go to akira.

![Akira account settings](https://github.com/user-attachments/assets/f397d56e-7052-4b8f-940e-0b07c2b2f139)

Enter the IP of the PC running the companion app. I have a few network interfaces here, so I know the one I need is `192.168.20.123`. Yours will differ depending on your setup (wifi, LAN card, Docker).

Press **Fetch**. This connects to the companion app and pulls from the HTTP server.

![Fetching the token](https://github.com/user-attachments/assets/9d558983-edfa-4882-ac38-6514fc552540)

You should see this:

![Token fetched](https://github.com/user-attachments/assets/b8f6d9d0-d519-4730-b15a-d5b9e07bc399)

## 7. Add your first host (local)

Autodiscovery **only works on the same subnet**. My PS5 is on a different subnet, so I just use manual:

![Manual host entry](https://github.com/user-attachments/assets/a69b8e05-b85d-4463-ab52-fd57fef91899)

![Registration screen](https://github.com/user-attachments/assets/ff03c0e3-5c72-400b-97ae-1ecd961c373f)

Hit **Register**, then enter the 8-digit PIN from **Pair with Remote Play**.

![Entering the pairing PIN](https://github.com/user-attachments/assets/a9fa0f8d-1c14-46d8-ad42-1a2d0370e6c8)

## 8. Add a host (remote)

:::caution
I wrote this for fun and don't really use it. **UDP hole-punching is tricky and it may or may not work.**
:::

Read this carefully. The same [prerequisites](https://streetpea.github.io/chiaki-ng/setup/remoteconnection/) apply:

- Console on the latest firmware.
- **Register the console locally first.** That's where the registration key comes from.
- Over PSN, PS4 only works with the primary console on your account. Sony's limitation, not chiaki-ng's.

Then it's simple. Hit **Find Remote**:

![Find Remote](https://github.com/user-attachments/assets/366ab74c-a646-439e-a308-4479dee0a76e)

Link it to a local console:

![Linking to a local console](https://github.com/user-attachments/assets/52c88649-b54e-4c88-9d46-e13daeeb7888)

## FAQ

### Why are you doing this? What's wrong with the lookup button?

There's no native unauthenticated API for fetching an account ID. Every one of them uses a throwaway Sony account with npsso under the hood. The public ones went down and I'm not self-hosting one. Streamlining this is the easiest fix, and remote play needs it anyway, so you fetch everything in one shot.

### Are you sending my tokens anywhere?

No. Everything stays local on your PC and switch.

### I keep getting "could not resolve host name". The server is running, my PS5 is on, and the IP and ports are correct.

The PS5 has nothing to do with this flow at this point. The steps are:

1. Log in to the web and grab the tokens.
2. Expose them on an HTTP server on your local network so the switch can connect.
3. Connect to that HTTP server from the switch and pull the tokens.

If step 3 fails, the problem is your switch reaching your PC. Check the basics:

- You entered the right IP and port shown in the companion app.
- The switch has the right date and time.
- You can hit the HTTP server with `curl` from another device and pull the JSON at `/tokens`.

### This app sucks, I think X is better.

Then use X. I used to maintain chiaki-ng's switch port in-tree, and kkwong maintained his own fork with rumble and hwdec. Both stopped because dealing with users like this got tiring. If you don't want the improvements to the render loop, input latency, wireguard, and external connectivity, don't use it.

### Do you take donations?

No. This was built to scratch our own itch. Projects I'd actually give money to:

- [Streetpea](https://github.com/streetpea/chiaki-ng) for chiaki-ng.
- [devkitPro](https://www.patreon.com/devkitPro) for maintaining the homebrew toolchains.
- [KDE](https://kde.org/donate/) for the free desktop I use every day.
- Any Linux distro that needs it. Linux infrastructure work has put food on our tables.
- [Make-A-Wish](https://www.make-a-wish.org.uk/) and [Soup Kitchen London](https://soupkitchenlondon.org/).
