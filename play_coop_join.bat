@echo off
setlocal
set HOST_IP=%~1
set PORT=%~2
if "%PORT%"=="" set PORT=7777
if "%HOST_IP%"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play_coop_join.ps1" -Port %PORT%
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play_coop_join.ps1" -HostAddr "%HOST_IP%" -Port %PORT%
)
if errorlevel 1 pause
