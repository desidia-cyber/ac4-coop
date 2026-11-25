@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play_coop_host.ps1" -Steam
if errorlevel 1 pause
