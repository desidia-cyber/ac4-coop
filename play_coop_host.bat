@echo off
setlocal
set PORT=%~1
if "%PORT%"=="" set PORT=7777
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play_coop_host.ps1" -Port %PORT%
if errorlevel 1 pause
