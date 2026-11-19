@echo off
:: Copyright (C) 2026 Mike
:: SPDX-License-Identifier: GPL-2.0-or-later
:: GNU General Public License version 2 or later; see the file LICENSE.
:: Double-click to build cm93depth_pi.dll for the OpenCPN on this PC and
:: package it as a tarball for OpenCPN's Options > Plugins > Import plugin...
:: (in the dist folder). See README-windows.txt for what you need first.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows.ps1" %*
echo.
pause
