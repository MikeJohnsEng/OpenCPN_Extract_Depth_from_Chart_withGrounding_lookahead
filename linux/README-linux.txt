cm93depth_pi on Linux and Raspberry Pi
======================================

Same two jobs as on Windows:

  BUILD    once per distribution release and architecture, with
           linux/build_linux.sh. Result: a plugin tarball, e.g.
             linux/dist/cm93depth_pi-1.5.1-0_debian-13-arm64.tar.gz
  INSTALL  on any PC with that release and architecture, from inside
           OpenCPN: Options > Plugins > Import plugin...

Which OpenCPN
-------------
The plugin needs OpenCPN 5.8 or later (plugin API 1.18).

  Ubuntu / Mint      OpenCPN from the OpenCPN PPA (ppa:opencpn/opencpn) or
                     the distribution if it is 5.8+.
  Debian 13 trixie   "apt install opencpn" (5.10).
  Raspberry Pi OS    Based on trixie: "apt install opencpn" is fine.
                     Based on bookworm: its OpenCPN is 5.6, too old. Use the
                     Flatpak OpenCPN, or move to the trixie-based release.
  Flatpak OpenCPN    Needs a plugin built inside the Flatpak SDK; see below.

Check with Help > About in OpenCPN.

Building
--------
1. Unzip the plugin source, e.g. into ~/cm93depth_pi
2. In a terminal:
       bash ~/cm93depth_pi/linux/build_linux.sh
   It installs what it needs with apt (asks for your password once):
   cmake, g++, the wxWidgets 3.2 GTK development files (the same library
   OpenCPN uses), liblzma-dev, libgl-dev, lsb-release. Then it builds
   libcm93depth_pi.so and packages it as linux/dist/....tar.gz.
   A Raspberry Pi 4/5 takes a few minutes.

   For development, --install also copies it to ~/.local/lib/opencpn/ and
   enables it in ~/.opencpn/opencpn.conf (backup kept).

Installing
----------
1. Copy the .tar.gz to the PC (same distribution release and arm64/armhf/
   x86_64 as the name says).
2. OpenCPN: Options > Plugins > Import plugin..., choose the file.
3. Enable "CM93 Least Depth" in the plugin list.
Files go to ~/.local/lib/opencpn/ for that user. Uninstall from the same list.

Serial ports
------------
USB serial adapters are /dev/ttyUSB0, /dev/ttyACM0 ...; the Pi's own UART is
/dev/serial0. Your user must be in the "dialout" group to open them:
    sudo usermod -aG dialout $USER     (then log out and in)
Or send through an OpenCPN connection instead (Settings > NMEA output).

Alarm sound
-----------
Uses OpenCPN's sound system. Test it on the Alarm tab; on a Pi without a
speaker set the audio output (HDMI / headphone / USB) in the desktop first.

Flatpak OpenCPN
---------------
The Flatpak OpenCPN runs in its own runtime and cannot load a plugin built
against the system libraries. It needs the plugin built with flatpak-builder
against the OpenCPN Flatpak SDK (as OpenCPN's own catalogue plugins are).
That is not set up yet. Until then use an apt-installed OpenCPN 5.8+.

If it fails
-----------
- "OpenCPN ... is too old": see "Which OpenCPN" above.
- Plugin not listed after import: OpenCPN's log (~/.opencpn/opencpn.log),
  lines with "cm93depth". "incompatible" usually means it was built on a
  different release or architecture than this PC: build it on (a PC like)
  this one.
