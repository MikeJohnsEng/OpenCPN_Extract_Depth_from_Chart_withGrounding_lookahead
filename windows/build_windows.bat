@echo off
:: Double-click to build cm93depth_pi.dll for the OpenCPN on this PC and
:: package it as a tarball for OpenCPN's Options > Plugins > Import plugin...
:: (in the dist folder). See README-windows.txt for what you need first.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows.ps1" %*
echo.
pause
