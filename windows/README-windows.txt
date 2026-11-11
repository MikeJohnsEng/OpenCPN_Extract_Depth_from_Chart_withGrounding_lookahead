cm93depth_pi on Windows
=======================

Two separate jobs:

  BUILD    once per OpenCPN type, on a PC with Visual Studio.
           Result: a plugin tarball, e.g.
             windows\dist\cm93depth_pi-1.5.1-0_msvc-wx32-10-x86.tar.gz
  INSTALL  on any PC with that OpenCPN type, from inside OpenCPN:
             Options > Plugins > Import plugin...
           No compiler, script or admin rights needed.

A plugin DLL must match the OpenCPN it runs in: 32- or 64-bit, and the
wxWidgets version (3.2 for current OpenCPN). The tarball name shows which:
"msvc-wx32 ... x86" = 32-bit OpenCPN, "msvc-64 ... x86_64" = 64-bit.
OpenCPN refuses a tarball of the wrong type ("Incompatible import plugin").
It loads in OpenCPN 5.8 and later of the same type; rebuild if a new
OpenCPN release changes 32/64-bit or the wxWidgets version.


Installing (any PC)
-------------------
1. Copy the .tar.gz to the PC.
2. Start OpenCPN. Options > Plugins > Import plugin... and choose the file.
   OpenCPN says "Plugin CM93 Least Depth successfully imported".
3. In the plugin list, find "CM93 Least Depth" and enable it.
   A toolbar button (seabed with a red arrow) appears.
4. Open Settings... in the plugin window and pick the output.

Updating: import the newer tarball the same way; it replaces the old one.
Uninstalling: in Options > Plugins select CM93 Least Depth > Uninstall.

Files go to %LOCALAPPDATA%\opencpn\plugins\cm93depth_pi.dll, wherever
OpenCPN itself is installed.


Building (one PC)
-----------------
1. Install, once
   - Visual Studio 2022 Community (free):
       https://visualstudio.microsoft.com/vs/community/
     In the installer, tick "Desktop development with C++". The defaults
     inside it are fine (they include the C++ CMake tools).
     ("Build Tools for Visual Studio 2022" with the same workload also works.)
   - 7-Zip: https://www.7-zip.org
   - The OpenCPN you are building for (the build reads its opencpn.exe).

2. Unzip cm93depth_pi-*-src.zip somewhere with no special permissions, e.g.
   C:\Users\<you>\Documents\cm93depth_pi

3. Double-click  windows\build_windows.bat
   It will:
     - find OpenCPN (installer registry entry, then Program Files; asks for
       the folder if it can't) and detect 32/64-bit and wxWidgets version
     - download the matching wxWidgets development files (~40 MB, once;
       kept in %LOCALAPPDATA%\cm93depth_pi-build, delete it to free space)
     - fetch the plugin API header for your OpenCPN version (or use the
       bundled one)
     - make an import library from opencpn.exe
     - build cm93depth_pi.dll
     - package it as windows\dist\cm93depth_pi-<version>_<type>.tar.gz
   It does not touch your OpenCPN installation. Install the tarball as
   described above.

   OpenCPN somewhere unusual? From a Command Prompt:
     windows\build_windows.bat -OpenCPNDir "F:\OpenCPN"

   For development only, -Install also copies the DLL straight into this
   PC's plugin folder and enables it in opencpn.ini (as earlier versions did).


If it fails
-----------
- "Visual Studio not found" / "no C++ tools": re-run the Visual Studio
  Installer, Modify, tick "Desktop development with C++".
- Import says "Incompatible import plugin": the tarball was built for the
  other type (32/64-bit) of OpenCPN. Build against the OpenCPN it is for.
- OpenCPN crashes at start with the plugin enabled: start OpenCPN in safe
  mode, or with OpenCPN closed move cm93depth_pi.dll out of
  %LOCALAPPDATA%\opencpn\plugins\. Then send C:\ProgramData\opencpn\opencpn.log
  (copy it before starting OpenCPN again).
- The COM port shows "Access is denied": another program, or an OpenCPN
  connection, already has it open. Either close that, or in Settings choose
  "OpenCPN output connection" and pick that connection.
- Send me the last screen of output if anything else goes wrong.
