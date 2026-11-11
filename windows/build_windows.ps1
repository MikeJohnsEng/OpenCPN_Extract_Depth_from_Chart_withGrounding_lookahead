# ---------------------------------------------------------------------------
# cm93depth_pi - build on Windows, matched to the installed OpenCPN (32- or
# 64-bit, wxWidgets 3.2 or 3.3), and package it as a plugin tarball that
# OpenCPN installs itself: Options > Plugins > Import plugin...
#
# Normally started by double-clicking build_windows.bat in this folder.
# The result is windows\dist\cm93depth_pi-<version>_<target>.tar.gz. Copy it
# to any PC with the same OpenCPN type (32/64-bit) and import it there.
#
# Needs:
#   * Visual Studio 2022 (Community is fine) or "Build Tools for Visual
#     Studio 2022" with the "Desktop development with C++" workload
#   * 7-Zip (https://www.7-zip.org) - to unpack the wxWidgets download
#   * Internet access (downloads ~40 MB of wxWidgets development files once)
#
# Options:
#   -OpenCPNDir "D:\Apps\OpenCPN"   folder holding opencpn.exe, if it is not
#                                   found automatically
#   -Install                        also copy the DLL into this PC's plugin
#                                   folder and enable it (development use;
#                                   importing the tarball is the normal way)
# ---------------------------------------------------------------------------
param(
  [string]$OpenCPNDir = "",
  [switch]$Install
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"  # Invoke-WebRequest is very slow with the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$PluginDir = (Resolve-Path (Join-Path $Here "..")).Path
$Work = Join-Path $Here "work"
New-Item -ItemType Directory -Force -Path $Work | Out-Null
# wxWidgets downloads live outside the plugin folder, so unzipping a new
# plugin version somewhere else doesn't fetch them again.
$Cache = Join-Path $env:LOCALAPPDATA "cm93depth_pi-build"
New-Item -ItemType Directory -Force -Path $Cache | Out-Null

function Step($msg) { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }
function Fail($msg) { Write-Host ""; Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

# ---- 1. Find OpenCPN ----------------------------------------------------------
Step "Looking for OpenCPN"
$candidates = @()
if ($OpenCPNDir) { $candidates += $OpenCPNDir }
# Where the OpenCPN installer says it put it (any drive)
foreach ($k in @("HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*",
                 "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*",
                 "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*")) {
  Get-ItemProperty $k -ErrorAction SilentlyContinue |
    Where-Object { $_.DisplayName -like "OpenCPN*" } | ForEach-Object {
      if ($_.InstallLocation) { $candidates += $_.InstallLocation.Trim('"') }
      if ($_.DisplayIcon) { $candidates += (Split-Path ($_.DisplayIcon -replace ',\d+$', '').Trim('"')) }
      if ($_.UninstallString) { $candidates += (Split-Path $_.UninstallString.Trim('"')) }
    }
}
$candidates += (Join-Path $env:ProgramFiles "OpenCPN")
if (${env:ProgramFiles(x86)}) { $candidates += (Join-Path ${env:ProgramFiles(x86)} "OpenCPN") }
$OpenCPNDir = $null
foreach ($c in $candidates) {
  if ($c -and (Test-Path (Join-Path $c "opencpn.exe"))) { $OpenCPNDir = $c; break }
}
while (-not $OpenCPNDir) {
  Write-Host "    opencpn.exe not found." -ForegroundColor Yellow
  $c = (Read-Host "    Type the folder holding opencpn.exe (or just Enter to stop)").Trim('"', ' ')
  if (-not $c) { Fail "opencpn.exe not found. Run again with -OpenCPNDir `"<folder holding opencpn.exe>`"." }
  if (Test-Path (Join-Path $c "opencpn.exe")) { $OpenCPNDir = $c }
}
$Exe = Join-Path $OpenCPNDir "opencpn.exe"
Write-Host "    $Exe"

# 32 or 64 bit? Read the machine field of the PE header.
$bytes = [IO.File]::ReadAllBytes($Exe)
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
$machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
$bytes = $null
switch ($machine) {
  0x8664 { $Arch = "x64"; $CMakeArch = "x64";   $LibMachine = "x64"; $WxSuffix = "_x64"; $VsArch = "amd64" }
  0x014c { $Arch = "x86"; $CMakeArch = "Win32"; $LibMachine = "x86"; $WxSuffix = "";     $VsArch = "x86" }
  default { Fail ("Unsupported opencpn.exe architecture (machine 0x{0:X})" -f $machine) }
}
Write-Host "    $Arch build"

# OpenCPN version (for the matching plugin API header)
$ver = (Get-Item $Exe).VersionInfo
$OcpnVersion = $null
foreach ($v in @($ver.ProductVersion, $ver.FileVersion)) {
  if ($v -and ($v -match '(\d+)[\.,]\s*(\d+)[\.,]\s*(\d+)')) {
    $OcpnVersion = "$($matches[1]).$($matches[2]).$($matches[3])"; break
  }
}
Write-Host "    version: $(if ($OcpnVersion) { $OcpnVersion } else { 'unknown' })"

# wxWidgets series used by this OpenCPN, from its DLL names
$WxSeries = "3.2"
$wxdll = Get-ChildItem -Path $OpenCPNDir -Filter "wxmsw*u_core*.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($wxdll -and ($wxdll.Name -match '^wxmsw(\d)(\d)u_core')) { $WxSeries = "$($matches[1]).$($matches[2])" }
switch ($WxSeries) {
  "3.2" { $WxVersion = "3.2.9" }
  "3.3" { $WxVersion = "3.3.1" }
  default { Fail "OpenCPN uses wxWidgets $WxSeries, which this script doesn't know about." }
}
if ($env:WXVER) { $WxVersion = $env:WXVER }
Write-Host "    wxWidgets $WxSeries (building against $WxVersion)"

# ---- 2. Visual Studio ---------------------------------------------------------
Step "Setting up Visual Studio C++ tools ($Arch)"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
  Fail "Visual Studio not found. Install Visual Studio 2022 Community (or Build Tools) with 'Desktop development with C++'."
}
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Fail "Visual Studio has no C++ tools. In the Visual Studio Installer, add 'Desktop development with C++'." }
$devcmd = Join-Path $vs "Common7\Tools\VsDevCmd.bat"
$envDump = cmd /c "`"$devcmd`" -no_logo -arch=$VsArch -host_arch=amd64 && set"
foreach ($line in $envDump) {
  if ($line -match '^([^=]+)=(.*)$') {
    [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
  }
}
foreach ($tool in @("cl.exe", "dumpbin.exe", "lib.exe", "cmake.exe")) {
  if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
    Fail "$tool not found after loading the Visual Studio environment. Make sure the C++ workload (with CMake tools) is installed."
  }
}
Write-Host "    $vs"

# ---- 3. wxWidgets development files -------------------------------------------
$WxRoot = Join-Path $Cache "wxWidgets-$WxVersion"
# reuse a copy unpacked by an earlier version of this script, if present
$oldWx = Join-Path $Work "wxWidgets-$WxVersion"
if (-not (Test-Path $WxRoot) -and (Test-Path $oldWx)) {
  try { Move-Item $oldWx $WxRoot } catch { }
}
$WxLib = Join-Path $WxRoot "lib\vc14x$($WxSuffix)_dll"
if (-not (Test-Path (Join-Path $WxLib "mswu\wx\setup.h"))) {
  Step "Downloading wxWidgets $WxVersion development files"
  $sevenZip = $null
  foreach ($p in @((Join-Path $env:ProgramFiles "7-Zip\7z.exe"),
                   $(if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe" }))) {
    if ($p -and (Test-Path $p)) { $sevenZip = $p; break }
  }
  if (-not $sevenZip) {
    $cmd = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($cmd) { $sevenZip = $cmd.Source }
  }
  if (-not $sevenZip) { Fail "7-Zip not found. Install it from https://www.7-zip.org and run again." }

  $base = "https://github.com/wxWidgets/wxWidgets/releases/download/v$WxVersion"
  $files = @("wxWidgets-$WxVersion-headers.7z", "wxMSW-$($WxVersion)_vc14x$($WxSuffix)_Dev.7z")
  $dl = Join-Path $Cache "downloads"
  New-Item -ItemType Directory -Force -Path $dl | Out-Null
  foreach ($f in $files) {
    $out = Join-Path $dl $f
    if (-not (Test-Path $out)) {
      Write-Host "    $f"
      try { Invoke-WebRequest -UseBasicParsing -Uri "$base/$f" -OutFile $out }
      catch { if (Test-Path $out) { Remove-Item $out }; Fail "Download failed: $base/$f`n$_" }
    }
    & $sevenZip x -y "-o$WxRoot" $out | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "7-Zip could not unpack $f" }
  }
  if (-not (Test-Path (Join-Path $WxLib "mswu\wx\setup.h"))) {
    Fail "wxWidgets unpacked but $WxLib\mswu\wx\setup.h is missing."
  }
}
Write-Host "    wxWidgets: $WxRoot"

# ---- 4. OpenCPN plugin API header ----------------------------------------------
Step "Getting the OpenCPN plugin API header"
$ApiDir = Join-Path $Work "api"
New-Item -ItemType Directory -Force -Path $ApiDir | Out-Null
$hdr = Join-Path $ApiDir "ocpn_plugin.h"
$got = $false
if ($OcpnVersion) {
  $url = "https://raw.githubusercontent.com/OpenCPN/OpenCPN/Release_$OcpnVersion/include/ocpn_plugin.h"
  try {
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $hdr
    $got = $true
    Write-Host "    ocpn_plugin.h from Release_$OcpnVersion"
  } catch { }
}
if (-not $got) {
  Copy-Item (Join-Path $PluginDir "api\ocpn_plugin.h") $hdr -Force
  Write-Host "    using the bundled ocpn_plugin.h (OpenCPN 5.14.2); fine for 5.8 and later"
}

# ---- 5. Import library for opencpn.exe -------------------------------------------
Step "Creating opencpn.lib from opencpn.exe's exports"
$def = Join-Path $Work "opencpn_$Arch.def"
$implib = Join-Path $Work "opencpn_$Arch.lib"
$exports = & dumpbin /nologo /exports $Exe
$names = foreach ($l in $exports) {
  if ($l -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') { $matches[1] }
}
if (-not $names -or $names.Count -lt 50) {
  Fail "opencpn.exe exports only $($names.Count) symbols; this OpenCPN build can't take compiled plugins."
}
$defText = @("LIBRARY `"opencpn.exe`"", "EXPORTS") + ($names | ForEach-Object { "    $_" })
Set-Content -Path $def -Value $defText -Encoding ASCII
& lib /nologo "/def:$def" "/machine:$LibMachine" "/out:$implib" | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "lib.exe failed to create the import library" }
Write-Host "    $($names.Count) exports -> $implib"

# ---- 6. Configure and build ------------------------------------------------------
Step "Building cm93depth_pi.dll ($Arch, Release)"
$Build = Join-Path $Work "build-$Arch"
# Fresh build tree each run (only a handful of source files): stops a cached wxWidgets
# path from an older run, or a moved folder, breaking the configure step.
if (Test-Path $Build) { Remove-Item -Recurse -Force $Build }
& cmake -S $PluginDir -B $Build -A $CMakeArch `
    "-DwxWidgets_ROOT_DIR=$WxRoot" "-DwxWidgets_LIB_DIR=$WxLib" `
    "-DwxWidgets_CONFIGURATION=mswu" `
    "-DOCPN_API_DIR=$ApiDir" "-DOCPN_IMPORT_LIB=$implib"
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed (see messages above)" }
& cmake --build $Build --config Release
if ($LASTEXITCODE -ne 0) { Fail "Build failed (see messages above)" }
$Dll = Join-Path $Build "Release\cm93depth_pi.dll"
if (-not (Test-Path $Dll)) {
  $found = Get-ChildItem -Path $Build -Recurse -Filter "cm93depth_pi.dll" | Select-Object -First 1
  if (-not $found) { Fail "Build finished but cm93depth_pi.dll was not found" }
  $Dll = $found.FullName
}
$Out = Join-Path $Here "cm93depth_pi.dll"
Copy-Item $Dll $Out -Force
Write-Host "    $Out"

# ---- 7. Plugin tarball for OpenCPN's "Import plugin..." -------------------------
# Layout OpenCPN's plugin installer expects (model/src/plugin_handler.cpp):
#   <top dir>/metadata.xml
#   <top dir>/plugins/cm93depth_pi.dll   -> %LOCALAPPDATA%\opencpn\plugins\
# The metadata target must match OpenCPN's own: "msvc-wx32" for 32-bit,
# "msvc-64" for 64-bit Windows builds. The name must equal the plugin's
# common name so OpenCPN can tie the installed files to the loaded plugin.
Step "Packaging the plugin for OpenCPN"
$hdrText = Get-Content (Join-Path $PluginDir "src\cm93depth_pi.h") -Raw
$vparts = foreach ($k in @("MAJOR", "MINOR", "PATCH")) {
  if ($hdrText -match "PLUGIN_VERSION_$k\s+(\d+)") { $matches[1] } else { "0" }
}
$PlugVer = ($vparts -join ".")
$Target = if ($Arch -eq "x64") { "msvc-64" } else { "msvc-wx32" }
$TArch = if ($Arch -eq "x64") { "x86_64" } else { "x86" }
$TopName = "cm93depth_pi-$PlugVer-0_$Target-10-$TArch"
$Dist = Join-Path $Here "dist"
$Stage = Join-Path $Work "stage"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "$TopName\plugins") | Out-Null
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
Copy-Item $Out (Join-Path $Stage "$TopName\plugins\cm93depth_pi.dll") -Force
# No <?xml?> line: OpenCPN wraps this file inside its own XML when reading it.
$meta = @"
<plugin version="1">
  <name>CM93 Least Depth</name>
  <version>$PlugVer.0</version>
  <release>0</release>
  <summary>Least charted depth and look-ahead from CM93 charts, output as NMEA</summary>
  <api-version>1.18</api-version>
  <open-source>yes</open-source>
  <author>cm93depth_pi</author>
  <source>local build</source>
  <info-url></info-url>
  <description>Reads the CM93 vector charts at the vessel and ahead of it. Sends the least charted depth (`$PCMLD) and the first shoal ahead (`$PCMLA) as NMEA 0183. Built for OpenCPN $OcpnVersion, $Arch, wxWidgets $WxSeries.</description>
  <target>$Target</target>
  <target-version>10</target-version>
  <target-arch>$TArch</target-arch>
  <tarball-url></tarball-url>
  <tarball-checksum></tarball-checksum>
</plugin>
"@
[IO.File]::WriteAllText((Join-Path $Stage "$TopName\metadata.xml"), $meta,
                        (New-Object Text.UTF8Encoding $false))
$tarExe = Join-Path $env:SystemRoot "System32\tar.exe"
if (-not (Test-Path $tarExe)) { Fail "tar.exe not found (it comes with Windows 10 1803 and later)." }
$Tarball = Join-Path $Dist "$TopName.tar.gz"
if (Test-Path $Tarball) { Remove-Item $Tarball -Force }
# files only (-n: no directory entries), forward slashes as OpenCPN expects
& $tarExe -czf $Tarball -C $Stage -n "$TopName/metadata.xml" "$TopName/plugins/cm93depth_pi.dll"
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Tarball)) { Fail "tar.exe could not create $Tarball" }
Write-Host "    $Tarball"

if (-not $Install) {
  Write-Host ""
  Write-Host "Built and packaged:" -ForegroundColor Green
  Write-Host "    $Tarball"
  Write-Host "To install: start OpenCPN, Options > Plugins > Import plugin..., choose this"
  Write-Host "file, then tick/enable 'CM93 Least Depth' in the plugin list."
  Write-Host "The same file installs on any PC with OpenCPN $Arch ($Target)."
  exit 0
}

# ---- 8. Direct install (only with -Install) ------------------------------------------
Step "Installing"
$dest = Join-Path $env:LOCALAPPDATA "opencpn\plugins"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
# OpenCPN must be closed: it holds the DLL and rewrites opencpn.ini on exit.
while (Get-Process -Name opencpn -ErrorAction SilentlyContinue) {
  Write-Host "    OpenCPN is running. Close it, then press Enter to continue." -ForegroundColor Yellow
  [void](Read-Host)
}
try { Copy-Item $Out (Join-Path $dest "cm93depth_pi.dll") -Force }
catch { Fail "Could not copy to $dest (is OpenCPN still running?)" }
Write-Host "    $dest\cm93depth_pi.dll"

# OpenCPN loads hand-installed plugins disabled. Switch it on in opencpn.ini:
#   [PlugIns/cm93depth_pi.dll]
#   bEnabled=1
# The file is handled as raw bytes (Latin-1 maps 1:1), so nothing else in it
# is re-encoded, and a backup is kept next to it.
function Enable-PluginInIni([string]$ini) {
  $section = "[PlugIns/cm93depth_pi.dll]"
  $latin1 = [Text.Encoding]::GetEncoding(28591)
  $text = ""
  if (Test-Path $ini) {
    $text = $latin1.GetString([IO.File]::ReadAllBytes($ini))
    Copy-Item $ini "$ini.bak-cm93depth" -Force
  }
  $nl = if ($text -match "`r`n") { "`r`n" } else { "`n" }
  # group 1 = header line ending, group 2 = the section's key lines
  $pattern = '(?ms)^\[PlugIns/cm93depth_pi\.dll\][ \t]*(\r?\n|\z)(.*?)(?=^\[|\z)'
  $m = [regex]::Match($text, $pattern)
  if ($m.Success) {
    $body = $m.Groups[2].Value
    if ($body -match '(?m)^bEnabled=') {
      $newBody = [regex]::Replace($body, '(?m)^bEnabled=[^\r\n]*', 'bEnabled=1')
    } elseif ($m.Groups[1].Length -eq 0) {   # header was the last line
      $newBody = $nl + "bEnabled=1" + $nl
    } else {
      $newBody = "bEnabled=1" + $nl + $body
    }
    $text = $text.Substring(0, $m.Groups[2].Index) + $newBody +
            $text.Substring($m.Groups[2].Index + $m.Groups[2].Length)
  } else {
    if ($text.Length -gt 0 -and -not $text.EndsWith("`n")) { $text += $nl }
    $text += $section + $nl + "bEnabled=1" + $nl
  }
  [IO.File]::WriteAllBytes($ini, $latin1.GetBytes($text))
}

$ini = Join-Path $env:ProgramData "opencpn\opencpn.ini"
$enabled = $false
if (Test-Path (Split-Path $ini)) {
  try {
    Enable-PluginInIni $ini
    $enabled = $true
    Write-Host "    enabled in $ini (backup: opencpn.ini.bak-cm93depth)"
  } catch {
    Write-Host "    Could not edit $ini ($($_.Exception.Message))." -ForegroundColor Yellow
  }
}
Write-Host ""
if ($enabled) {
  Write-Host "Done. Start OpenCPN; the CM93 Least Depth button is on the toolbar." -ForegroundColor Green
} else {
  Write-Host "Done, but the plugin could not be switched on automatically." -ForegroundColor Yellow
  Write-Host "With OpenCPN closed, add these two lines to the end of $ini :"
  Write-Host "    [PlugIns/cm93depth_pi.dll]"
  Write-Host "    bEnabled=1"
  Write-Host "or run build_windows.bat again as administrator."
}
